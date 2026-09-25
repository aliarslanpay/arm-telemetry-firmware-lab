#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check linked images for unresolved symbols and selected runtime artifacts."""
import os
import pathlib
import re
import subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
cross = os.environ.get("CROSS", "arm-none-eabi-")
for name in ("baremetal", "freertos", "inversion"):
    image = ROOT / "build" / f"{name}.elf"
    symbols = subprocess.check_output([cross + "nm", str(image)], text=True)
    assert not subprocess.check_output([cross + "nm", "-u", str(image)], text=True).strip(), "unresolved symbols"
    forbidden = re.compile(r"(?:malloc|calloc|realloc|free|pvPortMalloc|vPortFree|_Z[nd]\w*|__cxa\w*|__gxx_personality\w*|_Unwind_\w*|__aeabi_unwind_\w*|_GLOBAL__sub_I_\w*)")
    assert not any(forbidden.fullmatch(line.split()[-1]) for line in symbols.splitlines()), "unsupported runtime symbol linked"
    sections = subprocess.check_output([cross + "readelf", "-SW", str(image)], text=True)
    forbidden_sections = {".init_array", ".preinit_array", ".fini_array", ".ctors", ".dtors", ".ARM.exidx", ".ARM.extab", ".eh_frame"}
    for section, size in re.findall(r"\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)", sections):
        assert section not in forbidden_sections or int(size, 16) == 0, f"unsupported runtime section: {section}"
    parsed = {line.split()[2]: int(line.split()[0], 16) for line in symbols.splitlines() if len(line.split()) == 3}
    assert parsed["vector_table"] == 0
    assert parsed["_stack_top"] == 0x20010000 and parsed["_stack_limit"] == 0x2000F000
    assert parsed["_bss_end"] <= parsed["_stack_limit"]
    print(f"PASS {name}.elf: vector symbol at 0, reserved main stack, no unresolved symbols or selected runtime artifacts")
