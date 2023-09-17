#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
.SECONDARY:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM)
endif

include $(DEVKITARM)/ds_rules

export TARGET		:=	dsxTool
export TOPDIR		:=	$(CURDIR)

export VERSION_MAJOR	:= 1
export VERSION_MINOR	:= 0
export VERSTRING	:=	$(VERSION_MAJOR).$(VERSION_MINOR)

.PHONY: clean arm7/$(TARGET).elf arm9/$(TARGET).elf

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all: $(TARGET).nds

dist:	all
	@mkdir -p debug
	@cp $(TARGET).nds
	@cp $(TARGET).arm7.elf debug/$(TARGET).arm7.elf
	@cp $(TARGET).arm9.elf debug/$(TARGET).arm9.elf

$(TARGET).nds:	$(TARGET).arm7 $(TARGET).arm9
	ndstool	-c $(TARGET).nds -7 $(TARGET).arm7.elf -9 $(TARGET).arm9.elf \
			-b $(CURDIR)/icon.bmp "DS-Xtreme NAND Tool;NAND Dumper;Apache Thunder" \
			-g DSXT 01 "DSXtremeTool" -z 80040000 -u 00030004 -a 00000138 -p 0001

$(TARGET).arm7	: arm7/$(TARGET).elf
	cp arm7/$(TARGET).elf $(TARGET).arm7.elf
$(TARGET).arm9	: arm9/$(TARGET).elf
	cp arm9/$(TARGET).elf $(TARGET).arm9.elf

#---------------------------------------------------------------------------------
arm7/$(TARGET).elf:
	$(MAKE) -C arm7
	
#---------------------------------------------------------------------------------
arm9/$(TARGET).elf:
	$(MAKE) -C arm9

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr debug
	@rm -fr data
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds $(TARGET).nds.orig.nds
	@rm -fr $(TARGET).arm7
	@rm -fr $(TARGET).arm9
	@rm -fr $(TARGET).arm7.elf
	@rm -fr $(TARGET).arm9.elf
	@$(MAKE) -C arm9 clean
	@$(MAKE) -C arm7 clean

data:
	@mkdir -p data
