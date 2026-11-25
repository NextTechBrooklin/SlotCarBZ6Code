#!/usr/bin/env python3
"""Embed the PIC32CX-BZ6 boot metadata header into the firmware ELF.

The BZ6 boot ROM expects a 512-byte metadata header at 0x01000000, directly in
front of the application image at 0x01000200. The header layout is the one used
by Zephyr's soc/microchip/pic32c/pic32cx_bz/bz6x/gen_signed_hex.py. It is
added to the ELF as an extra section plus a matching PT_LOAD program header.
MPLAB's programmer loads from the program headers, so both are needed for
programming/debugging the ELF (and for the HEX generated from it) to include
the header.

Usage: add_bz6_metadata.py <objcopy> <elf> [seq_num_hex] [auth_mthd] [fw_rev_hex]
"""
import os
import struct
import subprocess
import sys
import tempfile

METADATA_ADDR = 0x01000000
METADATA_LENGTH = 512
IMAGE_ADDR = 0x01000200
IMAGE_LIMIT = 0x01200000
SECTION_NAME = ".mchp_metadata"
PT_LOAD = 1


def read_elf(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise SystemExit("error: %s is not a little-endian ELF32 file" % path)
    e_phoff, e_shoff = struct.unpack_from("<II", data, 0x1C)
    e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(
        "<HHHHH", data, 0x2A)

    segments = []
    for i in range(e_phnum):
        p_type, p_off, p_vaddr, p_paddr, p_filesz = struct.unpack_from(
            "<IIIII", data, e_phoff + i * e_phentsize)
        segments.append((p_type, p_off, p_paddr, p_filesz))

    sections = {}
    if e_shoff and e_shnum:
        strtab_hdr = e_shoff + e_shstrndx * e_shentsize
        str_off = struct.unpack_from("<I", data, strtab_hdr + 0x10)[0]
        for i in range(e_shnum):
            hdr = e_shoff + i * e_shentsize
            name_idx, _, _, sh_addr, sh_off, sh_size = struct.unpack_from(
                "<IIIIII", data, hdr)
            end = data.index(b"\0", str_off + name_idx)
            sections[data[str_off + name_idx:end].decode()] = (sh_addr, sh_off, sh_size)
    return data, segments, sections


def image_length(segments):
    end = IMAGE_ADDR
    for p_type, _, paddr, filesz in segments:
        if p_type == PT_LOAD and filesz and IMAGE_ADDR <= paddr < IMAGE_LIMIT:
            end = max(end, paddr + filesz)
    if end == IMAGE_ADDR:
        raise SystemExit("error: no application image found at 0x%08X" % IMAGE_ADDR)
    return end - IMAGE_ADDR


def build_metadata(seq_num, auth_mthd, fw_rev, length):
    md = bytearray(METADATA_LENGTH)
    md[24:28] = b"PHCM"
    md[60:64] = seq_num.to_bytes(4, "little")
    md[64] = 0x03
    md[65] = 0x01
    md[66] = auth_mthd & 0xFF
    md[70] = 0x74
    md[72:76] = fw_rev.to_bytes(4, "little")
    md[76:80] = IMAGE_ADDR.to_bytes(4, "little")
    md[80:84] = IMAGE_ADDR.to_bytes(4, "little")
    md[84:88] = struct.pack("<I", length)
    return bytes(md)


def run(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit("error: %s failed" % os.path.basename(cmd[0]))


def add_load_segment(path):
    """Append a PT_LOAD program header for the metadata section.

    The program header table is rewritten at the end of the file (with the new
    entry inserted in address order) because there is no room after the
    original table.
    """
    data, _, sections = read_elf(path)
    addr, off, size = sections[SECTION_NAME]
    e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x2A)
    if e_phentsize != 32:
        raise SystemExit("error: unexpected program header size %d" % e_phentsize)

    entries = [data[e_phoff + i * 32:e_phoff + (i + 1) * 32] for i in range(e_phnum)]
    new_entry = struct.pack("<8I", PT_LOAD, off, addr, addr, size, size, 4, 4)
    index = len(entries)
    for i, entry in enumerate(entries):
        p_type, _, p_vaddr = struct.unpack_from("<III", entry)
        if p_type == PT_LOAD and p_vaddr > addr:
            index = i
            break
    entries.insert(index, new_entry)

    out = bytearray(data)
    out += b"\0" * (-len(out) % 4)
    new_phoff = len(out)
    out += b"".join(entries)
    struct.pack_into("<I", out, 0x1C, new_phoff)
    struct.pack_into("<H", out, 0x2C, len(entries))
    with open(path, "wb") as f:
        f.write(out)


def metadata_state(path):
    """Return (section_bytes_or_None, has_load_segment)."""
    data, segments, sections = read_elf(path)
    if SECTION_NAME not in sections:
        return None, False
    addr, off, size = sections[SECTION_NAME]
    has_segment = any(p_type == PT_LOAD and paddr == addr and filesz == size
                      for p_type, _, paddr, filesz in segments)
    return data[off:off + size], has_segment


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    objcopy, elf = argv[1], argv[2]
    seq_num = int(argv[3], 16) if len(argv) > 3 else 0xFFFFFFFE
    auth_mthd = int(argv[4]) if len(argv) > 4 else 0
    fw_rev = int(argv[5], 16) if len(argv) > 5 else 0x01000000

    _, segments, _ = read_elf(elf)
    length = image_length(segments)
    metadata = build_metadata(seq_num, auth_mthd, fw_rev, length)

    existing, has_segment = metadata_state(elf)
    if existing is not None:
        if existing != metadata or not has_segment:
            raise SystemExit("error: %s already has different metadata; relink it" % elf)
    else:
        with tempfile.TemporaryDirectory() as tmp:
            md_file = os.path.join(tmp, "metadata.bin")
            with open(md_file, "wb") as f:
                f.write(metadata)
            run([objcopy,
                 "--add-section", "%s=%s" % (SECTION_NAME, md_file),
                 "--set-section-flags", "%s=alloc,load,readonly,data" % SECTION_NAME,
                 "--change-section-address", "%s=0x%08X" % (SECTION_NAME, METADATA_ADDR),
                 elf])
        add_load_segment(elf)

    embedded, has_segment = metadata_state(elf)
    if embedded != metadata or not has_segment:
        raise SystemExit("error: metadata missing or not loadable in %s" % elf)
    print("BZ6 metadata embedded at 0x%08X: image 0x%08X, length %d bytes, "
          "seq 0x%08X, auth %d, rev 0x%08X"
          % (METADATA_ADDR, IMAGE_ADDR, length, seq_num, auth_mthd, fw_rev))


if __name__ == "__main__":
    main(sys.argv)
