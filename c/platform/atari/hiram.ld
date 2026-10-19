/* BomberNet on the Atari XL/XE: added to llvm-mos's atari8-dos link script.
   The OS is off while the game runs (atari.s), so the RAM under its ROM is
   ours, and the RAM above MEMTOP:
     0xc000-0xcfff  the four fonts (1 KB each, as ANTIC wants them)
     0xd800-0xfff9  code and constants of the objects named hi_*.o (the
                    network code); the CPU's vectors above
     0xb800-0xbaff  sprite memory's unused first 0x300 bytes: the variables
                    of hi_netdev_soft.o (zeroed by plat_init, before any
                    network call)
   (0xb400 the screen, 0xb800 the sprites: tools/build_atari.py.)
   tools/build_atari.py puts the code and fonts into the .xex as segments
   that the loader copies up (atari_hicopy). */
MEMORY {
    hifont (rw) : ORIGIN = 0xc000, LENGTH = 0x1000
    hiram (rwx) : ORIGIN = 0xd800, LENGTH = 0xfffa - 0xd800
    pmggap (rw) : ORIGIN = 0xb800, LENGTH = 0x300
}
SECTIONS {
    .hifont : { *(.rodata.atari_field_font) *(.rodata.atari_text_font)
                *(.rodata.atari_title_font) *(.rodata.atari_logo_font) } >hifont
    .hitext : { *hi_*.o(.text .text.* .rodata .rodata.*) } >hiram
    .hibss (NOLOAD) : { *hi_netdev_soft.o(.bss .bss.*) } >pmggap
} INSERT BEFORE .text;
