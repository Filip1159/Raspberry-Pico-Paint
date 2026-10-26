#pragma once

// Gallery index and .pnt file-name helpers. The pure helpers live in
// src/storage/paint_names.cpp so the native UI tests can use them; the
// LittleFS-backed index is compiled only for the firmware.

#include <cstddef>
#include <cstdint>

namespace paint {

constexpr int kMaxPaintFiles = 32;
constexpr size_t kPaintFileNameSize = 16;  // "img9999.pnt" + NUL
constexpr char kPaintDirectory[] = "/paint";
constexpr char kAutosaveFileName[] = "autosave.pnt";
constexpr char kAutosavePath[] = "/paint/autosave.pnt";

// True for a bare "*.pnt" file name (no slashes).
bool isPntFileName(const char* name);

// The autosave is a .pnt file but is never shown as a gallery drawing.
bool isAutosaveFileName(const char* name);

// Parses imgNNNN.pnt and returns NNNN, or -1 when the name does not match.
int paintFileNumber(const char* name);

// Formats imgNNNN.pnt (1..9999). Returns false for an invalid number or a
// buffer that is too small.
bool paintFileNameForNumber(int number, char* out, size_t capacity);

// Builds /paint/<name>. Rejects empty names and names containing a slash.
bool paintFilePath(const char* name, char* out, size_t capacity);

// Scans /paint for .pnt files, sorted by name. The index is deliberately
// bounded: the gallery only needs enough entries for a few pages and every
// byte saved here is available to the canvas and Wi-Fi stack.
class PaintFileIndex {
 public:
  int refresh();
  int count() const { return count_; }
  int totalCount() const { return totalCount_; }
  const char* name(int index) const;
  bool remove(int index);

  // Picks the first free imgNNNN.pnt by checking the real directory, not just
  // this bounded index, so Save-as can never overwrite a file beyond the cap.
  bool nextName(char* out, size_t capacity) const;

 private:
  char names_[kMaxPaintFiles][kPaintFileNameSize] = {};
  int count_ = 0;
  int totalCount_ = 0;
};

}  // namespace paint
