// SPDX-License-Identifier: MIT
// Negative link test: GCC emits an .init_array entry for this constructor.
volatile unsigned constructor_probe;
struct ConstructorProbe {
    ConstructorProbe() { constructor_probe = 1; }
};
ConstructorProbe probe;
