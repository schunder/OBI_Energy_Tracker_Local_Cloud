.syntax unified
.thumb
.section .text.kmp_tramp, "ax", %progbits
.global kmp_tramp
.thumb_func
kmp_tramp:                 @ replaces `bl 0x5BFA` @0x5A08 -- pure detour, NO hook body
    push {r4, lr}
    ldr  r4, =(0x5BFA | 1)
    blx  r4               @ call the original sub_5BFA
    pop  {r4, pc}         @ return to 0x5A0C -- net effect identical to the original bl
