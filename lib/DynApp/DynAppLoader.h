#pragma once

// Loads a CrossPoint dynamic app (.eapp — a PIC ELF32 RISC-V shared object
// laid out by sdk/dynapp/build-eapp.sh) from SD into one executable heap
// block and resolves its entry point. See docs/engineering/dynapp.md for the
// address-domain math this depends on.
//
// Not available in the simulator build (needs the C3 heap and I/D alias).

#include <cstddef>
#include <cstdint>
#include <string>

#include "crosspoint_app_abi.h"

class DynAppLoader {
 public:
  enum class Error : uint8_t {
    None = 0,
    FileOpen,     // .eapp missing or unreadable
    BadElf,       // not a little-endian ELF32 RISC-V DYN object
    BadLayout,    // segments outside the SDK's link layout
    TooLarge,     // image exceeds kMaxImageBytes
    OutOfMemory,  // executable heap block unavailable
    BadReloc,     // unsupported relocation type
    BadEntry,     // entry point missing/outside text, or CpApp invalid
    AbiMismatch,  // app built for an incompatible CP_ABI_VERSION
  };

  // Refuse images that would gouge the ~380KB heap. Covers text+rodata+data
  // +bss; the block is freed in unload(), so this is a transient cost while
  // the app runs.
  static constexpr uint32_t kMaxImageBytes = 96 * 1024;

  DynAppLoader() = default;
  ~DynAppLoader() { unload(); }
  DynAppLoader(const DynAppLoader&) = delete;
  DynAppLoader& operator=(const DynAppLoader&) = delete;

  // Loads and relocates the image, then calls cp_app_entry(). On success
  // app() returns the callback table until unload().
  Error load(const std::string& eappPath);
  void unload();

  bool loaded() const { return app_ != nullptr; }
  const CpApp* app() const { return app_; }
  uint32_t imageBytes() const { return imageBytes_; }
  static const char* errorName(Error e);

 private:
  const CpApp* app_ = nullptr;
  // D-domain base of the single EXEC allocation (heap gave the I alias;
  // freed via the original pointer). Size = imageBytes_.
  uint8_t* blockD_ = nullptr;
  void* execAlloc_ = nullptr;
  uint32_t imageBytes_ = 0;
};
