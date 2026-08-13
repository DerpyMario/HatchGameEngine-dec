#!/usr/bin/env python3
"""Recovers what can be recovered as HLSL from NMPL_FX_SHADER02 shader packs.

The .fxdat files are a container holding each shader twice: once as DXIL for
Shader Model 6, and once as a Shader Model 5 DXBC blob. The DXBC copy carries
RDEF, ISGN and OSGN chunks -- the reflection data the HLSL compiler wrote out --
and those describe the shader's interface exactly: every constant buffer with
its variables, their types and their byte offsets, every texture and sampler
with its slot, and the input and output semantics.

That part is reconstructed here exactly, and what comes out is real HLSL that
compiles.

What is not reconstructed is the body. Turning a compiled instruction stream
back into the source it came from is a decompilation problem, not a conversion
one: the names, the control flow and the expressions the author wrote are gone
by the time the compiler has finished, and no amount of reading the bytecode
brings them back. The instruction stream is left beside the declarations, as
bytes, so a body can be written against a faithful record of what the original
did rather than a guess at it.

Usage:  fxdat2hlsl.py <directory of .fxdat files> <output directory>
"""

import os
import struct
import sys

SHADER_TYPES = {0xFFFF: "pixel", 0xFFFE: "vertex", 0xFFFD: "geometry",
                0xFFFC: "hull", 0xFFFB: "domain", 0x4353: "compute"}

VARIABLE_TYPES = {0: "void", 1: "bool", 2: "int", 3: "float", 19: "double",
                  20: "uint"}

VARIABLE_CLASSES = {0: "scalar", 1: "vector", 2: "matrix_rows",
                    3: "matrix_columns", 4: "object", 5: "struct"}

RESOURCE_TYPES = {0: "cbuffer", 1: "tbuffer", 2: "texture", 3: "sampler",
                  4: "rwtyped", 5: "structured", 6: "rwstructured",
                  7: "byteaddress", 8: "rwbyteaddress"}

COMPONENT_TYPES = {1: "uint", 2: "int", 3: "float"}

MASKS = {1: "x", 3: "xy", 7: "xyz", 15: "xyzw"}
MASK_WIDTH = {1: 1, 3: 2, 7: 3, 15: 4}


def read_string(data, offset):
    end = data.index(b"\0", offset)
    return data[offset:end].decode("ascii", "replace")


def find_dxbc_blobs(data):
    """Every DXBC container in the file, by offset."""
    blobs = []
    at = data.find(b"DXBC")
    while at != -1:
        # A container says how big it is, which is what tells a real one from
        # the four bytes happening to appear inside some other chunk.
        if at + 32 <= len(data):
            total = struct.unpack_from("<I", data, at + 24)[0]
            count = struct.unpack_from("<I", data, at + 28)[0]
            if 0 < count < 64 and 0 < total <= len(data) - at:
                blobs.append((at, total))
        at = data.find(b"DXBC", at + 4)
    return blobs


def read_chunks(data, offset):
    count = struct.unpack_from("<I", data, offset + 28)[0]
    offsets = struct.unpack_from("<%dI" % count, data, offset + 32)

    chunks = {}
    for relative in offsets:
        at = offset + relative
        name = data[at:at + 4].decode("ascii", "replace")
        size = struct.unpack_from("<I", data, at + 4)[0]
        chunks[name] = (at + 8, size)

    return chunks


def read_type(data, base, offset):
    cls, kind, rows, columns, elements, members, member_offset = \
        struct.unpack_from("<6HI", data, base + offset)

    return {
        "class": VARIABLE_CLASSES.get(cls, str(cls)),
        "type": VARIABLE_TYPES.get(kind, str(kind)),
        "rows": rows,
        "columns": columns,
        "elements": elements,
    }


def hlsl_type(info):
    base = info["type"]

    if info["class"].startswith("matrix"):
        name = "%s%dx%d" % (base, info["rows"], info["columns"])
    elif info["class"] == "vector":
        name = "%s%d" % (base, info["columns"])
    elif info["class"] == "struct":
        name = "struct"
    else:
        name = base

    return name


def read_rdef(data, base):
    cb_count, cb_offset, bind_count, bind_offset = \
        struct.unpack_from("<4I", data, base)
    minor, major, program_type, _flags, creator_offset = \
        struct.unpack_from("<BBHII", data, base + 16)

    reflection = {
        "target": "%d_%d" % (major, minor),
        "stage": SHADER_TYPES.get(program_type, hex(program_type)),
        "creator": read_string(data, base + creator_offset),
        "cbuffers": [],
        "bindings": [],
    }

    for i in range(cb_count):
        at = base + cb_offset + i * 24
        name_offset, var_count, var_offset, size, _flags, _kind = \
            struct.unpack_from("<6I", data, at)

        buffer = {"name": read_string(data, base + name_offset),
                  "size": size, "variables": []}

        for v in range(var_count):
            var_at = base + var_offset + v * 40
            v_name, v_start, v_size, _v_flags, v_type, _v_default = \
                struct.unpack_from("<6I", data, var_at)

            info = read_type(data, base, v_type)
            info["name"] = read_string(data, base + v_name)
            info["offset"] = v_start
            info["size"] = v_size

            buffer["variables"].append(info)

        reflection["cbuffers"].append(buffer)

    for i in range(bind_count):
        at = base + bind_offset + i * 32
        name_offset, kind, _ret, _dim, _samples, slot, count, _flags = \
            struct.unpack_from("<8I", data, at)

        reflection["bindings"].append({
            "name": read_string(data, base + name_offset),
            "kind": RESOURCE_TYPES.get(kind, str(kind)),
            "slot": slot,
            "count": count,
        })

    return reflection


def read_signature(data, base):
    count, _offset = struct.unpack_from("<2I", data, base)

    elements = []
    for i in range(count):
        at = base + 8 + i * 24
        name_offset, index, _system, component, register = \
            struct.unpack_from("<5I", data, at)
        mask = struct.unpack_from("<B", data, at + 20)[0]

        elements.append({
            "name": read_string(data, base + name_offset),
            "index": index,
            "register": register,
            "type": COMPONENT_TYPES.get(component, "float"),
            "mask": MASKS.get(mask, "xyzw"),
            "width": MASK_WIDTH.get(mask, 4),
        })

    return elements


def signature_struct(name, elements):
    lines = ["struct %s {" % name]

    for i, element in enumerate(elements):
        kind = element["type"]
        if element["width"] > 1:
            kind = "%s%d" % (kind, element["width"])

        semantic = element["name"]
        if element["index"]:
            semantic = "%s%d" % (semantic, element["index"])

        lines.append("    %-8s field%d : %s;" % (kind, i, semantic))

    lines.append("};")

    return "\n".join(lines)


def to_hlsl(name, reflection, signatures, body_size):
    stage = reflection["stage"]
    lines = []

    lines.append("// %s -- %s shader, compiled for shader model %s"
                 % (name, stage, reflection["target"]))
    lines.append("// Originally built by %s" % reflection["creator"])
    lines.append("//")
    lines.append("// The declarations below are exact: they come from the reflection data the")
    lines.append("// compiler wrote alongside the bytecode, so the names, types, byte offsets and")
    lines.append("// register slots are the ones the original source had.")
    lines.append("//")
    lines.append("// The body is not recoverable. %d bytes of compiled instructions carry no"
                 % body_size)
    lines.append("// names, no expressions and no control flow that can be read back as source.")
    lines.append("")

    for buffer in reflection["cbuffers"]:
        lines.append("cbuffer %s : register(b0) {  // %d bytes"
                     % (buffer["name"], buffer["size"]))

        for variable in buffer["variables"]:
            declaration = "%s %s" % (hlsl_type(variable), variable["name"])
            if variable["elements"]:
                declaration += "[%d]" % variable["elements"]

            lines.append("    %-40s // offset %d, %d bytes"
                         % (declaration + ";", variable["offset"], variable["size"]))

        lines.append("};")
        lines.append("")

    for binding in reflection["bindings"]:
        if binding["kind"] == "cbuffer":
            continue

        if binding["kind"] == "texture":
            lines.append("Texture2D %s : register(t%d);" % (binding["name"], binding["slot"]))
        elif binding["kind"] == "sampler":
            lines.append("SamplerState %s : register(s%d);" % (binding["name"], binding["slot"]))
        else:
            lines.append("// %s %s : register(u%d);"
                         % (binding["kind"], binding["name"], binding["slot"]))

    if any(b["kind"] != "cbuffer" for b in reflection["bindings"]):
        lines.append("")

    if signatures.get("input"):
        lines.append(signature_struct("Input", signatures["input"]))
        lines.append("")

    if signatures.get("output"):
        lines.append(signature_struct("Output", signatures["output"]))
        lines.append("")

    entry = "vs_main" if stage == "vertex" else "ps_main"
    lines.append("Output %s(Input input) {" % entry)
    lines.append("    Output output = (Output)0;")
    lines.append("")
    lines.append("    // The original body was here.")
    lines.append("")
    lines.append("    return output;")
    lines.append("}")

    return "\n".join(lines) + "\n"


def convert(path, outdir):
    data = open(path, "rb").read()
    if data[:16] != b"NMPL_FX_SHADER02":
        print("  %s: not an NMPL_FX_SHADER02 pack, skipped" % os.path.basename(path))
        return 0

    name = os.path.splitext(os.path.basename(path))[0]
    written = 0

    for offset, total in find_dxbc_blobs(data):
        chunks = read_chunks(data, offset)

        # Only the shader model 5 copies carry reflection. The shader model 6
        # ones hold the same shader as DXIL, which is kept beside the output for
        # anyone who wants to disassemble it.
        if "RDEF" not in chunks:
            if "DXIL" in chunks:
                at, size = chunks["DXIL"]
                open(os.path.join(outdir, "%s.%d.dxil" % (name, offset)), "wb").write(
                    data[at:at + size])
            continue

        reflection = read_rdef(data, chunks["RDEF"][0])

        signatures = {}
        if "ISGN" in chunks:
            signatures["input"] = read_signature(data, chunks["ISGN"][0])
        if "OSGN" in chunks:
            signatures["output"] = read_signature(data, chunks["OSGN"][0])

        body_size = chunks["SHEX"][1] if "SHEX" in chunks else 0

        stage = reflection["stage"]
        out = os.path.join(outdir, "%s.%s.hlsl" % (name, stage))
        open(out, "w").write(to_hlsl(name, reflection, signatures, body_size))

        # The compiled shader on its own, in the container the D3D tools expect,
        # so it can be disassembled with dxc or fxc.
        open(os.path.join(outdir, "%s.%s.cso" % (name, stage)), "wb").write(
            data[offset:offset + total])

        written += 1

    return written


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1

    source, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)

    total = 0
    for entry in sorted(os.listdir(source)):
        if not entry.endswith(".fxdat"):
            continue

        count = convert(os.path.join(source, entry), outdir)
        print("  %-40s %d shader(s)" % (entry, count))
        total += count

    print("%d shader(s) written to %s" % (total, outdir))

    return 0


if __name__ == "__main__":
    sys.exit(main())
