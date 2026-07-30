// kmp_hook.c — Kamstrup KMP water-meter poll for the BAT32G135 OBI reader.
// (Wollmilchsau Phase-1 draft. COMPILES + links into free flash; the call-site
// injection + baud switch are armed separately on HW-verify — never flash a
// blind injection.)
//
// Reuses reversed vendor primitives (all addresses verified from reader_stock_v57.bin,
// load base 0x4000):
//   sendOptical(buf,len) @0x59A4  — copy up to 128 B into the TX buffer + kick off
//                                    interrupt-paced transmit on the optical UART.
//   RX callback slot      0x2000009C — the optical RX ISR does `if(cb) cb(rxbyte)`;
//                                    install our collector here (Thumb bit set).
//   energy import field   0x20000D68 — read out by the existing LoRa report; we drop
//                                    the decoded volume (in litres) here so it rides the
//                                    normal reader->gateway uplink untouched.
//
// KMP (field-validated vs a real Multical 21, see kamstrup-multical21-kmp memory):
//   query reg 0x0044 (V1 volume, m3): 80 3F 10 01 00 44 4D C0 0D  (pre-stuffed, CRC+stop baked in)
//   reply e.g. 40 3F 10 00 44 28 04 43 00 04 CB 13 29 23 0D
//              = unit 0x28(m3), mantlen 4, sign/exp 0x43(exp -3, positive), mant 0x0004CB13=314131
//              -> 314.131 m3 = 314131 L.
// Wire: prefix 0x80=request/0x40=reply; stop 0x0D; escape {06,0D,1B,40,80} as 1B,b^FF;
//       CRC = CCITT poly 0x1021 init 0, bitwise, valid when CRC over the destuffed frame == 0.

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef int            i32;

#define KMP_TX_ADDR   0x59A4u                 // sendOptical(buf,len) — Thumb
#define KMP_RXCB_SLOT ((volatile u32 *)0x2000009Cu)
#define KMP_IMPORT    ((volatile u32 *)0x20000D68u)

typedef void (*send_fn)(const u8 *buf, int len);
#define vendor_send_optical ((send_fn)(KMP_TX_ADDR | 1u))

// fixed request for register 0x0044 (volume) — constant, so no runtime CRC/stuffing needed
static const u8 kmp_query_v1[9] = { 0x80,0x3F,0x10,0x01,0x00,0x44,0x4D,0xC0,0x0D };

// RX assembly state in verified-safe scratch RAM (0x20001000+ block, per README)
#define KMP_BUF   ((volatile u8  *)0x20001040u)   // raw received bytes (pre-destuff)
#define KMP_LEN   ((volatile u32 *)0x20001020u)   // count in KMP_BUF
#define KMP_RDY   ((volatile u32 *)0x20001024u)   // 1 once a full 0x0D-terminated frame is in
#define KMP_MAX   64

// ---- divide-by-10 with no hardware divide / no soft-division runtime ----
// Freestanding -nostdlib: a plain `n / 10` links __aeabi_uidiv (absent). This uses only
// shifts/adds/MULS. Correct for all u32. (Multiply is fine on M0+; only divide is missing.)
static u32 div10(u32 n)
{
    u32 q = (n >> 1) + (n >> 2);   // ~0.75n
    q += q >> 4; q += q >> 8; q += q >> 16;
    q >>= 3;                        // ~n/10
    u32 r = n - (((q << 2) + q) << 1);   // n - q*10
    return q + ((r + 6) >> 4);      // correct the off-by-one
}

// ---- CRC: true-CCITT, poly 0x1021, init 0, bitwise (matches PHK kamstrup.py) ----
static u16 kmp_crc(const u8 *d, int n)
{
    u32 reg = 0;
    for (int i = 0; i < n; i++) {
        reg ^= (u32)d[i] << 8;
        for (int b = 0; b < 8; b++)
            reg = (reg & 0x8000) ? ((reg << 1) ^ 0x1021) : (reg << 1);
    }
    return (u16)reg;
}

// ---- RX collector: installed at KMP_RXCB_SLOT; called by the optical RX ISR per byte ----
// Bounded + non-blocking: just accumulates until the 0x0D stop (or buffer full). NEVER waits.
void kmp_rx_byte(u8 b)
{
    u32 n = *KMP_LEN;
    if (*KMP_RDY) return;                 // already have a frame; ignore trailing noise
    if (n < KMP_MAX) KMP_BUF[n++] = b;
    *KMP_LEN = n;
    if (b == 0x0D) *KMP_RDY = 1;          // end of frame
}

// ---- send the volume query (call once per read cycle, after the port is at 1200 8N1) ----
void kmp_poll(void)
{
    *KMP_LEN = 0;
    *KMP_RDY = 0;
    *KMP_RXCB_SLOT = ((u32)&kmp_rx_byte) | 1u;   // install our collector (Thumb bit)
    vendor_send_optical(kmp_query_v1, sizeof kmp_query_v1);
    // reply is gathered by kmp_rx_byte via the ISR; parsed later by kmp_decode()
    // (called from the same read-cycle tail after a bounded wait — no spin here).
}

// ---- decode the collected reply -> litres into the import field. Returns 1 on success. ----
// Destuff, CRC-check, then parse unit/mantissa-length/sign+exp/mantissa per the frame above.
int kmp_decode(void)
{
    if (!*KMP_RDY) return 0;

    // destuff into a local copy: 0x1B <x> -> (x ^ 0xFF)
    u8 f[KMP_MAX]; int m = 0;
    u32 n = *KMP_LEN;
    for (u32 i = 0; i < n && m < KMP_MAX; i++) {
        u8 c = KMP_BUF[i];
        if (c == 0x1B && i + 1 < n) { i++; f[m++] = KMP_BUF[i] ^ 0xFF; }
        else                        {        f[m++] = c; }
    }
    if (m < 10 || f[0] != 0x40) return 0;          // not a valid reply header
    // strip trailing 0x0D, CRC over the rest (incl. its 2 CRC bytes) must be 0
    if (f[m-1] == 0x0D) m--;
    if (kmp_crc(f, m) != 0) return 0;              // CRC fail

    // fields (offsets validated against the real Multical 21 capture):
    // [3..4]=reg, [5]=unit, [6]=mantlen, [7]=sign/exp, [8..]=mantissa BE
    u8 unit    = f[5];
    u8 mantlen = f[6];
    u8 se      = f[7];
    if (mantlen == 0 || mantlen > 4 || 8 + mantlen > m) return 0;

    u32 mant = 0;
    for (int i = 0; i < mantlen; i++) mant = (mant << 8) | f[8 + i];

    int exp = se & 0x3F;
    if (se & 0x40) exp = -exp;            // 0x40 = exponent negative
    int neg = (se & 0x80) != 0;           // 0x80 = value negative

    // convert to LITRES (integer) so it fits the u32 import field:
    //   value_m3 = mant * 10^exp   (unit 0x28 = 40 = m3)
    //   litres   = value_m3 * 1000 = mant * 10^(exp+3)
    (void)unit;                            // (kept for a future multi-register/unit branch)
    i32 e = exp + 3;
    u32 litres = mant;
    if (e >= 0) { for (int i = 0; i < e; i++) litres *= 10; }
    else        { for (int i = 0; i < -e; i++) litres = div10(litres); }

    *KMP_IMPORT = neg ? (u32)(-(i32)litres) : litres;   // -> rides the existing LoRa report
    *KMP_RDY = 0;                          // consumed; ready for the next cycle
    return 1;
}
