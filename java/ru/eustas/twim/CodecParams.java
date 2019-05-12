package ru.eustas.twim;

public class CodecParams {
  public static final int MAX_LEVEL = 7;

  private static final int MAX_F1 = 4;
  private static final int MAX_F2 = 5;
  private static final int MAX_F3 = 5;
  private static final int MAX_F4 = 5;

  public static final int MAX_PARTITION_CODE = MAX_F1 * MAX_F2 * MAX_F3 * MAX_F4;
  public static final int MAX_COLOR_CODE = 30;

  public static final int MAX_CODE = MAX_PARTITION_CODE * MAX_COLOR_CODE;

  private static final int[] COLOR_QUANT = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
      24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54};
  private static final int[] SCALE_STEP = {1000, 631, 399, 252, 159, 100};
  private static final int SCALE_STEP_FACTOR = 40;
  private static final int[] BASE_SCALE = {4, 9, 16, 25, 36};
  private static final int BASE_SCALE_FACTOR = 36;

  private final int[] levelScale = new int[MAX_LEVEL];
  public final int[] angleBits = new int[MAX_LEVEL];
  public final int[] colorQuant = new int[3];

  // Actually, that is image params...
  private final int width;
  private final int height;

  private int partitionCode;
  private int colorCode;

  public CodecParams(int width, int height) {
    this.width = width;
    this.height = height;
  }

  public void setColorCode(int code) {
    colorCode = code;
    int c1 = code;
    int c2 = c1;
    int c3 = c1;
    colorQuant[0] = COLOR_QUANT[c1];
    colorQuant[1] = COLOR_QUANT[c2];
    colorQuant[2] = COLOR_QUANT[c3];
  }

  public void setPartitionCode(int code) {
    partitionCode = code;
    int f1 = code % MAX_F1;
    code = code / MAX_F1;
    int f2 = code % MAX_F2;
    code = code / MAX_F2;
    int f3 = code % MAX_F3;
    code = code / MAX_F3;
    int f4 = code % MAX_F4;

    int scale = (width * width + height * height) * BASE_SCALE[f2];
    for (int i = 0; i < MAX_LEVEL; ++i) {
      levelScale[i] = scale / BASE_SCALE_FACTOR;
      scale = (scale * SCALE_STEP_FACTOR) / SCALE_STEP[f3];
    }

    int bits = 9 - f1;
    for (int i = 0; i < MAX_LEVEL; ++i) {
      angleBits[i] = Math.max(bits - i - (i * f4) / 2, 0);
    }
  }

  public int getCode() {
    return partitionCode + colorCode * MAX_PARTITION_CODE;
  }

  void setCode(int code) {
    setPartitionCode(code % MAX_PARTITION_CODE);
    code /= MAX_PARTITION_CODE;
    setColorCode(code % MAX_COLOR_CODE);
  }

  public int getLevel(int[] region) {
    final int count3 = region[region.length - 1] * 3;
    if (count3 == 0) {
      return -1;
    }

    // TODO(eustas): perhaps box area is not the best value for level calculation.
    int min_y = height + 1;
    int max_y = -1;
    int min_x = width + 1;
    int max_x = -1;
    for (int i = 0; i < count3; i += 3) {
      int y = region[i];
      int x0 = region[i + 1];
      int x1 = region[i + 2];
      min_y = Math.min(min_y, y);
      max_y = Math.max(max_y, y);
      min_x = Math.min(min_x, x0);
      max_x = Math.max(max_x, x1);
    }

    int dx = max_x - min_x;
    int dy = max_y + 1 - min_y;
    int d = dx * dx + dy * dy;
    for (int i = 0; i < MAX_LEVEL; ++i) {
      if (d >= levelScale[i]) {
        return i;
      }
    }
    return MAX_LEVEL - 1;
  }
}