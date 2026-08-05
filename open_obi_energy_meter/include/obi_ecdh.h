// obi_ecdh.h — P-256 (secp256r1) ECDH via the ESP32's built-in mbedTLS.
// The reader and gateway each send a raw 64-byte public key (X||Y, no 0x04 prefix);
// the TEA key is the first 16 bytes of the 32-byte shared X coordinate (no KDF).
#pragma once
#include <Arduino.h>

// generate an ephemeral keypair; writes our 64-byte public key (X||Y). false on error.
bool obi_ecdh_generate(uint8_t out_pub64[64]);

// Load a previously persisted private key (32-byte scalar) and derive our public key from it.
// false if the scalar is unusable.
bool obi_ecdh_load(const uint8_t priv32[32], uint8_t out_pub64[64]);

// Export the current private key so it can be persisted. false on error.
bool obi_ecdh_export(uint8_t out_priv32[32]);

// compute the shared secret against the peer's 64-byte public key; writes 32-byte shared X.
// TEA key = out_secret32[0..15].
bool obi_ecdh_compute(const uint8_t peer_pub64[64], uint8_t out_secret32[32]);
