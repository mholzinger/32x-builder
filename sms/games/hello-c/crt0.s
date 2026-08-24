;; Minimal crt0 for harness blobs (RAM-resident, entry at $0000).
;; The 68K copies the whole image into Z80 RAM and releases reset; the
;; Z80 wakes here. Stack below $1F80 per the contract.
;;
;; Uninit Z80 RAM boots $FF and the image gaps arrive as makebin's $FF
;; fill, so nothing may rely on implicit zeros: _DATA and _BSS are zeroed
;; here, then gsinit copies initialized globals (_INITIALIZER ->
;; _INITIALIZED) the standard SDCC way.
    .module crt0
    .globl  _main
    .globl  l__INITIALIZER
    .globl  s__INITIALIZER
    .globl  s__INITIALIZED
    .globl  l__DATA
    .globl  s__DATA
    .globl  l__BSS
    .globl  s__BSS

    .area   _HEADER (ABS)
    .org    0x0000
init:
    di
    ld  sp, #0x1F80
    ld  hl, #s__DATA
    ld  bc, #l__DATA
    call    zero_area
    ld  hl, #s__BSS
    ld  bc, #l__BSS
    call    zero_area
    call    gsinit
    call    _main
halt_loop:
    jr  halt_loop

zero_area:
    ld  a, b
    or  a, c
    ret Z
    ld  (hl), #0
    inc hl
    dec bc
    jr  zero_area

    ;; area ordering: code first, then data, all inside the flat image
    .area   _HOME
    .area   _CODE
    .area   _INITIALIZER
    .area   _GSINIT
    .area   _GSFINAL
    .area   _DATA
    .area   _INITIALIZED
    .area   _BSEG
    .area   _BSS
    .area   _HEAP

    .area   _GSINIT
gsinit::
    ld  bc, #l__INITIALIZER
    ld  a, b
    or  a, c
    jr  Z, gsinit_done
    ld  de, #s__INITIALIZED
    ld  hl, #s__INITIALIZER
    ldir
gsinit_done:
    .area   _GSFINAL
    ret
