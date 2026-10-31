| boot.s - Yie Ar Kung-Fu Amiga 1200 bootblock (KS 3.x, 68020+).  Must stay <= 1024 bytes.
|
| Entered by the OS strap with A6 = ExecBase, A1 = trackdisk IOStdReq (open, DF0:).
| 0. CPU check: 68040/68060 are refused (cyan screen) - the cache handling below is the 68020/68030 one.
| 1. AllocAbs the fixed chip RAM region (display buffers, copper list, audio buffers) so that the OS cannot use it.
| 2. AllocMem a staging buffer in chip RAM and read the payload from the disk (trackdisk CMD_READ, byte offset 1024).
| 3. AllocMem the run area: FAST RAM if there is any, else chip RAM (zeroed): text + rodata + data + bss of the payload.
| 4. Supervisor(): mask all interrupts, finish the Blitter, switch off all DMA, copy the payload into the run area, apply the relocation table
|    (every 32-bit pointer that points into the linked image is moved by dest - link_base), clear the caches and jump to the entry point.
|    The OS is never used again.
| Failure (screen turns a solid colour, machine hangs):
|    magenta = AllocMem/AllocAbs failed, red = disk read error, cyan = 68040/68060.
|
| The header fields (offset 20..) are patched by mkdisk.py.
    .text
    .long   0x444F5300          | 'DOS\0'
    .long   0                   | checksum (filled in by mkadf.py)
    .long   880                 | root block
entry:                          | offset 12
    bra.w   start
    .long   0x5949454C          | offset 16: marker 'YIEL' (checked by mkdisk.py)
payload_len:    .long 0         | offset 20: bytes to read from the disk (multiple of 512)
load_len:       .long 0         | 24: bytes of the payload that are copied into the run area (text + rodata + data)
total_len:      .long 0         | 28: size of the run area (adds bss)
link_base:      .long 0         | 32: address the payload is linked at
entry_addr:     .long 0         | 36: link address of _start
reloc_off:      .long 0         | 40: byte offset of the relocation table inside the payload
reloc_count:    .long 0         | 44: number of 32-bit offsets (from link_base) in the relocation table
chip_base:      .long 0         | 48: fixed chip RAM region
chip_len:       .long 0         | 52
comp_len:       .long 0         | 56: bytes of LZ4-compressed image at the start of the payload (the relocation table follows at reloc_off)

start:
    move.l  %a1,%a4             | a4 = IO request
    move.w  296(%a6),%d0        | ExecBase->AttnFlags: bit 3 = 68040, bit 7 = 68060
    move.w  #0x00FF,%d1         | cyan
    btst    #3,%d0
    bne     fail
    btst    #7,%d0
    bne     fail
    move.l  chip_base(%pc),%a1
    move.l  chip_len(%pc),%d0
    jsr     -204(%a6)           | AllocAbs
    tst.l   %d0
    beq.s   fail_mem
    move.l  payload_len(%pc),%d7
    move.l  %d7,%d0
    moveq   #2,%d1              | MEMF_CHIP
    jsr     -198(%a6)           | AllocMem: staging buffer
    move.l  %d0,%d6             | d6 = staging buffer
    beq.s   fail_mem
    move.l  total_len(%pc),%d0
    move.l  #0x10004,%d1        | MEMF_FAST | MEMF_CLEAR
    jsr     -198(%a6)
    tst.l   %d0
    bne.s   got_dest
    move.l  total_len(%pc),%d0
    move.l  #0x10002,%d1        | MEMF_CHIP | MEMF_CLEAR
    jsr     -198(%a6)
    tst.l   %d0
    beq.s   fail_mem
got_dest:
    move.l  %d0,%d5             | d5 = run area
    move.w  #2,28(%a4)          | io_Command = CMD_READ
    move.l  %d7,36(%a4)         | io_Length
    move.l  %d6,40(%a4)         | io_Data
    move.l  #1024,44(%a4)       | io_Offset (payload starts at ADF byte 1024)
    move.l  %a4,%a1
    jsr     -456(%a6)           | DoIO
    tst.b   31(%a4)             | io_Error
    bne.s   fail_read
    move.w  #9,28(%a4)          | TD_MOTOR
    clr.l   36(%a4)             | io_Length = 0 -> motor off
    move.l  %a4,%a1
    jsr     -456(%a6)
    lea     super(%pc),%a5
    jsr     -30(%a6)            | Supervisor(a5)  - never returns

fail_mem:
    move.w  #0x0F0F,%d1
    bra.s   fail
fail_read:
    move.w  #0x0F00,%d1
fail:
    move.w  #0x7FFF,0xDFF096    | DMA off -> screen = COLOR00
    move.w  #0x7FFF,0xDFF09A
1:  move.w  %d1,0xDFF180
    bra.s   1b

super:                          | supervisor mode, d6 = staging buffer, d5 = run area
    move.w  #0x2700,%sr
    move.w  #0x7FFF,0xDFF09A    | INTENA off
    move.w  #0x7FFF,0xDFF09C    | INTREQ clear
    move.b  0xDFF002,%d0        | DMACONR: dummy read (BBUSY quirk)
2:  btst    #6,0xDFF002         | wait for Blitter idle (BBUSY, bit 14)
    bne.s   2b
    move.w  #0x7FFF,0xDFF096    | all DMA off
    move.w  #0x7FFF,0xDFF09C
    move.l  #0x0808,%d0         | CACR: clear I-cache (CI) and D-cache (CD, 68030), both disabled
    movec   %d0,%cacr
    move.l  %d6,%a0             | src = staging (LZ4 block of the image: load_len bytes when decoded)
    move.l  %d5,%a1             | dst = run area
    move.l  comp_len(%pc),%d0
    lea     0(%a0,%d0.l),%a4    | a4 = end of the compressed data
10: moveq   #0,%d1
    move.b  (%a0)+,%d1          | token
    move.l  %d1,%d2
    lsr.b   #4,%d1              | d1 = literal length
    and.w   #15,%d2             | d2 = match length - 4
    cmp.b   #15,%d1
    bne.s   12f
11: moveq   #0,%d3
    move.b  (%a0)+,%d3
    add.l   %d3,%d1
    cmp.b   #255,%d3
    beq.s   11b
12: subq.l  #1,%d1
    bmi.s   14f
13: move.b  (%a0)+,(%a1)+       | literals
    subq.l  #1,%d1
    bpl.s   13b
14: cmp.l   %a4,%a0
    bcc.s   20f                 | the last sequence has literals only
    moveq   #0,%d3
    moveq   #0,%d4
    move.b  (%a0)+,%d3
    move.b  (%a0)+,%d4
    lsl.w   #8,%d4
    or.w    %d4,%d3             | offset (little endian)
    move.l  %a1,%a2
    suba.l  %d3,%a2             | match source
    cmp.b   #15,%d2
    bne.s   16f
15: moveq   #0,%d3
    move.b  (%a0)+,%d3
    add.l   %d3,%d2
    cmp.b   #255,%d3
    beq.s   15b
16: addq.l  #3,%d2              | match length 4.. minus 1 (loop counter)
17: move.b  (%a2)+,(%a1)+       | byte-wise: overlapping copies (run-length) are intended
    subq.l  #1,%d2
    bpl.s   17b
    bra.s   10b
20:
    | relocation: table at staging + reloc_off; delta = run area - link_base
    move.l  %d6,%a0
    add.l   reloc_off(%pc),%a0
    move.l  %d5,%a3             | a3 = run area
    move.l  %d5,%d4
    sub.l   link_base(%pc),%d4  | d4 = delta
    move.l  link_base(%pc),%d3  | d3 = lowest address of the linked image
    move.l  %d3,%d2
    add.l   total_len(%pc),%d2  | d2 = highest
    move.l  reloc_count(%pc),%d0
    beq.s   6f
4:  move.l  (%a0)+,%d1          | offset of a 32-bit pointer, from link_base
    lea     0(%a3,%d1.l),%a2
    move.l  (%a2),%d7
    cmp.l   %d3,%d7
    bcs.s   5f                  | below the image (fixed chip RAM, hardware): keep
    cmp.l   %d2,%d7
    bhi.s   5f                  | above: keep
    add.l   %d4,%d7
    move.l  %d7,(%a2)
5:  subq.l  #1,%d0
    bne.s   4b
6:  move.l  #0x0808,%d0         | clear the caches again (the code that follows was just written)
    movec   %d0,%cacr
    move.l  entry_addr(%pc),%a0
    add.l   %d4,%a0
    jmp     (%a0)
