#pragma once

// LittleFS-backed .pnt saves and loads. Kept out of the native build; the
// format itself is exercised by paint/pnt.h on the host.

#include <cstddef>
#include <cstdint>

#include "paint/document.h"
#include "paint/geometry.h"

namespace paint {

// Writes `path` through a `.tmp` file, verifies the result and only then
// renames it over `path`. A failed write leaves any existing file untouched.
bool savePnt(const char* path, const uint16_t* pixels, size_t stride, uint16_t width,
             uint16_t height);

// Verifies the header, size, dimensions and CRC32 before decoding. `stride`
// must be at least `width`; decoding writes rows with that stride.
bool loadPnt(const char* path, uint16_t* pixels, size_t stride, uint16_t width,
             uint16_t height, DocumentInfo& info);

// Draws the embedded 64x86 thumbnail into `dest`, clipped to `clip`. Only the
// rows touched by the clip are read from LittleFS, so the gallery can render
// a 2x2 page without keeping four 11 KB thumbnails in RAM.
bool drawPntThumbnail(const char* path, uint16_t* fb, int fbWidth,
                      const Rect& clip, const Rect& dest);

// Header, canvas dimensions, file size, CRC32 and RLE structure check without
// touching the canvas. Used to decide whether boot should offer recovery.
bool isPntFileValid(const char* path);

}  // namespace paint
