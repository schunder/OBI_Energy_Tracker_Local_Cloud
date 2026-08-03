// Host test harness for kmp_hook.c — runs the REAL decoder natively, no reader hardware.
//
// Why this exists: the decoder CRC'd over the 0x40 start delimiter, so every valid meter reply
// was rejected. That defect was invisible on hardware (the injection never ran) and invisible in
// review until the CRC was computed by hand. This harness makes that class of bug fail loudly.
//
// Build + run:
//   cc -Wno-pointer-to-int-cast -DKMP_HOST_TEST -I. test/test_kmp_decode.c -o /tmp/kmp_test \
//     && /tmp/kmp_test
//
// (the cast warning is host-only: kmp_poll stores a function address into a u32 RAM slot, which
//  is exact on the 32-bit target and lossy only on a 64-bit host that never calls it)
//
// The target build is untouched: kmp_hook.c's real addresses are #ifndef-guarded and we override
// them here to point at ordinary arrays.

#include <stdio.h>
#include <string.h>

// ---- stub the reader's memory map with host arrays, BEFORE including the hook ----------------
static unsigned char  host_buf[64];
static unsigned int   host_len;
static unsigned int   host_rdy;
static unsigned int   host_import;
static unsigned int   host_rxcb;
static unsigned char  host_tx[32];
static int            host_tx_len;

static void host_send_optical(const unsigned char *b, int n) {
    if (n > (int)sizeof host_tx) n = sizeof host_tx;
    memcpy(host_tx, b, n);
    host_tx_len = n;
}

#define KMP_BUF        ((volatile unsigned char *)host_buf)
#define KMP_LEN        ((volatile unsigned int  *)&host_len)
#define KMP_RDY        ((volatile unsigned int  *)&host_rdy)
#define KMP_IMPORT     ((volatile unsigned int  *)&host_import)
#define KMP_RXCB_SLOT  ((volatile unsigned int  *)&host_rxcb)
#define KMP_TX_ADDR    0u
#define vendor_send_optical host_send_optical

// kmp_set_baud_1200() writes an MMIO register that does not exist on the host; the harness
// exercises the protocol layer (collect / destuff / CRC / parse), not the SAU retune.
#define KMP_NO_BAUD_WRITE 1

#include "../kmp_hook.c"

// ---------------------------------------------------------------------------------------------
static int failures = 0;

static void check(const char *what, unsigned long got, unsigned long want) {
    if (got == want) {
        printf("  PASS  %-38s = %lu\n", what, got);
    } else {
        printf("  FAIL  %-38s = %lu (expected %lu)\n", what, got, want);
        failures++;
    }
}

static void feed(const unsigned char *bytes, int n) {
    host_len = 0; host_rdy = 0; host_import = 0;
    for (int i = 0; i < n; i++) kmp_rx_byte(bytes[i]);
}

int main(void) {
    // The real captured Multical 21 reply to GetRegister(0x0044):
    //   40 3F 10 00 44 28 04 43 00 04 CB 13 29 23 0D
    //   ^start ^addr ^cid ^reg   ^unit=0x28(m3)
    //                                ^len=4 ^sign/exp=0x43 -> exp -3
    //                                          ^mantissa 0x0004CB13 = 314131
    //                                                       ^crc  ^stop
    //   => 314.131 m3 => 314131 litres
    static const unsigned char reply[] = {
        0x40,0x3F,0x10,0x00,0x44,0x28,0x04,0x43,0x00,0x04,0xCB,0x13,0x29,0x23,0x0D
    };

    printf("kmp_hook decoder — host tests\n");

    printf("\n[1] real captured reply\n");
    feed(reply, sizeof reply);
    check("frame ready", host_rdy, 1);
    check("kmp_decode() accepted", (unsigned)kmp_decode(), 1);
    check("volume (litres)", host_import, 314131);

    printf("\n[2] CRC must cover from the ADDRESS byte, not the 0x40 start\n");
    check("crc over frame incl 0x40 (wrong)", kmp_crc(reply, 14), 0x2BBF);
    check("crc over frame from addr (right)", kmp_crc(reply + 1, 13), 0x0000);

    printf("\n[3] corrupted payload must be rejected\n");
    {
        unsigned char bad[sizeof reply];
        memcpy(bad, reply, sizeof reply);
        bad[10] ^= 0xFF;                      // flip a mantissa byte
        feed(bad, sizeof bad);
        check("kmp_decode() rejected", (unsigned)kmp_decode(), 0);
    }

    printf("\n[4] byte-stuffed reply decodes identically\n");
    {
        // Re-stuff mantissa byte 0x13 as 0x1B,0xEC to prove the destuffer runs. (0x13 is not a
        // reserved byte, but the destuffer is unconditional on 0x1B, so this is a valid probe.)
        unsigned char st[sizeof reply + 1]; int n = 0;
        for (unsigned i = 0; i < sizeof reply; i++) {
            if (i == 11) { st[n++] = 0x1B; st[n++] = (unsigned char)(reply[i] ^ 0xFF); }
            else         { st[n++] = reply[i]; }
        }
        feed(st, n);
        check("kmp_decode() accepted", (unsigned)kmp_decode(), 1);
        check("volume (litres)", host_import, 314131);
    }

    printf("\n[5] request frame the hook transmits\n");
    check("query length", (unsigned)sizeof kmp_query_v1, 9);
    check("start byte is 0x80 (master->meter)", kmp_query_v1[0], 0x80);
    check("baked-in CRC matches computed", kmp_crc(kmp_query_v1 + 1, 5), 0x4DC0);
    check("crc bytes in frame", ((unsigned)kmp_query_v1[6] << 8) | kmp_query_v1[7], 0x4DC0);
    check("stop byte", kmp_query_v1[8], 0x0D);

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
