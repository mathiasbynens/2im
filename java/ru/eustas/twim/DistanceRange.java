package ru.eustas.twim;

class DistanceRange {
  int min;
  int max;
  int numLines;
  int lineQuant;

  void update(int[] region, int angle, CodecParams cp) {
    final int step = region.length / 3;
    final int count = region[region.length - 1];
    if (count == 0) {
      throw new IllegalStateException("empty region");
    }

    // Note: SinCos.SIN is all positive -> d0 <= d1
    int nx = SinCos.SIN[angle];
    int ny = SinCos.COS[angle];
    int mi = Integer.MAX_VALUE;
    int ma = Integer.MIN_VALUE;
    for (int i = 0; i < count; i++) {
      int y = region[i];
      int x0 = region[step + i];
      int x1 = region[2 * step + i] - 1;
      int d0 = ny * y + nx * x0;
      int d1 = ny * y + nx * x1;
      mi = Math.min(mi, d0);
      ma = Math.max(ma, d1);
    }
    this.min = mi;
    this.max = ma;

    int lineQuant = cp.getLineQuant();
    while (true) {
      this.numLines = (ma - mi) / lineQuant;
      if (this.numLines > cp.lineLimit) {
        lineQuant += lineQuant >> 4;
      } else {
        break;
      }
    }
    this.lineQuant = lineQuant;
  }

  int distance(int line) {
    if (numLines > 1) {
      return min + ((max - min) - (numLines - 1) * lineQuant) / 2 + lineQuant * line;
    } else {
      return (max + min) / 2;
    }
  }
}
