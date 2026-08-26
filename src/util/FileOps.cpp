#include "FileOps.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

namespace {

// Bounded name scratch per recursion level (SdFat long names top out at 255,
// but the paths this device stores are far shorter).
constexpr size_t kNameBuf = 160;

}  // namespace

namespace FileOps {

CopyBuffer::CopyBuffer(const size_t bytes) {
  data_ = makeUniqueNoThrow<uint8_t[]>(bytes);
  if (!data_) {
    LOG_ERR("FileOps", "OOM: copy buffer (%u bytes)", static_cast<unsigned>(bytes));
    return;
  }
  size_ = bytes;
}

std::string joinPath(const std::string& base, const std::string& leaf) {
  if (base.empty() || base == "/") return "/" + leaf;
  if (base.back() == '/') return base + leaf;
  return base + "/" + leaf;
}

std::string baseName(const std::string& path) {
  std::string p = path;
  while (p.size() > 1 && p.back() == '/') p.pop_back();
  const auto pos = p.rfind('/');
  return pos == std::string::npos ? p : p.substr(pos + 1);
}

std::string parentPath(const std::string& path) {
  std::string p = path;
  while (p.size() > 1 && p.back() == '/') p.pop_back();
  const auto pos = p.rfind('/');
  if (pos == std::string::npos) return "/";
  return pos == 0 ? "/" : p.substr(0, pos);
}

bool copyFile(const std::string& src, const std::string& dst, const CopyBuffer& buf) {
  if (!buf.valid()) return false;
  if (Storage.exists(dst.c_str())) return false;

  HalFile in;
  if (!Storage.openFileForRead("FileOps", src, in)) return false;
  HalFile out;
  if (!Storage.openFileForWrite("FileOps", dst, out)) return false;

  for (;;) {
    const int n = in.read(buf.data(), buf.size());
    if (n < 0) {
      LOG_ERR("FileOps", "read failed: %s", src.c_str());
      out.close();
      Storage.remove(dst.c_str());  // never leave a half-written file behind
      return false;
    }
    if (n == 0) break;
    if (out.write(buf.data(), static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      LOG_ERR("FileOps", "write failed: %s", dst.c_str());
      out.close();
      Storage.remove(dst.c_str());
      return false;
    }
  }
  out.close();
  return true;
}

bool copyTree(const std::string& src, const std::string& dst, const CopyBuffer& buf, const int depth) {
  if (!buf.valid() || depth > kMaxDepth) return false;
  // Copying a directory into itself would recurse forever as the walk keeps
  // finding what it just wrote.
  if (FsHelpers::isSameOrDescendantPath(dst, src)) {
    LOG_ERR("FileOps", "refusing to copy %s into its own subtree", src.c_str());
    return false;
  }
  if (!Storage.ensureDirectoryExists(dst.c_str())) return false;

  auto dir = Storage.open(src.c_str());
  if (!dir || !dir.isDirectory()) return false;
  dir.rewindDirectory();

  bool ok = true;
  char name[kNameBuf];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.getName(name, sizeof(name)) == 0) continue;
    const std::string childSrc = joinPath(src, name);
    const std::string childDst = joinPath(dst, name);
    if (entry.isDirectory()) {
      entry.close();
      if (!copyTree(childSrc, childDst, buf, depth + 1)) ok = false;
    } else {
      entry.close();
      if (!copyFile(childSrc, childDst, buf)) ok = false;
    }
  }
  dir.close();
  return ok;
}

bool copyAny(const std::string& src, const std::string& dst, const CopyBuffer& buf) {
  auto probe = Storage.open(src.c_str());
  if (!probe) return false;
  const bool isDir = probe.isDirectory();
  probe.close();
  return isDir ? copyTree(src, dst, buf) : copyFile(src, dst, buf);
}

bool removeTree(const std::string& path, const int depth) {
  if (depth > kMaxDepth) return false;
  auto node = Storage.open(path.c_str());
  if (!node) return false;
  if (!node.isDirectory()) {
    node.close();
    return Storage.remove(path.c_str());
  }
  node.rewindDirectory();

  bool ok = true;
  char name[kNameBuf];
  for (auto entry = node.openNextFile(); entry; entry = node.openNextFile()) {
    if (entry.getName(name, sizeof(name)) == 0) continue;
    const std::string child = joinPath(path, name);
    const bool childIsDir = entry.isDirectory();
    entry.close();
    if (childIsDir) {
      if (!removeTree(child, depth + 1)) ok = false;
    } else if (!Storage.remove(child.c_str())) {
      ok = false;
    }
  }
  node.close();
  // The directory itself is only empty (and removable) once the walk succeeded.
  if (!ok) return false;
  return Storage.rmdir(path.c_str());
}

uint64_t treeSize(const std::string& path, const int depth) {
  if (depth > kMaxDepth) return 0;
  auto node = Storage.open(path.c_str());
  if (!node) return 0;
  if (!node.isDirectory()) {
    const uint64_t size = node.fileSize64();
    node.close();
    return size;
  }
  node.rewindDirectory();

  uint64_t total = 0;
  char name[kNameBuf];
  for (auto entry = node.openNextFile(); entry; entry = node.openNextFile()) {
    if (entry.getName(name, sizeof(name)) == 0) continue;
    if (entry.isDirectory()) {
      const std::string child = joinPath(path, name);
      entry.close();
      total += treeSize(child, depth + 1);
    } else {
      total += entry.fileSize64();
      entry.close();
    }
  }
  node.close();
  return total;
}

void formatSize(const uint64_t bytes, char* out, const size_t cap) {
  if (bytes < 1024) {
    snprintf(out, cap, "%u B", static_cast<unsigned>(bytes));
  } else if (bytes < 1024ULL * 1024) {
    const unsigned kb = static_cast<unsigned>(bytes / 1024);
    const unsigned frac = static_cast<unsigned>((bytes % 1024) * 10 / 1024);
    snprintf(out, cap, "%u.%u KB", kb, frac);
  } else if (bytes < 1024ULL * 1024 * 1024) {
    const unsigned mb = static_cast<unsigned>(bytes / (1024ULL * 1024));
    const unsigned frac = static_cast<unsigned>((bytes % (1024ULL * 1024)) * 10 / (1024ULL * 1024));
    snprintf(out, cap, "%u.%u MB", mb, frac);
  } else {
    const unsigned gb = static_cast<unsigned>(bytes / (1024ULL * 1024 * 1024));
    const unsigned frac = static_cast<unsigned>((bytes % (1024ULL * 1024 * 1024)) * 10 / (1024ULL * 1024 * 1024));
    snprintf(out, cap, "%u.%u GB", gb, frac);
  }
}

std::string uniqueNameIn(const std::string& dir, const std::string& name) {
  if (!Storage.exists(joinPath(dir, name).c_str())) return name;
  // Split off the extension so the suffix lands before it.
  const auto dot = name.rfind('.');
  const std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
  const std::string ext = dot == std::string::npos || dot == 0 ? "" : name.substr(dot);
  for (int n = 2; n < 100; ++n) {
    char suffix[8];
    snprintf(suffix, sizeof(suffix), " (%d)", n);
    const std::string candidate = stem + suffix + ext;
    if (!Storage.exists(joinPath(dir, candidate).c_str())) return candidate;
  }
  return name;  // caller's exists-check will reject it
}

}  // namespace FileOps
