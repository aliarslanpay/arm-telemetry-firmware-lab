SHELL := /bin/bash
CROSS ?= arm-none-eabi-
HOST_CXX ?= g++
QEMU ?= qemu-system-arm
BUILD := build
DEPS_DIR ?= $(CURDIR)/.deps
KERNEL := $(DEPS_DIR)/FreeRTOS-Kernel-V11.1.0
CPU := -mcpu=cortex-m3 -mthumb
WARN := -Wall -Wextra -Werror
ARM_INCLUDES ?=
FREESTANDING := $(ARM_INCLUDES) $(CPU) -Os -g3 -ffreestanding -ffunction-sections -fdata-sections -MMD -MP
CFLAGS := $(FREESTANDING) $(WARN) -std=c11
CXXFLAGS := $(FREESTANDING) $(WARN) -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-unwind-tables -fno-asynchronous-unwind-tables
LDFLAGS := $(CPU) -nostdlib -Wl,--gc-sections -T platform/mps2.ld
.PHONY: all boot-smoke clean bootstrap
all: $(BUILD)/boot.elf
$(BUILD):
	mkdir -p $@
$(BUILD)/startup.o: platform/startup.S | $(BUILD)
	$(CROSS)gcc $(CPU) -c $< -o $@
$(BUILD)/reset.o: platform/reset.c | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
$(BUILD)/boot.o: firmware/boot.cpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/boot.elf: $(BUILD)/startup.o $(BUILD)/reset.o $(BUILD)/boot.o platform/mps2.ld
	$(CROSS)g++ $(LDFLAGS) $(filter %.o,$^) -Wl,-Map,$@.map -o $@
.PHONY: check-constructors
$(BUILD)/constructor_probe.o: tests/constructor_probe.cpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) -c $< -o $@
check-constructors: $(BUILD)/boot.elf $(BUILD)/constructor_probe.o
	@set -eu; \
	image="$(BUILD)/constructor_probe.elf"; log="$(BUILD)/constructor_probe.log"; \
	rm -f "$$image"; \
	if $(CROSS)g++ $(LDFLAGS) $(BUILD)/startup.o $(BUILD)/reset.o $(BUILD)/boot.o $(BUILD)/constructor_probe.o -o "$$image" >"$$log" 2>&1; then \
		rm -f "$$image"; echo 'FAIL: global constructor linked successfully' >&2; exit 1; \
	fi; \
	if ! grep -Fq 'Global constructors are unsupported' "$$log"; then \
		cat "$$log" >&2; echo 'FAIL: link failed without the constructor guard diagnostic' >&2; exit 1; \
	fi; \
	echo 'PASS constructor guard: .init_array probe rejected by linker'
boot-smoke: $(BUILD)/boot.elf
	timeout 10 $(QEMU) -M mps2-an385 -display none -monitor none -serial stdio -semihosting-config enable=on,target=native -kernel $<
bootstrap:
	DEPS_DIR="$(DEPS_DIR)" tools/bootstrap.sh
clean:
	rm -rf $(BUILD)
.PHONY: test
$(BUILD)/protocol_test: tests/protocol_test.cpp shared/protocol.cpp shared/protocol.hpp shared/bounded_queue.hpp | $(BUILD)
	$(HOST_CXX) -std=c++17 $(WARN) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Ishared tests/protocol_test.cpp shared/protocol.cpp -o $@
test: $(BUILD)/protocol_test
	./$(BUILD)/protocol_test
-include $(wildcard $(BUILD)/*.d)
