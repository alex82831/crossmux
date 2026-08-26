#pragma once

#include <cstdint>
#include <memory>
#include <string>

// Shared file/directory operations for the File Manager and the App Manager's
// install-from-disk path. Everything routes through the Storage HAL.
//
// Copies stream through ONE buffer owned by the caller for the whole
// operation (see CopyBuffer) rather than allocating per file — a recursive
// copy of a 200-file directory would otherwise churn the heap 200 times on a
// 380KB budget.
namespace FileOps {

// Recursion cap for tree walks. SD trees here are shallow (books, apps,
// fonts); the cap bounds stack use and stops a pathological tree from
// exhausting it.
constexpr int kMaxDepth = 8;

// One reusable streaming buffer. Allocation is checked; `valid()` is false on
// OOM and every entry point below then fails cleanly instead of aborting.
class CopyBuffer {
 public:
  explicit CopyBuffer(size_t bytes = 2048);
  bool valid() const { return data_ != nullptr; }
  uint8_t* data() const { return data_.get(); }
  size_t size() const { return size_; }

 private:
  std::unique_ptr<uint8_t[]> data_;
  size_t size_ = 0;
};

// Copy one file. Fails if `dst` exists (callers decide about overwriting).
bool copyFile(const std::string& src, const std::string& dst, const CopyBuffer& buf);

// Recursively copy a directory tree. Refuses to copy into its own descendant.
bool copyTree(const std::string& src, const std::string& dst, const CopyBuffer& buf, int depth = 0);

// Copy a file or a directory tree, whichever `src` is.
bool copyAny(const std::string& src, const std::string& dst, const CopyBuffer& buf);

// Recursively delete a directory and its contents (SdFat's removeDir only
// handles empty directories), or a single file.
bool removeTree(const std::string& path, int depth = 0);

// Total bytes of a directory tree, or the file's size.
uint64_t treeSize(const std::string& path, int depth = 0);

// Human-readable size ("12 B", "3.4 KB", "1.2 MB").
void formatSize(uint64_t bytes, char* out, size_t cap);

// "/a" + "b" -> "/a/b" (handles a trailing slash on the base).
std::string joinPath(const std::string& base, const std::string& leaf);

// Trailing path component ("/a/b.txt" -> "b.txt"; "/a/b/" -> "b").
std::string baseName(const std::string& path);

// Everything above the trailing component ("/a/b.txt" -> "/a"; "/" -> "/").
std::string parentPath(const std::string& path);

// `name` with " (2)", " (3)" … inserted before the extension until the result
// does not exist in `dir`. Used when pasting into the source's own directory.
std::string uniqueNameIn(const std::string& dir, const std::string& name);

}  // namespace FileOps
