#!/usr/bin/env python3
# Emulate the BAT32G135 reader (Cortex-M0+) around the KMP injection to find WHY
# the 0x5A08 splice faults -- the SWD we can't attach, done in software.
import sys, struct
from unicorn import *
from unicorn.arm_const import *
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

BIN = sys.argv[1] if len(sys.argv) > 1 else r"C:\tmp\reader_armed_v96.bin"
START = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0xEE08   # trampoline entry
FULL  = "--full" in sys.argv                                     # start at read-session 0x59E8 instead

FLASH_BASE, FLASH_SIZE = 0x00000000, 0x00020000   # 128K window (app loads at 0x4000)
RAM_BASE,   RAM_SIZE   = 0x20000000, 0x00010000   # 64K RAM window
PERIPH_BASE,PERIPH_SIZE= 0x40000000, 0x00100000   # MMIO
SP_TOP = 0x20002000                                # ~8K SRAM stack top (BAT32G135)

app = open(BIN, "rb").read()
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB); md.detail = False

mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
mu.mem_map(FLASH_BASE, FLASH_SIZE)
mu.mem_map(RAM_BASE, RAM_SIZE)
mu.mem_write(0x4000, app)                          # image is the app @ load 0x4000

# --- peripheral MMIO: reads return all-ones so 'ready'/flag polls pass (no hang); writes ignored
# Spin-loop breaker: if the same PC hammers the same peripheral addr repeatedly (a flag-wait
# poll), flip that read to all-ones so the "ready" bit is seen and the loop exits. Otherwise 0
# (so values fed into divides/counters stay small and loops terminate).
_last = {"pc": 0, "addr": 0, "n": 0}
def periph_read(uc, offset, size, user):
    addr = PERIPH_BASE + offset
    pc = uc.reg_read(UC_ARM_REG_PC)
    if pc == _last["pc"] and addr == _last["addr"]:
        _last["n"] += 1
    else:
        _last.update(pc=pc, addr=addr, n=0)
    if _last["n"] > 3:                       # looks like a stuck poll -> report "ready"
        return 0xFFFFFFFF & ((1 << (size*8)) - 1)
    return 0
mu.mmio_map(PERIPH_BASE, PERIPH_SIZE, periph_read, None, lambda uc,o,s,v,u: None, None)
# Cortex-M PPB / System Control Space (SysTick, NVIC, SCB) @ 0xE0000000 -- reads 0, writes ignored
mu.mmio_map(0xE0000000, 0x00100000, lambda uc,o,s,u: 0, None, lambda uc,o,s,v,u: None, None)

trace = []   # ring buffer of (pc, mnemonic)
def hook_code(uc, addr, size, user):
    try:
        code = uc.mem_read(addr, size)
        ins = next(md.disasm(bytes(code), addr), None)
        m = f"{ins.mnemonic} {ins.op_str}" if ins else "?"
    except Exception:
        m = "??"
    trace.append((addr, m))
    if len(trace) > 40: trace.pop(0)
mu.hook_add(UC_HOOK_CODE, hook_code)

faults = []
def hook_mem_invalid(uc, access, addr, size, value, user):
    pc = uc.reg_read(UC_ARM_REG_PC)
    kind = {UC_MEM_READ_UNMAPPED:"RD_UNMAPPED", UC_MEM_WRITE_UNMAPPED:"WR_UNMAPPED",
            UC_MEM_FETCH_UNMAPPED:"FETCH_UNMAPPED", UC_MEM_READ_PROT:"RD_PROT",
            UC_MEM_WRITE_PROT:"WR_PROT", UC_MEM_FETCH_PROT:"FETCH_PROT"}.get(access, str(access))
    faults.append((kind, addr, pc))
    print(f"\n!!! FAULT {kind} addr={addr:#010x} size={size} at PC={pc:#010x}")
    return False   # do not fix -> stops emulation
mu.hook_add(UC_HOOK_MEM_INVALID, hook_mem_invalid)

# initial register state
mu.reg_write(UC_ARM_REG_SP, SP_TOP)
mu.reg_write(UC_ARM_REG_LR, 0x5A0C | 1)            # return addr the tramp expects (thumb)
for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3):
    mu.reg_write(r, 0)

begin = 0x59E8 if FULL else START
# stop at the read-session's own return (0x5A20) so we exercise the whole tail after our injection
STOP = 0x5A20
print(f"== emulating {BIN}")
print(f"== start={begin:#x} stop={STOP:#x} SP={SP_TOP:#x} LR=0x5A0C  (FULL={FULL})")
try:
    mu.emu_start(begin | 1, STOP, timeout=0, count=200000)
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"\n== emulation STOPPED cleanly at PC={pc:#010x} (reached stop / count)")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"\n== UcError: {e}  final PC={pc:#010x}")

print("\n--- last executed instructions ---")
for a, m in trace[-24:]:
    print(f"  {a:#010x}: {m}")
print("\n--- key regs ---")
for name, rid in [("r0",UC_ARM_REG_R0),("r1",UC_ARM_REG_R1),("r2",UC_ARM_REG_R2),("r3",UC_ARM_REG_R3),
                  ("r4",UC_ARM_REG_R4),("sp",UC_ARM_REG_SP),("lr",UC_ARM_REG_LR),("pc",UC_ARM_REG_PC)]:
    print(f"  {name}={mu.reg_read(rid):#010x}")
imp = struct.unpack("<I", mu.mem_read(0x20000D68, 4))[0]
print(f"  [0x20000D68] = {imp:#010x}  (sentinel 0xABCD == hook ran)")
print(f"\nfaults: {faults}")
