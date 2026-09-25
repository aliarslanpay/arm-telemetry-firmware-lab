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
all: $(BUILD)/boot.elf $(BUILD)/baremetal.elf $(BUILD)/freertos.elf $(BUILD)/inversion.elf
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
COMMON_OBJ := $(BUILD)/application.o $(BUILD)/runtime.o $(BUILD)/startup.o $(BUILD)/reset.o $(BUILD)/platform.o $(BUILD)/protocol.o
INCLUDES := -Ishared -Iplatform
$(BUILD)/platform.o: platform/platform.cpp platform/platform.hpp shared/protocol.hpp shared/bounded_queue.hpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/protocol.o: shared/protocol.cpp shared/protocol.hpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/baremetal.o: firmware/baremetal.cpp platform/platform.hpp shared/protocol.hpp shared/application.hpp shared/bounded_queue.hpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/baremetal.elf: $(COMMON_OBJ) $(BUILD)/baremetal.o platform/mps2.ld
	$(CROSS)g++ $(LDFLAGS) $(filter %.o,$^) -Wl,-Map,$@.map -o $@

$(BUILD)/runtime.o: platform/runtime.c | $(BUILD)
	$(CROSS)gcc $(CFLAGS) -fno-builtin -c $< -o $@

$(BUILD)/application.o: shared/application.cpp shared/application.hpp shared/protocol.hpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) $(INCLUDES) -c $< -o $@
RTOS_INC := $(INCLUDES) -Irtos -I$(KERNEL)/include -I$(KERNEL)/portable/GCC/ARM_CM3
RTOS_SRC := tasks queue list
RTOS_OBJ := $(addprefix $(BUILD)/kernel-,$(addsuffix .o,$(RTOS_SRC))) $(BUILD)/port.o $(BUILD)/hooks.o
.PHONY: kernel-check
kernel-check:
	@test "$$(git -C "$(KERNEL)" rev-parse HEAD)" = dbf70559b27d39c1fdb68dfb9a32140b6a6777a0 || { echo 'Run make bootstrap; kernel pin mismatch' >&2; exit 1; }
	@git -C "$(KERNEL)" diff --quiet HEAD -- tasks.c queue.c list.c include portable/GCC/ARM_CM3 || { echo 'Kernel source modified' >&2; exit 1; }
$(BUILD)/kernel-%.o: $(KERNEL)/%.c rtos/FreeRTOSConfig.h | $(BUILD) kernel-check
	$(CROSS)gcc $(CFLAGS) $(RTOS_INC) -c $< -o $@
$(BUILD)/port.o: $(KERNEL)/portable/GCC/ARM_CM3/port.c rtos/FreeRTOSConfig.h | $(BUILD) kernel-check
	$(CROSS)gcc $(CFLAGS) $(RTOS_INC) -c $< -o $@
$(BUILD)/hooks.o: rtos/hooks.c rtos/FreeRTOSConfig.h | $(BUILD)
	$(CROSS)gcc $(CFLAGS) $(RTOS_INC) -c $< -o $@
$(BUILD)/freertos.o: firmware/freertos.cpp rtos/FreeRTOSConfig.h platform/platform.hpp shared/application.hpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) $(RTOS_INC) -c $< -o $@
$(BUILD)/freertos.elf: $(COMMON_OBJ) $(RTOS_OBJ) $(BUILD)/freertos.o platform/mps2.ld
	$(CROSS)g++ $(LDFLAGS) $(filter %.o,$^) -Wl,-Map,$@.map -o $@
$(BUILD)/inversion.o: firmware/inversion.cpp rtos/FreeRTOSConfig.h platform/platform.hpp | $(BUILD)
	$(CROSS)g++ $(CXXFLAGS) $(RTOS_INC) -c $< -o $@
$(BUILD)/inversion.elf: $(COMMON_OBJ) $(RTOS_OBJ) $(BUILD)/inversion.o platform/mps2.ld
	$(CROSS)g++ $(LDFLAGS) $(filter %.o,$^) -Wl,-Map,$@.map -o $@

.PHONY: integration check-images verify demo-baremetal demo-freertos experiment
integration: all
	QEMU="$(QEMU)" timeout 20 python3 tools/harness.py --image $(BUILD)/baremetal.elf --scenario all
	QEMU="$(QEMU)" timeout 20 python3 tools/harness.py --image $(BUILD)/freertos.elf --scenario all
	QEMU="$(QEMU)" timeout 10 python3 tools/harness.py --image $(BUILD)/inversion.elf --scenario inversion
check-images: all
	CROSS="$(CROSS)" python3 tools/check_images.py
verify: test boot-smoke integration check-images check-constructors
demo-baremetal: $(BUILD)/baremetal.elf
	QEMU="$(QEMU)" timeout 10 python3 tools/harness.py --image $< --scenario auto
demo-freertos: $(BUILD)/freertos.elf
	QEMU="$(QEMU)" timeout 10 python3 tools/harness.py --image $< --scenario auto
experiment: $(BUILD)/inversion.elf
	QEMU="$(QEMU)" timeout 10 python3 tools/harness.py --image $< --scenario inversion
-include $(wildcard $(BUILD)/*.d)
