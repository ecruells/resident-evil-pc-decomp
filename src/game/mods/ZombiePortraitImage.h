#pragma once

// CPU-only portrait generation. RGBA bytes, top-down; no entity/GPU state.
// PC EMD torso/head objects 0/1 face +X; render head-and-shoulders with a
// 15-degree turn and the camera looking down eight degrees.
bool zm_portrait_model(const unsigned char* emd, unsigned int size,
                       unsigned char* rgba30);
bool zm_portrait_sheet(const unsigned char* tim, unsigned int size,
                       unsigned char* rgba64);
bool zm_portrait_crop(const unsigned char* rgba, int width, int height,
                      int x, int y, int cropWidth, int cropHeight,
                      unsigned char* rgba30);
