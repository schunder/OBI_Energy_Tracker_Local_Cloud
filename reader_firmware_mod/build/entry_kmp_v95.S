.syntax unified
.thumb
.section .text.kmp_tramp, "ax", %progbits
.global kmp_tramp
.thumb_func
kmp_tramp:                 @ replaces `bl 0x5BFA` @0x5A08; lr = 0x5A0C on entry
    push {r4, lr}          @ minimal frame: 8 bytes (was 24 -- r0-r3 unneeded, sub_5BFA ignores them)
    ldr  r4, =(0x5BFA | 1) @ thumb addr of the original call target
    blx  r4               @ run sub_5BFA FIRST, holding only our 8 bytes across its deep subtree
    bl   kmp_arm          @ then our bounded hook (leaf/shallow)
    pop  {r4, pc}         @ restore r4, return to read cycle at 0x5A0C
