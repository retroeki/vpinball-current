#!/usr/bin/env python3
"""
Extract every scriptable name from a folder of .vpx tables: the table itself,
each game item with its script type, and each collection with its members.
vbs_check --items reads these so the stand-in table answers exactly the names
the device would, with the right TypeName.

VPX tables are OLE2 compound files. GameStg/GameData holds the table's NAME;
each GameStg/GameItem<N> starts with a 4-byte item type and then BIFF records
([int32 size][4-byte tag][payload], size counting the tag); each
GameStg/Collection<N> has a NAME and one ITEM per member.

Usage:
    python tests/scriptpatcher/extract_vpx_items.py <vpx_dir> <out_dir>

Writes <out_dir>/<table>.items.tsv:
    <name>\tTable
    <name>\t<TypeName>\t<Key>=<value>;...  e.g. Light, TimerInterval=12;Material=Plastic
    <name>\tCollection\t<member>,<member>
The properties are the ones scripts use as data: TimerInterval (core.vbs
vpmMapLights indexes lamps by it), Color, Material and Image.
Requires `olefile` (pip install olefile).
"""
import glob
import os
import struct
import sys

import olefile

# ItemTypeEnum order (src/core/iselect.h) -> TypeName the device reports
# (the GetDocumentation names in standalone/vpinball_standalone_i_proxy.cpp).
TYPE_NAMES = [
    "Wall", "Flipper", "Timer", "Plunger", "Textbox", "Bumper", "Trigger", "Light", "Kicker", "Decal",
    "Gate", "Spinner", "Ramp", "Table", "LightCenter", "ControlPoint", "Collection", "DispReel", "LightSeq",
    "Primitive", "Flasher", "Rubber", "HitTarget", "IBall", "PartGroup",
]


def records(data, start):
    p = start
    while p + 8 <= len(data):
        size = struct.unpack_from("<i", data, p)[0]
        tag = data[p + 4:p + 8]
        if tag == b"ENDB" or size < 4:
            return
        if tag == b"CODE":  # tag-only record, the text carries its own length
            slen = struct.unpack_from("<I", data, p + 8)[0]
            p += 12 + slen
            continue
        yield tag, data[p + 8:p + 4 + size]
        p += 4 + size


def wide(payload):
    n = struct.unpack_from("<i", payload, 0)[0]
    return payload[4:4 + n].decode("utf-16-le", "replace")


def narrow(payload):
    n = struct.unpack_from("<i", payload, 0)[0]
    return payload[4:4 + n].decode("latin-1")


def first_name(data, start):
    for tag, payload in records(data, start):
        if tag == b"NAME":
            return wide(payload)
    return None


def item_record(data):
    """Name and script-visible data properties of one GameItem stream."""
    name, props = None, []
    for tag, payload in records(data, 4):
        if tag == b"NAME" and name is None:
            name = wide(payload)
        elif tag == b"TMIN" and len(payload) >= 4:
            props.append(f"TimerInterval={struct.unpack_from('<i', payload, 0)[0]}")
        elif tag == b"COLR" and len(payload) >= 4:
            props.append(f"Color={struct.unpack_from('<i', payload, 0)[0]}")
        elif tag in (b"MATR", b"IMAG") and len(payload) >= 4:
            value = narrow(payload)
            if ";" not in value and "\t" not in value:
                props.append(("Material=" if tag == b"MATR" else "Image=") + value)
    return name, ";".join(props)


def extract_items(path):
    ole = olefile.OleFileIO(path)
    try:
        streams = [s for s in ole.listdir() if len(s) == 2 and s[0] == "GameStg"]
        lines = []
        for s in streams:
            if s[1] == "GameData":
                name = first_name(ole.openstream(s).read(), 0)
                if name:
                    lines.append(f"{name}\tTable")
        for s in streams:
            if s[1].startswith("GameItem"):
                data = ole.openstream(s).read()
                if len(data) < 4:
                    continue
                kind = struct.unpack_from("<i", data, 0)[0]
                name, props = item_record(data)
                if name and 0 <= kind < len(TYPE_NAMES):
                    lines.append(f"{name}\t{TYPE_NAMES[kind]}\t{props}")
        for s in streams:
            if s[1].startswith("Collection"):
                data = ole.openstream(s).read()
                name, members = None, []
                for tag, payload in records(data, 0):
                    if tag == b"NAME":
                        name = wide(payload)
                    elif tag == b"ITEM":
                        members.append(wide(payload))
                if name:
                    lines.append(f"{name}\tCollection\t{','.join(members)}")
        return lines
    finally:
        ole.close()


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    files = sorted(glob.glob(os.path.join(src, "*.vpx")))
    ok, fail = 0, []
    for f in files:
        base = os.path.splitext(os.path.basename(f))[0]
        try:
            lines = extract_items(f)
        except Exception as exc:  # noqa: BLE001 - report and continue
            fail.append((base, f"error: {exc}"))
            continue
        with open(os.path.join(out, base + ".items.tsv"), "w", encoding="utf-8", newline="\n") as o:
            o.write("\n".join(lines) + "\n")
        ok += 1
    print(f"Extracted items for {ok}/{len(files)} tables to {out}")
    for base, why in fail:
        print(f"  SKIP {base}: {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
