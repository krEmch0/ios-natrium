# Makefile for Mi 5s Plus boot shim

CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

# Directories
SRC_DIR := src
OUT_DIR := out

# Source files
ASM_SRC := $(SRC_DIR)/start.S
C_SRC := $(SRC_DIR)/platform.c $(SRC_DIR)/uart.c $(SRC_DIR)/payload.c
LINKER_SCRIPT := $(SRC_DIR)/linker.ld

# Object files
ASM_OBJ := $(OUT_DIR)/start.o
C_OBJ := $(C_SRC:$(SRC_DIR)/%.c=$(OUT_DIR)/%.o)
ALL_OBJ := $(ASM_OBJ) $(C_OBJ)

# Output files
ELF := $(OUT_DIR)/shim.elf
BIN := $(OUT_DIR)/shim.bin
MAP := $(OUT_DIR)/shim.map
DISASM := $(OUT_DIR)/shim.dis

# Compiler flags
CFLAGS := -march=armv8-a -mtune=cortex-a57 \
          -O2 -g -nostdlib -nostartfiles -ffreestanding \
          -fno-builtin -fno-common -fno-stack-protector \
          -Wall -Wextra -Werror \
          -I$(SRC_DIR)

# Assembler flags
ASFLAGS := -march=armv8-a -g

# Linker flags
LDFLAGS := -T $(LINKER_SCRIPT) -nostdlib -static \
           -Map=$(MAP)

.PHONY: all clean

all: $(BIN) $(DISASM)
	@echo ""
	@echo "✓ Boot shim built successfully"
	@echo "  Binary: $(BIN)"
	@echo "  Size: $$(stat -c%s $(BIN)) bytes"
	@echo ""

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(ASM_OBJ): $(ASM_SRC) | $(OUT_DIR)
	@echo "AS    $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

$(OUT_DIR)/%.o: $(SRC_DIR)/%.c | $(OUT_DIR)
	@echo "CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(ALL_OBJ) $(LINKER_SCRIPT)
	@echo "LD    $@"
	@$(LD) $(LDFLAGS) $(ALL_OBJ) -o $@

$(BIN): $(ELF)
	@echo "OBJCOPY $@"
	@$(OBJCOPY) -O binary $< $@

$(DISASM): $(ELF)
	@echo "OBJDUMP $@"
	@$(OBJDUMP) -d $< > $@

clean:
	@echo "Cleaning shim build..."
	@rm -rf $(OUT_DIR)
	@echo "✓ Clean complete"
