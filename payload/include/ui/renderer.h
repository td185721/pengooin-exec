// renderer.h — D3D11 overlay renderer
// language: C++20, target: Windows 11 x64, MSVC
#pragma once
#include "pch.h"

namespace r9k::ui {

// installs D3D11 IDXGISwapChain::Present vtable hook.
// on Present, drains the draw list onto the current back buffer and forwards.
bool renderer_install();
void renderer_shutdown();

// immediate-mode primitives — thread-safe (mutex-guarded internal list)
struct DrawColor { u8 r{}, g{}, b{}, a{255}; };

void draw_line   (float x1, float y1, float x2, float y2, float thickness, DrawColor c);
void draw_rect   (float x,  float y,  float w,  float h,  DrawColor c, bool filled);
void draw_circle (float cx, float cy, float radius, int segments, DrawColor c, bool filled);
void draw_text   (float x,  float y,  const char* utf8, DrawColor c, float size);
void draw_triangle(float x1, float y1, float x2, float y2, float x3, float y3, DrawColor c, bool filled);
void draw_quad   (float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, DrawColor c, bool filled);

// per-frame clear at Present start; user code re-issues its draws every frame
// via a RunService.RenderStepped connection installed by boot Luau.
void frame_reset();

}
