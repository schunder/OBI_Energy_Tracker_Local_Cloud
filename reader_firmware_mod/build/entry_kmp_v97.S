.syntax unified
.thumb
.section .text.kmp_tramp, "ax", %progbits
.global kmp_tramp
.thumb_func
kmp_tramp:                 @ replaces `bl 0x5A84` @0x5A18; lr = 0x5A1C on entry
    push {r4, lr}
    ldr  r4, =(0x5A84 | 1)
    blx  r4               @ run the original sub_5A84
    bl   kmp_arm          @ then our sentinel hook
    pop  {r4, pc}         @ return to read cycle at 0x5A1C
