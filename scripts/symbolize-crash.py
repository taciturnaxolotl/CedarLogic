#!/usr/bin/env python3
"""Turn a Windows crash report back into function names.

A crash report from the field carries lines like `CedarLogic.exe +0x70b171`.
The app tries to resolve those itself against the .pdb shipped beside it, but
when that fails -- and in every 3.2.1 report so far it has -- the offsets arrive
raw, and reading one means disassembling the shipped binary by hand. This does
the same lookup offline, against the .pdb published with the release:

    gh issue view 120 --json body -q .body | python3 scripts/symbolize-crash.py
    python3 scripts/symbolize-crash.py crashtrace.log
    python3 scripts/symbolize-crash.py --pdb build/CedarLogic.pdb trace.txt

With no --pdb it reads the version out of the report ("CedarLogic 3.2.1
(32-bit) on ...") and fetches that release's symbols with gh, caching them under
~/.cache/cedarlogic-symbols. Releases from before v3.1.1 published no .pdb at
all, so those reports cannot be resolved by anything, including this.

Resolves to the nearest exported function and an offset into it, which is what
the public symbols carry. Not file:line -- that lives in the per-module debug
streams and is a great deal more work to read for a good deal less of the
answer. Stdlib only, so it runs anywhere without a toolchain.
"""

import argparse
import bisect
import os
import re
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

CACHE = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "cedarlogic-symbols"

# Stream 1 is the PDB info stream, 3 is DBI. Both are fixed by the format.
PDB_STREAM_DBI = 3
# Index into the DBI optional debug header of the copied PE section table.
DBG_HEADER_SECTION_HDR = 5
S_PUB32 = 0x110E


class Pdb:
    """Just enough MSF to read the streams this needs.

    A .pdb is a block-allocated container: a directory lists every stream's
    size and the blocks holding it, and the directory itself is reached through
    a block map in the header.
    """

    def __init__(self, path):
        self.f = open(path, "rb")
        head = self.f.read(0x40)
        if not head.startswith(b"Microsoft C/C++ MSF 7.00"):
            raise ValueError("%s is not an MSF 7.0 .pdb" % path)
        self.block_size = struct.unpack_from("<I", head, 0x20)[0]
        dir_bytes = struct.unpack_from("<I", head, 0x2C)[0]
        block_map = struct.unpack_from("<I", head, 0x34)[0]

        n = (dir_bytes + self.block_size - 1) // self.block_size
        blocks = struct.unpack_from("<%dI" % n, self._block(block_map), 0)
        directory = b"".join(self._block(b) for b in blocks)[:dir_bytes]

        count = struct.unpack_from("<I", directory, 0)[0]
        self.sizes = struct.unpack_from("<%dI" % count, directory, 4)
        self.blocks = []
        off = 4 + 4 * count
        for size in self.sizes:
            # 0xffffffff marks a stream that was deleted, not an empty one.
            n = 0 if size == 0xFFFFFFFF else (size + self.block_size - 1) // self.block_size
            self.blocks.append(struct.unpack_from("<%dI" % n, directory, off))
            off += 4 * n

    def _block(self, n):
        self.f.seek(n * self.block_size)
        return self.f.read(self.block_size)

    def stream(self, i):
        if i >= len(self.sizes) or self.sizes[i] == 0xFFFFFFFF:
            return b""
        return b"".join(self._block(b) for b in self.blocks[i])[:self.sizes[i]]


def section_rvas(pdb, dbi):
    """Virtual addresses of each PE section, from the copy the linker left here.

    Public symbols are recorded as a section number and an offset into it, so
    turning one into the RVA a crash report quotes needs the section table. The
    .pdb carries its own copy, which saves having to find the matching .exe.
    """
    sizes = struct.unpack_from("<6i", dbi, 24)  # ModInfo..TypeServerMap, then EC
    ec_size = struct.unpack_from("<i", dbi, 52)[0]
    opt_off = 64 + sum(sizes[:5]) + ec_size
    opt_size = struct.unpack_from("<i", dbi, 48)[0]
    indices = struct.unpack_from("<%dH" % (opt_size // 2), dbi, opt_off)
    if len(indices) <= DBG_HEADER_SECTION_HDR:
        raise ValueError("no section header stream in this .pdb")
    headers = pdb.stream(indices[DBG_HEADER_SECTION_HDR])
    return [struct.unpack_from("<I", headers, o + 12)[0]
            for o in range(0, len(headers) - 39, 40)]


def public_symbols(pdb):
    """Every exported function as (rva, mangled name), sorted by address."""
    dbi = pdb.stream(PDB_STREAM_DBI)
    if len(dbi) < 64:
        raise ValueError("this .pdb has no DBI stream; it may be a stripped one")
    rvas = section_rvas(pdb, dbi)
    records = pdb.stream(struct.unpack_from("<H", dbi, 20)[0])

    out = []
    p = 0
    while p + 4 <= len(records):
        length, kind = struct.unpack_from("<HH", records, p)
        if length < 2:
            break
        if kind == S_PUB32 and p + 14 <= len(records):
            _flags, offset, section = struct.unpack_from("<IIH", records, p + 4)
            if 1 <= section <= len(rvas):
                name = records[p + 14:p + 2 + length].split(b"\0")[0]
                out.append((rvas[section - 1] + offset, name.decode("ascii", "replace")))
        p += length + 2
    out.sort()
    return out


# The MSVC special names worth spelling out. Anything else keeps its mangling
# rather than being half-decoded into something that reads like a symbol but is
# not one -- `??1guiWire@@UAE@XZ` becoming `?1guiWire` helps nobody.
SPECIAL = {"0": "{cls}::{cls}", "1": "{cls}::~{cls}"}


def pretty(name):
    """Make an MSVC mangled name readable enough to follow a stack.

    Decoding parameter types too would buy very little: the scope and the name
    identify a frame. Templates keep their name and lose their arguments.
    """
    if not name.startswith("?"):
        return name
    if name.startswith("??$"):  # template; its scope is not worth unpicking
        return name[3:].split("@")[0] + "<...>"
    if name.startswith("??"):
        form = SPECIAL.get(name[2:3])
        scope = [p for p in name[3:].split("@@")[0].split("@") if p]
        return form.format(cls=scope[0]) if form and scope else name
    scope = [p for p in name[1:].split("@@")[0].split("@") if p]
    return "::".join(reversed(scope)) if scope else name


def resolve(symbols, addrs, rva):
    i = bisect.bisect_right(addrs, rva) - 1
    if i < 0:
        return None
    base, name = symbols[i]
    return name, rva - base


def report_version(text):
    m = re.search(r"CedarLogic\s+(\d+\.\d+\.\d+)", text)
    return m.group(1) if m else None


def fetch_pdb(version):
    """Download and unpack the symbols published with a release."""
    target = CACHE / version / "CedarLogic.pdb"
    if target.exists():
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    pattern = "CedarLogic-%s-win32.pdb.zip" % version
    print("fetching symbols for %s ..." % version, file=sys.stderr)
    try:
        subprocess.run(
            ["gh", "release", "download", "v" + version, "--pattern", pattern,
             "--dir", str(target.parent), "--clobber"],
            check=True, capture_output=True)
    except FileNotFoundError:
        sys.exit("gh is not installed; pass --pdb with a local copy instead")
    except subprocess.CalledProcessError as e:
        sys.exit("no symbols published for v%s (releases before v3.1.1 have "
                 "none, and nothing can resolve those reports): %s"
                 % (version, e.stderr.decode().strip()))
    with zipfile.ZipFile(target.parent / pattern) as z:
        for member in z.namelist():
            if member.endswith("CedarLogic.pdb"):
                with z.open(member) as src, open(target, "wb") as dst:
                    dst.write(src.read())
                break
        else:
            sys.exit("%s holds no CedarLogic.pdb" % pattern)
    return target


# `CedarLogic.exe +0x70b171`, with or without the surrounding "fault at" line.
FRAME = re.compile(r"(CedarLogic\.exe)\s*\+0x([0-9a-fA-F]+)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("trace", nargs="?", help="report file; defaults to stdin")
    ap.add_argument("--pdb", help="use this .pdb instead of fetching one")
    ap.add_argument("--version", help="override the version read from the report")
    ap.add_argument("--raw", action="store_true", help="keep names mangled")
    args = ap.parse_args()

    text = Path(args.trace).read_text() if args.trace else sys.stdin.read()

    if args.pdb:
        pdb_path = Path(args.pdb)
    else:
        version = args.version or report_version(text)
        if not version:
            sys.exit("no version line in the report; pass --version or --pdb")
        pdb_path = fetch_pdb(version)

    symbols = public_symbols(Pdb(pdb_path))
    addrs = [a for a, _ in symbols]
    print("%d public symbols from %s" % (len(symbols), pdb_path), file=sys.stderr)

    hits = 0
    for line in text.splitlines():
        m = FRAME.search(line)
        # The "fault at" line quotes the module's full install path, where the
        # file name is part of the path and not a frame to resolve. Its address
        # is the first stack frame anyway, so nothing is lost by leaving it.
        if m and not (m.start() and line[m.start() - 1] in "\\/"):
            found = resolve(symbols, addrs, int(m.group(2), 16))
            if found:
                name, offset = found
                hits += 1
                label = name if args.raw else pretty(name)
                line = line[:m.start()] + "%s +0x%x" % (label, offset) + line[m.end():]
        print(line)

    if not hits:
        print("\nNo CedarLogic.exe frames matched. A report with no frames at "
              "all is its own bug, not a symbol problem.", file=sys.stderr)


if __name__ == "__main__":
    main()
