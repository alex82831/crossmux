#!/usr/bin/env python3
"""Structural verifier for CrossPoint .eapp images.

Re-implements every acceptance check DynAppLoader performs on device (plus
the layout invariant the loader relies on), so a bad build fails here rather
than as a LoadFault on hardware. Exits nonzero with a reason on the first
violation.
"""

import struct
import sys

TEXT_VBASE = 0x700000
MAX_IMAGE = 96 * 1024
R_RISCV_RELATIVE = 3
R_RISCV_NONE = 0


def fail(msg: str) -> None:
    print(f"verify-eapp: FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def v2p(vaddr: int) -> int:
    return vaddr - TEXT_VBASE if vaddr >= TEXT_VBASE else vaddr


def main(path: str) -> None:
    with open(path, "rb") as f:
        blob = f.read()

    if len(blob) < 52 or blob[:4] != b"\x7fELF":
        fail("not an ELF")
    if blob[4] != 1 or blob[5] != 1:
        fail("not ELF32 little-endian")
    (etype, machine, _ver, entry, phoff, shoff, _flags, _ehsize, phentsize,
     phnum, shentsize, shnum, _shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", blob, 16)
    if etype != 3:
        fail("not ET_DYN")
    if machine != 243:
        fail("not EM_RISCV")
    if phentsize != 32 or shentsize != 40:
        fail("unexpected header entry sizes")
    if not (0 < phnum <= 8):
        fail(f"phnum {phnum} outside loader bound (1..8)")
    if shnum > 48:
        fail(f"shnum {shnum} outside loader bound (<=48)")

    # Program headers: layout invariant + image size.
    segs = []
    image_end = 0
    saw_text = False
    for i in range(phnum):
        ptype, off, vaddr, _pa, filesz, memsz, flags, _align = struct.unpack_from(
            "<IIIIIIII", blob, phoff + i * 32)
        if ptype != 1 or memsz == 0:  # PT_LOAD
            continue
        if filesz > memsz:
            fail(f"segment {i}: filesz > memsz")
        is_text = vaddr >= TEXT_VBASE
        saw_text = saw_text or is_text
        if not is_text and vaddr + memsz > TEXT_VBASE:
            fail(f"segment {i}: data crosses the text window base")
        if is_text and not flags & 1:
            fail(f"segment {i}: text segment not executable")
        if not is_text and flags & 1:
            fail(f"segment {i}: data segment marked executable")
        phys = v2p(vaddr)
        segs.append((phys, phys + memsz, is_text))
        image_end = max(image_end, phys + memsz)
    if not saw_text:
        fail("no text segment at TEXT_VBASE")
    if image_end > MAX_IMAGE:
        fail(f"image {image_end} bytes exceeds {MAX_IMAGE}")

    # Physical overlap check (loader loads all segments into one block).
    segs.sort()
    for (a_start, a_end, _), (b_start, _b_end, _) in zip(segs, segs[1:]):
        if b_start < a_end:
            fail(f"segments overlap physically at {a_start:#x}..{a_end:#x} vs {b_start:#x}")

    # Layout invariant that makes auipc data access work: for data,
    # vaddr == phys; for text, vaddr == phys + TEXT_VBASE. v2p() guarantees
    # this by construction, so just confirm data sits below text physically.
    text_phys_start = min(s for s, _e, is_t in segs if is_t)
    if text_phys_start != 0:
        fail(f"text does not start at phys 0 (got {text_phys_start:#x})")

    # Entry inside text.
    if entry < TEXT_VBASE or v2p(entry) >= image_end:
        fail(f"entry {entry:#x} outside text")

    # Relocations: RELATIVE only, targets and values inside the image.
    n_rel = 0
    for i in range(shnum):
        (_name, stype, _sflags, _addr, off, size, _link, _info, _align,
         entsize) = struct.unpack_from("<IIIIIIIIII", blob, shoff + i * 40)
        if stype != 4 or size == 0:  # SHT_RELA
            continue
        if entsize not in (0, 12):
            fail("unexpected RELA entsize")
        for r in range(size // 12):
            r_off, r_info, r_addend = struct.unpack_from("<IIi", blob, off + r * 12)
            rtype = r_info & 0xFF
            if rtype == R_RISCV_NONE:
                continue
            if rtype != R_RISCV_RELATIVE:
                fail(f"unsupported relocation type {rtype}")
            if v2p(r_off) + 4 > image_end:
                fail(f"reloc target {r_off:#x} outside image")
            if v2p(r_addend & 0xFFFFFFFF) >= image_end:
                fail(f"reloc value {r_addend:#x} outside image")
            n_rel += 1

    # No undefined dynamic symbols (the app must import nothing).
    for i in range(shnum):
        (_name, stype, _sflags, _addr, off, size, link, _info, _align,
         entsize) = struct.unpack_from("<IIIIIIIIII", blob, shoff + i * 40)
        if stype != 11 or entsize != 16:  # SHT_DYNSYM
            continue
        for s in range(1, size // 16):
            _nm, value, _sz, info, _other, shndx = struct.unpack_from(
                "<IIIBBH", blob, off + s * 16)
            if shndx == 0 and info & 0xF != 0:  # SHN_UNDEF, not STT_NOTYPE
                fail("undefined dynamic symbol present")

    print(f"verify-eapp: OK ({image_end} bytes in RAM, {n_rel} relocations)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: verify-eapp.py <file.eapp>", file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1])
