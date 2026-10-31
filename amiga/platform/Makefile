# Yie Ar Kung-Fu for the Amiga 1200 (AGA, 68EC020/020, Kickstart 3.x)   make  ->  ../../build/a1200/yiear_a1200.adf
PREFIX  ?= m68k-linux-gnu-
CC      := $(PREFIX)gcc
LD      := $(PREFIX)ld -z noexecstack --no-warn-rwx-segments -q
OBJCOPY := $(PREFIX)objcopy
HERE    := $(CURDIR)
ROOT    := $(abspath ../..)
BUILD   ?= a1200
B       := $(ROOT)/build/$(BUILD)
A       := $(ROOT)/amiga
ROMS    := $(ROOT)/roms
EXTRA   ?=
CFLAGS  := -MMD -MP $(EXTRA) -m68020 -ffreestanding -nostdlib -fno-pic -O2 -fomit-frame-pointer -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks \
           -Wall -Wno-misleading-indentation -I$(HERE) -I$(A)/runtime -I$(A)/ref/src -I$(B) -DA1200
ASFLAGS := -m68020 -x assembler -Wa,-I$(A)/runtime

OBJS := $(B)/main.o $(B)/board.o $(B)/yrt.o $(B)/render.o $(B)/m6809.o $(B)/m6809f.o $(B)/ycode.o $(B)/ysys.o $(B)/blobs.o \
        $(B)/plat.o $(B)/rt.o $(B)/rtasm.o $(B)/isr.o

all: $(B)/yiear_a1200.adf
$(B):
	mkdir -p $(B)
$(B)/ycode.s: $(A)/translator/ytrans.py $(A)/translator/emit.py $(A)/translator/model.py | $(B)
	python3 -I $(A)/translator/ytrans.py $@ > $(B)/ytrans.log
$(B)/ycode.o: $(B)/ycode.s
	$(CC) $(ASFLAGS) -c $< -o $@
$(B)/ysys.o: $(A)/runtime/ysys.s
	$(CC) $(ASFLAGS) -c $< -o $@
$(B)/rom.bin: | $(B)
	cat $(ROMS)/407_i08.10d $(ROMS)/407_i07.8d > $@
$(B)/gfx.bin $(B)/palette.h: $(A)/tools/mkgfx.py $(A)/tools/gfxdec.py | $(B)
	python3 -I $(A)/tools/mkgfx.py $(B)
$(B)/blobs.s: blobs.s $(B)/rom.bin $(B)/gfx.bin
	sed -e 's|ROMBLOB|$(B)/rom.bin|; s|GFXBLOB|$(B)/gfx.bin|' blobs.s > $@
$(B)/blobs.o: $(B)/blobs.s
	$(CC) $(ASFLAGS) -c $< -o $@
$(B)/main.o: main.c $(B)/palette.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/board.o: $(A)/runtime/board.c | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/yrt.o: $(A)/runtime/yrt.c | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/render.o: $(A)/runtime/render.c | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/m6809.o: $(A)/ref/src/m6809.c | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/m6809f.o: $(A)/ref/src/m6809.c | $(B)
	$(CC) $(CFLAGS) -DM6809_FLAT -Dm6809_step=m6809_step_flat -Dm6809_reset=m6809_reset_flat -c $< -o $@
$(B)/plat.o: platform.c platform.h | $(B)
	$(CC) -m68020 -ffreestanding -nostdlib -fno-pic -O2 -Wall -Wextra -fomit-frame-pointer -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -I$(HERE) -c $< -o $@
$(B)/rt.o: rt.c | $(B)
	$(CC) -m68020 -ffreestanding -nostdlib -fno-pic -O2 -fomit-frame-pointer -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -I$(HERE) -c $< -o $@
$(B)/rtasm.o: rtasm.s | $(B)
	$(CC) $(ASFLAGS) -c $< -o $@
$(B)/isr.o: isr.s | $(B)
	$(CC) $(ASFLAGS) -c $< -o $@
$(B)/crt0.o: crt0.s | $(B)
	$(CC) $(ASFLAGS) -c $< -o $@
$(B)/boot.bin: boot.s | $(B)
	$(CC) $(ASFLAGS) -c $< -o $(B)/boot.o
	$(LD) -e 0 -Ttext=0 --oformat=binary -o $@ $(B)/boot.o
	@test `stat -c %s $@` -le 1024 || (echo "bootblock > 1024 bytes"; false)
$(B)/yiear.elf: $(OBJS) $(B)/crt0.o link.ld
	$(LD) -T link.ld -Map=$(B)/yiear.map -o $@ $(B)/crt0.o $(OBJS)
$(B)/yiear.bin: $(B)/yiear.elf
	$(OBJCOPY) -O binary $< $@
$(B)/yiear_a1200.adf: $(B)/yiear.elf $(B)/boot.bin mkdisk.py
	python3 mkdisk.py $@ $(B)/boot.bin $(B)/yiear.elf
	@$(PREFIX)size $(B)/yiear.elf
clean:
	rm -rf $(B)
.PHONY: all clean
-include $(wildcard $(B)/*.d)
