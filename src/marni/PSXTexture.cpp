// PSXTexture.cpp - MarniSystem PSXTexture implementation
// Handles PS1 TIM/PIX image format parsing and texture setup
// Original Store method at 0x0041fb60

#include "PSXTexture.h"
#include "MarniBits.h"
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <string.h>
#include <windows.h>

// VideoDriver_ClearArrayD0 (0x0041fb10)
// Original: Iterates over embedded CMarniBits sub-objects (0x68 bytes apart)
// starting at offset 0x00 (the PSXTexture itself as a CMarniBits).
// For each sub-object up to m_NumCLUTs, calls vtable[6] = Release.
// Then zeroes m_NumCLUTs (offset 0x340) and m_IsInitialized (offset 0x344).
// 
// In PSXTexture, the layout has 8 embedded CMarniBits-like sub-objects at:
//   0x00, 0x68, 0xD0, 0x138, 0x1A0, 0x208, 0x270, 0x2D8
// The count at offset 0x340 tracks how many are active.
void VideoDriver_ClearArrayD0(PSXTexture* tex) {
    BYTE* self = (BYTE*)tex;
    int count = *(int*)(self + 0x340);  // m_NumCLUTs
    if (count != 0) {
        CMarniBits* bits = (CMarniBits*)self;
        for (int i = 0; i < count; i++) {
            // Call Release: vtable[6] at vtable + 0x18
            bits->Release();
            bits = (CMarniBits*)((BYTE*)bits + 0x68);
        }
    }
    *(int*)(self + 0x340) = 0;  // m_NumCLUTs = 0
    *(int*)(self + 0x344) = 0;  // m_IsInitialized = 0
}

// FUN_004033f0 = CMarniBits::SetAddress (0x004033f0)
// Original: THISCALL, 'this' in ECX, pixels on stack, clut on stack
// Called from PSXTexture::Store and PSXTexture::SetAddress
int CMarniBits_SetAddress(void* self, void* pixels, void* clut) {
    return ((CMarniBits*)self)->SetAddress(pixels, clut);
}

// FUN_004034d0 = CMarniBits::CopyFrom / operator= (0x004034d0)
int CMarniBits_CopyFrom(void* dest, void* src) {
    return ((CMarniBits*)dest)->CopyFrom((CMarniBits*)src);
}

extern void* operator_new(size_t size);
extern void  operator_delete(void* ptr);

// ============================================================================
// PSXTexture Constructor (0x0041fee0)
// Original: Constructs 8 embedded CMarniBits sub-objects at 0x68-byte intervals
// using _eh_vector_constructor_iterator_. Then zeroes count fields.
// ============================================================================
PSXTexture::PSXTexture()
{
    // Construct 8 embedded CMarniBits sub-objects at offsets 0x00, 0x68, 0xD0, 
    // 0x138, 0x1A0, 0x208, 0x270, 0x2D8 (each 0x68 bytes apart)
    // The first at offset 0x00 IS the PSXTexture's CMarniBits base
    for (int i = 0; i < 8; i++) {
        new ((BYTE*)this + i * 0x68) CMarniBits();
    }
    
    m_NumCLUTs = 0;      // 0x340
    m_IsInitialized = 0;  // 0x344
}

// ============================================================================
// PSXTexture Destructor (0x0041ffb0 / 0x00420000 wrappers)
// Original: Calls ClearCLUTEntries (VideoDriver_ClearArrayD0) to Release all
// sub-objects, then calls _eh_vector_destructor_iterator_ to destroy them.
// In modern C++ we call the destructor directly in reverse order.
// ============================================================================
PSXTexture::~PSXTexture()
{
    // First release any owned pixel data from the main sub-object
    CMarniBits* mainBits = (CMarniBits*)this;
    if (mainBits->m_pPixelData && mainBits->m_dataSource == 1) {
        operator_delete(mainBits->m_pPixelData);
    }
    mainBits->m_pPixelData = NULL;

    // CRITICAL FIX: Clear ownership flags and dangling pointers on all 8
    // embedded CMarniBits sub-objects BEFORE calling their destructors.
    //
    // PROBLEM: PSXTexture::Store() sets m_Flag4C=1 at PSXTexture offset 0x4C,
    // which overlaps with CMarniBits::m_ownsPalette at the same offset.
    // During multi-CLUT setup, CMarniBits_CopyFrom() propagates m_dataSource=1
    // and m_ownsPalette=1 to sub-objects at offsets 0x68-0x2D8. When their
    // ~CMarniBits() -> Release() runs, it checks:
    //     if (m_dataSource != 0 && m_ownsPalette != 0) { free(m_pPixelData); free(m_pPalette); }
    // This triggers a double-free of pixelBuf (already freed above) AND
    // attempts to free m_pPalette which points to the global g_image_buffer
    // (BSS memory, not heap), causing _CrtIsValidHeapPointer assertion.
    //
    // Fix: Reset ownership and pointer fields so Release() becomes a no-op.
    for (int i = 0; i < 8; i++) {
        CMarniBits* sub = (CMarniBits*)((BYTE*)this + i * 0x68);
        sub->m_dataSource = 0;
        sub->m_ownsPalette = 0;
        sub->m_pPixelData = NULL;
        sub->m_pPalette = NULL;
    }

    // Destroy 8 embedded CMarniBits sub-objects (reverse construction order)
    for (int i = 7; i >= 0; i--) {
        ((CMarniBits*)((BYTE*)this + i * 0x68))->~CMarniBits();
    }
}

// ============================================================================
// Store - Parse PSX TIM/PIX image from buffer (0x0041fb60)
//
// TIM file format (little-endian):
//   Offset 0:  Magic number (must be 0x00000010)
//   Offset 4:  Flags (bit 3 = has CLUT palette)
//   If CLUT present:
//     Offset 8:  CLUT data size (bytes)
//     Offset 12: CLUT origin X (low 16), Y (high 16)
//     Offset 16: CLUT width (low 16), height (high 16)
//     Offset 20: CLUT color data (size - 12 bytes)
//   Then image section:
//     Offset N:   Image data size (bytes)
//     Offset N+4: Image origin X (low 16), Y (high 16)
//     Offset N+8: Image width in 16-bit words, height
//     Offset N+12: Pixel data
//
// PIX files: same as TIM but without the magic header (raw pixel data).
// They must be loaded with appropriate width/height context.
// ============================================================================
int PSXTexture::Store(int* imageData, int copyData)
{
    // 0x0041fb70: Validate image data pointer
    if (imageData == NULL) {
        printf("[PSXTexture::Store] ERROR: imageData is NULL!\n");
        return 0;
    }

    int* clutPtr = NULL;
    int* pixelPtr;

    // 0x0041fb80: Check magic number
    if (*imageData != 0x10) {
        printf("[PSXTexture::Store] Invalid magic: 0x%08X\n", *imageData);
        return 0;
    }

    VideoDriver_ClearArrayD0(this);
    m_NumCLUTs = 1;

    // 0x0041fba0: Parse flags
    DWORD flags = (DWORD)imageData[1];

    if (flags & 8) {
        // Has CLUT palette
        WORD clutOriginX = (WORD)(imageData[3] >> 16);      // CLUT X origin
        WORD clutOriginY = (WORD)(imageData[3] & 0xFFFF);   // CLUT Y origin
        clutPtr = imageData + 5;                             // CLUT data starts here

        WORD clutW = (WORD)(imageData[4] >> 16);            // CLUT width (colors)
        WORD clutH = (WORD)(imageData[4] & 0xFFFF);         // CLUT height (1 for 4/8bpp, variable for 16bpp)

        if (clutW > 8) {
            printf("[PSXTexture::Store] Invalid CLUT width: %d\n", clutW);
            return 0;
        }

        m_NumCLUTs = clutW;

        if (!copyData) {
            // Use CLUT data in-place (skip past CLUT data to image section)
            pixelPtr = (int*)((BYTE*)clutPtr + clutW * clutH * 2);
        } else {
            // Allocate and copy CLUT data into CLUT-specific field
            int clutDataSize = clutW * clutH * 2;
            m_pPixelData = NULL;  // will be set to pixel data below
            if (clutDataSize > (int)sizeof(m_CLUT_Data)) {
                printf("[PSXTexture::Store] CLUT too large: %d bytes\n", clutDataSize);
                return 0;
            }

            // Copy CLUT data to m_CLUT_Data (not m_pPixelData)
            WORD* src = (WORD*)clutPtr;
            WORD* dst = (WORD*)m_CLUT_Data;
            for (int i = 0; i < (int)(clutW * clutH); i++) {
                dst[i] = src[i];
            }

            // Image data follows CLUT
            pixelPtr = (int*)(src + clutW * clutH);
        }
    } else {
        // No CLUT: image data follows header directly
        pixelPtr = imageData + 2;
    }

    // 0x0041fc30: Parse image section
    DWORD imgFlags   = (DWORD)pixelPtr[1];                  // Image data size / flags
    // TIM format: Low word = width (pixels for 16bpp, halfwords for 4/8bpp), High word = height
    WORD  imgW       = (WORD)(pixelPtr[2] & 0xFFFF);        // Width in 16-bit words
    WORD  imgH       = (WORD)(pixelPtr[2] >> 16);           // Height in pixels
    int*  imgData    = pixelPtr + 3;                         // Pixel data starts here

    if (copyData) {
        // Allocate and copy pixel data
        int pixelDataSize = imgW * imgH * 2;
        void* pixelBuf = operator_new(pixelDataSize);
        if (pixelBuf == NULL) {
            printf("[PSXTexture::Store] Failed to allocate pixel memory\n");
            return 0;
        }

        WORD* src = (WORD*)imgData;
        WORD* dst = (WORD*)pixelBuf;
        for (int y = 0; y < imgH; y++) {
            for (int x = 0; x < imgW; x++) {
                dst[y * imgW + x] = src[y * imgW + x];
            }
        }
        m_pPixelData = pixelBuf;  // Always set to pixel data (CLUT stored separately)
        imgData = (int*)pixelBuf;
    }

    // 0x0041fce0: Set up texture descriptor fields
    m_clipX = 0;
    m_clipY = 0x1F;
    m_dispW = 10;
    m_clipW = 5;
    m_clipH = 5;
    m_dispX = 0x1F;
    m_dispY = 5;
    m_rectX = 0x1F;
    m_rectY = 5;
    m_RowStride = imgW * 2;

    // 0x0041fd30: Set bit depth based on flags
    DWORD bppFlags = flags & 7;
    switch (bppFlags) {
        case 0: // 4 bpp (16-color CLUT)
            m_BitDepth = 4;
            m_FormatFlags = 0x10;
            m_HasCLUT = 1;
            m_WidthPixels = imgW * 4;   // 4 pixels per 16-bit word
            break;
        case 1: // 8 bpp (256-color CLUT)
            m_BitDepth = 8;
            m_FormatFlags = 0x10;
            m_HasCLUT = 1;
            m_WidthPixels = imgW * 2;   // 2 pixels per 16-bit word
            break;
        case 2: // 16 bpp direct color (RGB555)
            m_BitDepth = 16;
            m_FormatFlags = 0;
            m_HasCLUT = 0;
            m_WidthPixels = imgW;       // 1 pixel per 16-bit word
            break;
        default:
            printf("[PSXTexture::Store] Unsupported bit depth flag: %d\n", bppFlags);
            return 0;
    }

    m_Height = imgH;
    m_RowStride = imgW * 2;

    // 0x0041fd90: CLUT info from earlier parse
    if (flags & 8) {
        WORD clutOriginX = (WORD)(imageData[3] >> 16);
        WORD clutOriginY = (WORD)(imageData[3] & 0xFFFF);
        WORD clutW = (WORD)(imageData[4] >> 16);
        WORD clutH = (WORD)(imageData[4] & 0xFFFF);
        m_CLUT_X = clutOriginX;
        m_CLUT_W = clutW;
        m_CLUT_Y = clutOriginY;
        m_CLUT_H = clutH;
    }

    // 0x0041fe00: Set pixel address
    CMarniBits_SetAddress(this, imgData, clutPtr);

    // NOTE: m_Flag4C at PSXTexture offset 0x4C overlaps with CMarniBits::m_ownsPalette.
    // Setting this to 1 causes CMarniBits::Release() to believe it owns the pixel/palette
    // memory, leading to double-free and invalid-free bugs. The PSXTexture destructor has
    // a guard loop that clears m_dataSource/m_ownsPalette on all sub-objects before calling
    // their destructors. Do NOT remove that guard without addressing this overlap.
    m_Flag4C = 1;
    m_DataSource = copyData;
    m_Flag50 = 1;
    m_IsLocked = 1;
    m_Flag64 = 0;

    // 0x0041fe50: Multi-CLUT support
    if (m_NumCLUTs > 1) {
        WORD* clutEntry = (WORD*)(((BYTE*)this) + 0xC0);
        for (DWORD n = 1; n < m_NumCLUTs; n++) {
            // Set up each CLUT entry's fields
            DWORD* entry = (DWORD*)((BYTE*)this + 0xC0 + (n - 1) * sizeof(DWORD) * 7);
            entry[-5] = 0;                                          // Flag
            CMarniBits_CopyFrom((BYTE*)this + 0x68, this);        // Setup palette (subobject at offset 0x68)
            entry[-1] = m_CLUT_X + (n << (m_BitDepth & 0x1F));    // CLUT X + offset
            entry[0]  = m_CLUT_Y;                                  // CLUT Y
            entry[1]  = m_CLUT_W;                                  // CLUT width
            entry[2]  = m_CLUT_H;                                  // CLUT height
            entry[3]  = m_Flag64;                                  // Flag

            // Set up pixel data for this CLUT
            DWORD offset = n << (m_BitDepth & 0x1F);
            CMarniBits_SetAddress((BYTE*)this + 0x68, (WORD*)((BYTE*)imgData + offset * 2), (WORD*)clutPtr);

            entry[0] = entry[0] + n;
        }
    }

    m_IsInitialized = 1;
    return 1;
}

// ============================================================================
// SetAddress - Set pixel and CLUT data pointers (0x004033f0 wrapper)
// ============================================================================
int PSXTexture::SetAddress(void* pixelData, void* clutData)
{
    if (m_IsLocked) {
        printf("[PSXTexture::SetAddress] Surface is locked\n");
        return 0;
    }
    if (m_DataSource == 1) {
        printf("[PSXTexture::SetAddress] Cannot set address on copied data\n");
        return 0;
    }
    m_pPixelData = pixelData;
    m_Pitch = (DWORD)clutData;  // Original stores CLUT data pointer here
    m_DataSource = 0;
    return 1;
}

// ============================================================================
// ClearCLUTEntries - Release all embedded CMarniBits sub-objects (0x0041fb10)
// Original name: VideoDriver_ClearArrayD0
// Called before loading a new texture to clear any existing data.
// ============================================================================
void PSXTexture::ClearCLUTEntries() {
    VideoDriver_ClearArrayD0(this);
}

// ============================================================================
// LoadFromFile - Open a TIM file and call Store (0x0041fa60)
// Original: Opens file with _lopen, reads entire content with _lread,
// passes buffer to LoadPSXImage (Store) with copyData=1, frees buffer.
// ============================================================================
int PSXTexture::LoadFromFile(const char* filename) {
    // 0x0041fa60
    // Original used _lopen/_llseek/_lread/_lclose CRT functions
    // Modern port uses Win32 CreateFile/ReadFile
    
    HANDLE hFile = CreateFileA(
        filename,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hFile == INVALID_HANDLE_VALUE) {
        char buf[256];
        sprintf_s(buf, sizeof(buf), "can't open file %s", filename);
        printf("%s: MarniSystem PSXTexture::Store\n", buf);
        return 0;
    }
    
    // Get file size
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return 0;
    }
    
    // Allocate buffer (original rounds up to 4-byte alignment + 16 bytes safety)
    DWORD alignedSize = (fileSize & 0xFFFFFFFC) + 0x10;
    void* buffer = operator_new(alignedSize);
    if (buffer == NULL) {
        CloseHandle(hFile);
        return 0;
    }
    
    // Read entire file
    DWORD bytesRead;
    BOOL readResult = ReadFile(hFile, buffer, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    
    if (!readResult || bytesRead != fileSize) {
        free(buffer);
        return 0;
    }
    
    // Parse TIM data (copyData=1: allocate and copy pixel data)
    int result = Store((int*)buffer, 1);
    
    // Free the temporary file buffer
    free(buffer);
    
    return result;
}

// ============================================================================
// ArrayClear - Reset PSXTexture array (0x00420000)
// Original: Called with 'this' as the PSXTexture to clear.
// 1. Calls VideoDriver_ClearArrayD0 to Release sub-objects
// 2. Calls _eh_vector_destructor_iterator_ to destroy all 8 sub-objects
// 3. Handles SEH via FUN_00420043 (CxxFrameHandler wrapper)
// ============================================================================
void PSXTexture::ArrayClear(PSXTexture* tex) {
    // 0x00420000
    // Step 1: Release all embedded CMarniBits (clear pixel/palette data)
    VideoDriver_ClearArrayD0(tex);
    
    // Step 2: Destroy all 8 embedded CMarniBits sub-objects (reverse order)
    for (int i = 7; i >= 0; i--) {
        ((CMarniBits*)((BYTE*)tex + i * 0x68))->DestructorBody();
    }
}

// ============================================================================
// ConstructElement - Construct a single embedded CMarniBits (0x0041ff60)
// Original: Called by _eh_vector_constructor_iterator_ for each of 8 elements.
// Wraps CMarniBits_Constructor with SEH frame.
// ============================================================================
void PSXTexture::ConstructElement(void* element) {
    // 0x0041ff60
    // Simply placement-construct a CMarniBits at the given address
    new (element) CMarniBits();
}

// ============================================================================
// DestroyElement - Destroy a single embedded CMarniBits (0x0041ffb0)
// Original: Called by _eh_vector_destructor_iterator_ for each of 8 elements.
// Calls FUN_0041fff1 → CMarniBits_DestructorBody with SEH frame.
// ============================================================================
void PSXTexture::DestroyElement(void* element) {
    // 0x0041ffb0 → 0x0041fff1 → CMarniBits_DestructorBody
    ((CMarniBits*)element)->DestructorBody();
}

// ============================================================================
// CopyFrom / operator= (0x0041f9c0)
// Copies a PSXTexture including all 8 embedded CMarniBits CLUT entries.
// Iterates over 8 CLUT slots at 0x68-byte intervals.
// For each active CLUT entry in the source, copies the CMarniBits data and
// CLUT descriptor fields (offsets 0x54-0x64 within each CLUT block).
// Then copies m_NumCLUTs and sets m_IsInitialized.
// ============================================================================
int PSXTexture::CopyFrom(PSXTexture* src) {
    // 0x0041f9c0
    // Clear existing data first
    VideoDriver_ClearArrayD0(this);

    // Each CLUT entry block is 0x68 bytes
    BYTE* pSrcBase = (BYTE*)src;
    BYTE* pDstBase = (BYTE*)this;

    for (int clutIdx = 0; clutIdx < 8; clutIdx++) {
        BYTE* pSrcClut = pSrcBase + clutIdx * 0x68;
        BYTE* pDstClut = pDstBase + clutIdx * 0x68;

        // Check if this CLUT entry is active (offset +0x40 within CLUT block = m_IsLocked)
        if (*(int*)(pSrcClut + 0x40) == 1) {
            // Copy the CMarniBits sub-object from src to dst
            CMarniBits_CopyFrom(pDstClut, pSrcClut);

            // Copy CLUT descriptor fields (+0x54 through +0x64)
            DWORD* dstFields = (DWORD*)(pDstClut + 0x54);
            DWORD* srcFields = (DWORD*)(pSrcClut + 0x54);
            dstFields[0] = srcFields[0];  // +0x54: CLUT_X/Y
            dstFields[1] = srcFields[1];  // +0x58: CLUT_W/H
            dstFields[2] = srcFields[2];  // +0x5C: CLUT_W2/H2
            dstFields[3] = srcFields[3];  // +0x60: CLUT_W3/H3
            dstFields[4] = srcFields[4];  // +0x64: m_Flag64

            // Verify the destination's pixel data pointer is valid
            if (*(DWORD*)(pDstClut + 0x40) == 0) {
                printf("invalid data pointer: MarniSystem PSXTexture::operator =\n");
                return 0;
            }
        }
    }

    // Copy trailing fields
    *(DWORD*)(pDstBase + 0x340) = *(DWORD*)(pSrcBase + 0x340);  // m_NumCLUTs
    *(DWORD*)(pDstBase + 0x344) = 1;                              // m_IsInitialized
    return 1;
}
