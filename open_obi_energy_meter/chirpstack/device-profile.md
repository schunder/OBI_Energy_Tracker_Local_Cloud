# ChirpStack device profile — OBI-bridge electricity gateway (WP4)

Manual steps against the municipal ChirpStack instance (no API access from this environment,
so these are **not** automated — run them yourself once a unit is built and flashed).

## 1. Device profile

- Region: **EU868**
- MAC version: **LoRaWAN 1.0.3** (the firmware's `beginOTAA()` passes `nwkKey = nullptr`, which
  RadioLib's LoRaWANNode treats as "stay in rev 0 / 1.0.x mode" — do not pick a 1.1 profile, the
  join will be rejected)
- Regional parameters revision: A (or match your Conduit's packet-forwarder config)
- ADR: **enabled** (firmware sets `setADR(true)` — see `lorawan_config.h::UseADR`)
- Uplink interval (for ChirpStack's own "device inactive" check): >= 300 s
  (`obilw::UplinkPeriodMs`, `include/lorawan_config.h`)
- Payload codec: **JS**, paste `codec.js` from this folder

## 2. Device registration

- DevEUI: printed at boot (`[lorawan] DevEUI = ...`) — built from the fleet convention
  (`FE-07-01-xx-xx-xx-xx-xx`; utility `0x07` = "electricity via OBI-protocol bridge", hardware
  `0x01` = Seeed XIAO ESP32-S3 + Wio-SX1262). Do NOT reuse the placeholder in
  `lorawan_secrets.h.example`.
- JoinEUI / AppKey: from `include/lorawan_secrets.h` (copy from the `.example`, git-ignored —
  fill with values generated in ChirpStack when you register the device)

## 3. Decoded object (from codec.js)

```json
{
  "schema_version": 1,
  "readers": [
    {
      "reader_index": 0,
      "import_Wh": 123456,
      "export_Wh": 789,
      "power_W": 340,
      "battery_mV": 3000,
      "infrared": true,
      "lowpower": false,
      "timesync": true
    }
  ]
}
```

`reader_index` (0..9) is a **stable per-reader slot** assigned by the gateway on first sight of
each physical OBI reader (persisted in its NVS, namespace `obislot`) — it is NOT the reader's
UUID. To map a slot back to a physical meter, check the gateway's own web dashboard (`/`) or
`/radio` page, which show the full 16-byte UUID per reader; record the slot->UUID mapping when
you commission each reader (matches the plan's WP6 provisioning runbook).

## 4. Optional: remote upload-interval control (WP4.4)

Downlink on **FPort 20**, 3 bytes: `[reader_index u8][interval_s u16 BE]`. Encode with
`codec.js`'s `encodeDownlink()`, e.g. `{ "reader_index": 0, "interval_s": 60 }`. Delivered in the
RX window of the gateway's *next* scheduled uplink (Class A) — expect up to
`obilw::UplinkPeriodMs` (default 300 s) latency.

## 5. Ingest wiring

Route the decoded object into the same n8n -> Supabase -> Grafana path the water/gas nodes use
(see `memory/meter-data-pipeline.md`), but as its **own measurement/tag** (e.g. `electricity_obi`)
— this is a separate node family from the Wasserverband water/wM-Bus deployment, not a
replacement for it.
