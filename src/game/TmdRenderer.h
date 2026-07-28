// TmdRenderer.h - 3D TMD object render queue (DX11 replacement for the
// original DX5 ordering-table 3D path).
//
// Original data flow (from the binary):
//   options_render_entity / calc_entity_lighting
//     -> FUN_00483250 -> FUN_00483080 (0x00483080)
//          builds model->view float matrix, computes OT depth
//       -> CMarniDirect3DTMD::Transform (0x00415520)
//          copies the 16-float matrix into each per-object data entry
//          (objData + 8) and calls CMarniDirect3D vtable[10] (0x00448300)
//       -> OT_InsertPrimitive(objData, depth)
//   ...at present time the OT was walked and every queued TMD object was
//   rendered with D3D5 execute buffers.
//
// In this port the OT is replaced by a flat per-frame queue (TmdQueueObject)
// that FlushTmdObjects() drains once per frame from FrameRateGovernor,
// drawing through MarniDX::DrawTriangles.
//
// The original also had a D3D depth buffer resolving the triangles within one
// queued object. MarniDX::DrawTriangles carries no Z, so FlushTmdObjects
// depth-sorts individual triangles instead and does not backface-cull.
//
// See docs/MARNI_SYSTEM.md section 3B ("DX11 render path" and "Pipeline
// invariants") for the full chain and the list of constraints this path
// depends on.
#pragma once

// Queue a TMD per-object data entry (0x84-byte block inside a
// CMarniDirect3DTMD slot: m_objectData or m_objectDataCopy) for rendering
// this frame. Called from the CMarniDirect3D vtable[10] implementation.
//
// Records the pointer only. CMarniDirect3DTMD::Transform writes the object's
// model->view matrix AFTER queueing it, so the render state must be read at
// flush time, never snapshotted here.
void  TmdQueueObject(void* objData, int depth);

// Draw every queued TMD object, then clear the queue. Called once per frame
// from FrameRateGovernor after the background sprites and before the 2D sprite
// command flush. Triangles from all objects are pooled, depth-sorted far to
// near, and submitted with adjacent same-texture runs merged.
void  FlushTmdObjects(void);

// Drop every queued object without drawing (frame reset).
void  TmdQueue_Reset(void);
