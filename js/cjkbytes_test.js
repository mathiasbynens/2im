goog.require('goog.testing.asserts');
goog.require('goog.testing.jsunit');

/**
 * @param{!Array<number>} bytes
 */
function checkRoundtrip(bytes) {
  let codePoints = cjkbytes.encode(bytes);
  let decoded = cjkbytes.decode(codePoints);
  assertArrayEquals(bytes, decoded);
}

function testEmpty() {
  checkRoundtrip([]);
}

function testOneByte() {
  checkRoundtrip([42]);
}

function testZeroTruncation() {
  let codePoints = cjkbytes.encode([42, 0, 0, 0, 0, 0, 43, 0, 0, 0]);
  let decoded = cjkbytes.decode(codePoints);
  assertArrayEquals([42, 0, 0, 0, 0, 0, 43], decoded);
}

function testRandom() {
  checkRoundtrip([
    0x20, 0x4c, 0x75, 0x2f, 0xa1, 0xd6, 0xe3, 0xb4, 0x21, 0x1f, 0x43, 0x77, 0x1d, 0xc8,
    0x4b, 0x7c, 0xfa, 0x6d, 0xf6, 0x6c, 0x8a, 0x78, 0x9c, 0x47, 0x60, 0xdd, 0xae, 0x90,
    0xb8, 0xee, 0x38, 0xb5, 0x07, 0x9e, 0x2a, 0x3d, 0xb0, 0xc0, 0x4e, 0x69, 0xcf, 0x32
  ]);
}
