// ChirpStack JS codec for the OBI-bridge LoRaWAN uplink (FPort 10). Mirrors
// open_obi_energy_meter/include/payload_codec.h -- keep both in sync if you change the layout.
//
// Frame = [schema_version u8] + N x 15-byte reader entries:
//   reader_index(u8) import_Wh(u32 BE) export_Wh(u32 BE) power_W(u32 BE) battery_raw(u8) flags(u8)

function decodeUplink(input) {
  var b = input.bytes;
  var warnings = [];
  if (b.length < 1) return { data: {}, warnings: ["empty payload"] };

  var version = b[0];
  if (version !== 1) warnings.push("unknown schema_version " + version);

  var readers = [];
  var off = 1;
  var ENTRY = 15;
  while (off + ENTRY <= b.length) {
    var readerIndex = b[off];
    var importWh = readBE32(b, off + 1);
    var exportWh = readBE32(b, off + 5);
    var powerRaw = readBE32(b, off + 9);
    var batteryRaw = b[off + 13];
    var flags = b[off + 14];

    readers.push({
      reader_index: readerIndex,
      import_Wh: importWh,
      export_Wh: exportWh,
      power_W: powerRaw === 0x7FFFFFFF ? null : toSigned32(powerRaw),
      battery_mV: batteryRaw * 20,
      infrared: (flags & 0x01) !== 0,
      lowpower: (flags & 0x02) !== 0,
      timesync: (flags & 0x04) !== 0,
    });
    off += ENTRY;
  }

  return {
    data: {
      schema_version: version,
      readers: readers,
    },
    warnings: warnings,
  };
}

// Optional WP4 downlink helper: set a reader's OBI upload interval.
// input.data = { reader_index: u8, interval_s: u16 }
function encodeDownlink(input) {
  var idx = input.data.reader_index & 0xFF;
  var secs = input.data.interval_s & 0xFFFF;
  return {
    bytes: [idx, (secs >> 8) & 0xFF, secs & 0xFF],
    fPort: 20,
  };
}

function readBE32(b, i) {
  return ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) >>> 0;
}
function toSigned32(u) {
  return u > 0x7FFFFFFF ? u - 0x100000000 : u;
}
