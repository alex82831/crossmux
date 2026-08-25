// DynAppLoader — minimal ELF32 loader for CrossPoint dynamic apps on the
// ESP32-C3.
//
// Address-domain background (the part that makes this chip special): the C3
// maps its SRAM twice — data bus at 0x3FC80000+ and instruction bus at
// 0x40380000+ (SOC_I_D_OFFSET apart). Instruction fetch works only through
// the I window; byte loads/stores only through the D window. A single loaded
// image needs both, so the SDK links apps with text at vaddr TEXT_VBASE
// (0x700000 == SOC_I_D_OFFSET) and data at vaddr == its physical offset in
// the block. With that layout, for EVERY link-time address A:
//
//     runtime address = blockD + A
//
// lands text in the I window (A >= 0x700000, and blockD + 0x700000 is the
// block's I alias) and data in the D window — including addresses code
// derives at runtime via PC-relative auipc, because the link-time
// text->data deltas equal the runtime deltas by construction. The build
// script enforces the layout; this loader verifies it and refuses anything
// else. Requires CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n (set in platformio.ini
// custom_sdkconfig) so heap memory keeps MALLOC_CAP_EXEC.

#ifndef SIMULATOR

#include "DynAppLoader.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>
#include <soc/soc.h>

#include <cstring>

namespace {

// --- Fixed link layout, mirrored by sdk/dynapp/build-eapp.sh -------------
constexpr uint32_t kTextVBase = SOC_I_D_OFFSET;  // 0x700000 on the C3

// --- Minimal ELF32 structures (only what the loader touches) -------------
struct Elf32Ehdr {
  uint8_t ident[16];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint32_t entry;
  uint32_t phoff;
  uint32_t shoff;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;
};

struct Elf32Phdr {
  uint32_t type;
  uint32_t offset;
  uint32_t vaddr;
  uint32_t paddr;
  uint32_t filesz;
  uint32_t memsz;
  uint32_t flags;
  uint32_t align;
};

struct Elf32Shdr {
  uint32_t name;
  uint32_t type;
  uint32_t flags;
  uint32_t addr;
  uint32_t offset;
  uint32_t size;
  uint32_t link;
  uint32_t info;
  uint32_t addralign;
  uint32_t entsize;
};

struct Elf32Rela {
  uint32_t offset;
  uint32_t info;
  int32_t addend;
};

constexpr uint16_t kEtDyn = 3;
constexpr uint16_t kEmRiscv = 243;
constexpr uint32_t kPtLoad = 1;
constexpr uint32_t kShtRela = 4;
constexpr uint32_t kRRiscvRelative = 3;
constexpr uint32_t kRRiscvNone = 0;

// Map a link-time vaddr to the block-relative physical offset.
inline uint32_t vaddrToPhys(const uint32_t vaddr) { return vaddr >= kTextVBase ? vaddr - kTextVBase : vaddr; }

}  // namespace

const char* DynAppLoader::errorName(const Error e) {
  switch (e) {
    case Error::None:
      return "ok";
    case Error::FileOpen:
      return "file open";
    case Error::BadElf:
      return "bad elf";
    case Error::BadLayout:
      return "bad layout";
    case Error::TooLarge:
      return "too large";
    case Error::OutOfMemory:
      return "out of memory";
    case Error::BadReloc:
      return "bad reloc";
    case Error::BadEntry:
      return "bad entry";
    case Error::AbiMismatch:
      return "abi mismatch";
  }
  return "unknown";
}

DynAppLoader::Error DynAppLoader::load(const std::string& eappPath) {
  unload();

  HalFile file;
  if (!Storage.openFileForRead("DYNAPP", eappPath.c_str(), file)) {
    return Error::FileOpen;
  }

  Elf32Ehdr ehdr;
  if (file.read(&ehdr, sizeof(ehdr)) != static_cast<int>(sizeof(ehdr))) return Error::BadElf;
  static constexpr uint8_t kMagic[4] = {0x7F, 'E', 'L', 'F'};
  if (memcmp(ehdr.ident, kMagic, 4) != 0 || ehdr.ident[4] != 1 /*ELFCLASS32*/ || ehdr.ident[5] != 1 /*LSB*/ ||
      ehdr.type != kEtDyn || ehdr.machine != kEmRiscv || ehdr.phentsize != sizeof(Elf32Phdr) ||
      ehdr.shentsize != sizeof(Elf32Shdr)) {
    return Error::BadElf;
  }
  if (ehdr.phnum == 0 || ehdr.phnum > 8 || ehdr.shnum > 48) return Error::BadElf;

  // --- Program headers: compute the image span and sanity-check layout ---
  Elf32Phdr phdrs[8];  // bounded above; stack, not heap
  if (!file.seekSet(ehdr.phoff) ||
      file.read(phdrs, sizeof(Elf32Phdr) * ehdr.phnum) != static_cast<int>(sizeof(Elf32Phdr) * ehdr.phnum)) {
    return Error::BadElf;
  }

  uint32_t imageEnd = 0;
  bool sawText = false;
  for (int i = 0; i < ehdr.phnum; ++i) {
    const Elf32Phdr& p = phdrs[i];
    if (p.type != kPtLoad || p.memsz == 0) continue;
    if (p.filesz > p.memsz) return Error::BadElf;
    const bool isText = p.vaddr >= kTextVBase;
    if (isText) sawText = true;
    // Data must sit below the text window base or the domain split breaks.
    if (!isText && p.vaddr + p.memsz > kTextVBase) return Error::BadLayout;
    const uint32_t physEnd = vaddrToPhys(p.vaddr) + p.memsz;
    if (physEnd > imageEnd) imageEnd = physEnd;
  }
  if (!sawText || imageEnd == 0) return Error::BadLayout;
  if (imageEnd > kMaxImageBytes) {
    LOG_ERR("DYNAPP", "image %u bytes exceeds cap %u", static_cast<unsigned>(imageEnd),
            static_cast<unsigned>(kMaxImageBytes));
    return Error::TooLarge;
  }
  if (ehdr.entry < kTextVBase || vaddrToPhys(ehdr.entry) >= imageEnd) return Error::BadEntry;

  // --- One executable block for the whole image -------------------------
  // Transient by design: sized by the app (<= 96KB cap), freed in unload().
  // A stack/static alternative is impossible — the size is per-app and the
  // memory must carry MALLOC_CAP_EXEC.
  execAlloc_ = heap_caps_aligned_alloc(16, imageEnd, MALLOC_CAP_EXEC);
  if (execAlloc_ == nullptr) {
    LOG_ERR("DYNAPP", "no EXEC heap for %u bytes", static_cast<unsigned>(imageEnd));
    return Error::OutOfMemory;
  }
  const uintptr_t execAddr = reinterpret_cast<uintptr_t>(execAlloc_);
  if (execAddr < SOC_DIRAM_IRAM_LOW || execAddr + imageEnd > SOC_DIRAM_IRAM_HIGH) {
    // EXEC allocations come back in the I window; anything else means the
    // heap config changed under us. Bail rather than fault on a copy.
    LOG_ERR("DYNAPP", "EXEC alloc outside I window: %p", execAlloc_);
    unload();
    return Error::OutOfMemory;
  }
  blockD_ = reinterpret_cast<uint8_t*>(MAP_IRAM_TO_DRAM(execAddr));
  imageBytes_ = imageEnd;

  // --- Copy segments (through the writable D alias), zero .bss ----------
  memset(blockD_, 0, imageEnd);
  for (int i = 0; i < ehdr.phnum; ++i) {
    const Elf32Phdr& p = phdrs[i];
    if (p.type != kPtLoad || p.filesz == 0) continue;
    if (!file.seekSet(p.offset) || file.read(blockD_ + vaddrToPhys(p.vaddr), p.filesz) != static_cast<int>(p.filesz)) {
      unload();
      return Error::BadElf;
    }
  }

  // --- Relocations: every RELA section, RELATIVE-only -------------------
  // Section headers are read one at a time (48 max) to keep the stack lean.
  for (int s = 0; s < ehdr.shnum; ++s) {
    Elf32Shdr shdr;
    if (!file.seekSet(ehdr.shoff + static_cast<uint32_t>(s) * sizeof(Elf32Shdr)) ||
        file.read(&shdr, sizeof(shdr)) != static_cast<int>(sizeof(shdr))) {
      unload();
      return Error::BadElf;
    }
    if (shdr.type != kShtRela || shdr.size == 0) continue;
    const uint32_t count = shdr.size / sizeof(Elf32Rela);
    if (!file.seekSet(shdr.offset)) {
      unload();
      return Error::BadElf;
    }
    // Batched reads: 32 entries = 384B stack scratch per iteration.
    Elf32Rela batch[32];
    uint32_t done = 0;
    while (done < count) {
      const uint32_t n = count - done < 32 ? count - done : 32;
      if (file.read(batch, n * sizeof(Elf32Rela)) != static_cast<int>(n * sizeof(Elf32Rela))) {
        unload();
        return Error::BadElf;
      }
      for (uint32_t r = 0; r < n; ++r) {
        const uint32_t type = batch[r].info & 0xFF;
        if (type == kRRiscvNone) continue;
        if (type != kRRiscvRelative) {
          LOG_ERR("DYNAPP", "unsupported reloc type %u", static_cast<unsigned>(type));
          unload();
          return Error::BadReloc;
        }
        const uint32_t where = vaddrToPhys(batch[r].offset);
        const uint32_t what = static_cast<uint32_t>(batch[r].addend);
        if (where + 4 > imageEnd || vaddrToPhys(what) >= imageEnd) {
          unload();
          return Error::BadReloc;
        }
        // Unified rule (see header comment): runtime value = blockD + A.
        // Text addends land in the I window automatically because
        // blockD + kTextVBase is the block's I alias.
        uint32_t value = reinterpret_cast<uint32_t>(blockD_) + what;
        memcpy(blockD_ + where, &value, sizeof(value));
      }
      done += n;
    }
  }

  // Order matters: writes went through the D alias, execution fetches via
  // the I alias — flush the fetch pipeline before jumping into the image.
#if __riscv
  asm volatile("fence.i" ::: "memory");
#endif

  const auto entry = reinterpret_cast<CpAppEntryFn>(blockD_ + ehdr.entry);
  const CpApp* app = entry();
  if (app == nullptr) {
    unload();
    return Error::BadEntry;
  }
  // The struct itself must live inside the loaded image (D domain).
  const auto appAddr = reinterpret_cast<uintptr_t>(app);
  const auto base = reinterpret_cast<uintptr_t>(blockD_);
  if (appAddr < base || appAddr + sizeof(CpApp) > base + imageEnd) {
    unload();
    return Error::BadEntry;
  }
  if (app->abi_version != CP_ABI_VERSION || app->api_min > sizeof(CpApi)) {
    LOG_ERR("DYNAPP", "abi %u api_min %u unsupported", static_cast<unsigned>(app->abi_version),
            static_cast<unsigned>(app->api_min));
    unload();
    return Error::AbiMismatch;
  }
  if (app->on_loop == nullptr || app->on_render == nullptr) {
    unload();
    return Error::BadEntry;
  }

  app_ = app;
  LOG_INF("DYNAPP", "loaded %s: %u bytes @%p", eappPath.c_str(), static_cast<unsigned>(imageEnd), blockD_);
  return Error::None;
}

void DynAppLoader::unload() {
  app_ = nullptr;
  if (execAlloc_ != nullptr) {
    heap_caps_free(execAlloc_);
    execAlloc_ = nullptr;
  }
  blockD_ = nullptr;
  imageBytes_ = 0;
}

#endif  // !SIMULATOR
