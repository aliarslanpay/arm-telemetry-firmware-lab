/* SPDX-License-Identifier: MIT
 * Linker script for QEMU MPS2. QEMU maps the low region to SSRAM1.
 * CODE is a software partition, not physical flash. */
ENTRY(Reset_Handler)
MEMORY {
  CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
_stack_limit = _stack_top - 4K;
SECTIONS {
  .vectors : { KEEP(*(.vectors)) } > CODE
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > CODE
  .data : { . = ALIGN(4); _data_start = .; *(.data*) . = ALIGN(4); _data_end = .; } > RAM AT> CODE
  _data_load = LOADADDR(.data);
  .bss (NOLOAD) : { . = ALIGN(8); _bss_start = .; *(.bss*) *(COMMON) . = ALIGN(8); _bss_end = .; } > RAM
  .init_array : { KEEP(*(.init_array*)) } > CODE
  /DISCARD/ : { *(.comment) *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) }
}
ASSERT(_bss_end <= _stack_limit, "RAM overlaps reserved 4 KiB main stack")
ASSERT(SIZEOF(.init_array) == 0, "Global constructors are unsupported")
