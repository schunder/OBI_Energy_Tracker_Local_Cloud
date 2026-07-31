.syntax unified
.thumb
.section .text.kmp_tramp, "ax", %progbits
.global kmp_tramp
.thumb_func
kmp_tramp:                 @ replaces `bl 0x5BFA` @0x5A08; entered with lr = 0x5A0C (read-cycle return)
    push {r4, lr}          @ save r4 + the read-cycle return address
    push {r0-r3}           @ save the original args destined for sub_5BFA
    bl   kmp_arm           @ our bounded hook (decode prev reply, retune, send query)
    pop  {r0-r3}           @ restore sub_5BFA's args exactly as they were
    ldr  r4, =(0x5BFA | 1) @ thumb address of the original call target
    blx  r4                @ run sub_5BFA (returns here; r0 = its result, unchanged by us)
    pop  {r4, pc}          @ restore r4, return to the read cycle at 0x5A0C
