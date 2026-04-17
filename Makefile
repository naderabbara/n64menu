V=1
SOURCE_DIR=src
BUILD_DIR=build
.DEFAULT_GOAL := all

LIBDRAGON_ROOT ?= ../libdragon
N64_MK_PATH := $(N64_INST)/include/n64.mk

ifeq ($(wildcard $(N64_MK_PATH)),)
N64_MK_PATH := $(LIBDRAGON_ROOT)/n64.mk
endif

ifeq ($(wildcard $(N64_MK_PATH)),)
$(error Could not find n64.mk. Set N64_INST to your installed libdragon toolchain, or keep a libdragon checkout at ../libdragon)
endif

include $(N64_MK_PATH)

ifeq ($(strip $(N64_INST)),)
$(error N64_INST is not set. Point it at your installed libdragon toolchain root)
endif

BOXART_PNGS := $(wildcard src/assets/boxart/*.png)
UI_PNGS := $(wildcard src/assets/ui/*.png)
STATIC_FILESYSTEM_ASSETS := $(shell find filesystem -type f ! -path 'filesystem/boxart/*' ! -path 'filesystem/ui/*' 2>/dev/null)

BOXART_SPRITES := $(patsubst src/assets/boxart/%.png,filesystem/boxart/%.sprite,$(BOXART_PNGS))
UI_SPRITES := $(patsubst src/assets/ui/%.png,filesystem/ui/%.sprite,$(UI_PNGS))
GENERATED_SPRITES := $(BOXART_SPRITES) $(UI_SPRITES)

sc64: mockup_menu.z64
	@cp $< sc64menu.n64
.PHONY: sc64

all: mockup_menu.z64
.PHONY: all
 
OBJS = $(BUILD_DIR)/main.o \
$(BUILD_DIR)/boot/cic.o \
$(BUILD_DIR)/boot/boot.o \
$(BUILD_DIR)/boot/reboot.o \
$(BUILD_DIR)/flashcart/64drive/64drive_ll.o \
$(BUILD_DIR)/flashcart/64drive/64drive.o \
$(BUILD_DIR)/flashcart/flashcart_utils.o \
$(BUILD_DIR)/flashcart/flashcart.o \
$(BUILD_DIR)/flashcart/sc64/sc64_ll.o \
$(BUILD_DIR)/flashcart/sc64/sc64.o \
$(BUILD_DIR)/utils/fs.o

mockup_menu.z64: N64_ROM_TITLE="Mockup Menu"
mockup_menu.z64: $(BUILD_DIR)/spritemap.dfs

filesystem/boxart/%.sprite: src/assets/boxart/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f RGBA16 -o "$(dir $@)" "$<"

filesystem/ui/%.sprite: src/assets/ui/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f RGBA16 -o "$(dir $@)" "$<"

$(BUILD_DIR)/spritemap.dfs: $(STATIC_FILESYSTEM_ASSETS) $(GENERATED_SPRITES)
$(BUILD_DIR)/mockup_menu.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) filesystem/boxart filesystem/ui *.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)
