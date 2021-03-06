package ru.eustas.twim;

import static ru.eustas.twim.RangeCode.HEAD_START;
import static ru.eustas.twim.RangeCode.NIBBLE_BITS;
import static ru.eustas.twim.RangeCode.NIBBLE_MASK;
import static ru.eustas.twim.RangeCode.NUM_NIBBLES;
import static ru.eustas.twim.RangeCode.RANGE_LIMIT_MASK;
import static ru.eustas.twim.RangeCode.VALUE_MASK;

final class RangeDecoder {
  private long low;
  private long range = VALUE_MASK;
  private long code;
  private final byte[] data;
  private int offset;

  RangeDecoder(byte[] data) {
    this.data = data;
    long code = 0;
    for (int i = 0; i < NUM_NIBBLES; ++i) {
      code = (code << NIBBLE_BITS) | readNibble();
    }
    this.code = code;
  }

  static int readNumber(RangeDecoder src, int max) {
    if (max < 2) return 0;
    int result = src.currentCount(max);
    src.removeRange(result, result + 1);
    return result;
  }

  static int readSize(RangeDecoder src) {
    int plus = -1;
    int bits = 0;
    while ((plus <= 0) || (readNumber(src, 2) == 1)) {
      plus = (plus + 1) << 3;
      int extra = readNumber(src, 8);
      bits = (bits << 3) + extra;
    }
    return bits + plus;
  }

  private int readNibble() {
    return (offset < data.length) ? (data[offset++] & NIBBLE_MASK) : 0;
  }

  final void removeRange(int bottom, int top) {
    low += bottom * range;
    range *= top - bottom;
    while (true) {
      if ((low ^ (low + range - 1)) >= HEAD_START) {
        if (range > RANGE_LIMIT_MASK) {
          break;
        }
        range = -low & VALUE_MASK;
      }
      code = ((code << NIBBLE_BITS) & VALUE_MASK) | readNibble();
      range = ((range << NIBBLE_BITS) & VALUE_MASK) | NIBBLE_MASK;
      low = (low << NIBBLE_BITS) & VALUE_MASK;
    }
  }

  final int currentCount(int totalRange) {
    range /= totalRange;
    if (range == 0) {
      throw new IllegalArgumentException("corrupted input");
    }
    int result = (int) ((code - low) / range);
    if (result < 0 || result > totalRange) {
      throw new IllegalArgumentException("corrupted input");
    }
    return result;
  }
}
