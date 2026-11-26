#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

export TARGET := nrioTool
export TOPDIR := $(CURDIR)

# GMAE_ICON is the image used to create the game icon, leave blank to use default rule
GAME_ICON :=

# specify a directory which contains the nitro filesystem
# this is relative to the Makefile
NITRO_FILES := nrioFiles
NITRO_STAGE := build/nitrofs


# These set the information text in the nds file
#GAME_TITLE     := My Wonderful Homebrew
#GAME_SUBTITLE1 := built with devkitARM
#GAME_SUBTITLE2 := http://devitpro.org

include $(DEVKITARM)/ds_rules

.PHONY: data bundle bootloader clean

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all: $(TARGET).nds

#---------------------------------------------------------------------------------
checkarm7:
	$(MAKE) -C arm7

#---------------------------------------------------------------------------------
checkarm9:
	$(MAKE) -C arm9

#---------------------------------------------------------------------------------
$(TARGET).nds : $(NITRO_FILES) $(wildcard $(NITRO_FILES)/*) prepare_nitrofs.py arm7/$(TARGET).elf arm9/$(TARGET).elf nrio-unified.dldi
	@python3 prepare_nitrofs.py "$(CURDIR)"
	@ndstool	-c $@ -7 arm7/$(TARGET).elf -9 arm9/$(TARGET).elf \
				-b $(CURDIR)/icon.bmp "NAND Cart Tool;For N-Card/Clones;By Apache Thunder" \
				-g DSNR 01 "NRIOTOOL" -z 80040000 -u 00030004 -a 00000138 -p 0001 \
				-d $(NITRO_STAGE)
	@dlditool nrio-unified.dldi $@

data:
	@mkdir -p data

.PHONY: xulu-menu
xulu-menu:
	$(MAKE) -C XuluMenu

bundle: xulu-menu
	python3 embed_bundle.py "$(CURDIR)"

bootloader: data bundle
	$(MAKE) -C bootloader LOADBIN=$(TOPDIR)/data/load.bin

#---------------------------------------------------------------------------------
arm7/$(TARGET).elf:
	$(MAKE) -C arm7

#---------------------------------------------------------------------------------
arm9/$(TARGET).elf: bootloader
	$(MAKE) -C arm9

#---------------------------------------------------------------------------------
clean:
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm7 clean
	$(MAKE) -C bootloader clean
	rm -f data/load.bin data/bootstub.bin
	rm -rf $(NITRO_STAGE)
	rm -f $(TARGET).nds

