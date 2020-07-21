//#include "encoder.h"

#include <cmath>
#include <future>
#include <hwy/highway.h>
#include <list>
#include <queue>
#include <vector>

#include "encoder.h"
#include "codec_params.h"
#include "distance_range.h"
#include "image.h"
#include "platform.h"
#include "region.h"
#include "xrange_encoder.h"

namespace twim {

using ::twim::Encoder::Result;
using ::twim::Encoder::Variant;

// TODO(eustas): move to common header
// TODO(eustas): adjust to SIMD implementation
constexpr size_t kDefaultAlign = 32;

/*
 * https://xkcd.com/221/
 *
 * Chosen by fair dice roll. Guaranteed to be random.
 */
constexpr float kRandom[64] = {
    0.196761043301f, 0.735187971367f, 0.240889315713f, 0.322607869281f,
    0.042645685482f, 0.100931237742f, 0.436030039105f, 0.949386184145f,
    0.391385426476f, 0.693787004045f, 0.957348258524f, 0.401165324581f,
    0.200612435526f, 0.774156296613f, 0.702329904898f, 0.794307087431f,
    0.913297998115f, 0.418554329021f, 0.305256297017f, 0.447050968372f,
    0.275901615626f, 0.423281943403f, 0.222083232637f, 0.259557717968f,
    0.134780340092f, 0.624659579778f, 0.459554804245f, 0.629270576873f,
    0.728531198516f, 0.270863443275f, 0.730693946899f, 0.958482839910f,
    0.597229071250f, 0.020570415805f, 0.876483717523f, 0.734546837759f,
    0.548824137588f, 0.628979430442f, 0.813059781398f, 0.145038174534f,
    0.174453058546f, 0.195531684379f, 0.127489363209f, 0.878269052109f,
    0.990909412408f, 0.109277869329f, 0.295625366456f, 0.247012273577f,
    0.508121083167f, 0.875779718247f, 0.863389034205f, 0.663539415689f,
    0.069178093049f, 0.859564180486f, 0.560775815455f, 0.039552534504f,
    0.989061239776f, 0.815374917179f, 0.951061519055f, 0.211362121050f,
    0.255234747636f, 0.047947586972f, 0.520984579718f, 0.399461090480f};

struct Stats {
  /* sum_r, sum_g, sum_b, pixel_count */
  ALIGNED_16 float values[4];
};

INLINE float rgb(const Stats& stats, size_t c) { return stats.values[c]; }

INLINE float pixelCount(const Stats& stats) { return stats.values[3]; }

class UberCache {
 public:
  const uint32_t width;
  const uint32_t height;
  const uint32_t stride;
  /* Cumulative sums. Extra column with total sum. */
  Owned<Vector<float>> sum;

  float rgb2[3] = {0.0f};

  explicit UberCache(const Image& src)
      : width(src.width),
        height(src.height),
        // 4 == [r, g, b, count].length
        stride(vecSize<float, kDefaultAlign>(4 * (src.width + 1))),
        sum(allocVector<float, kDefaultAlign>(stride * src.height)) {
    float* RESTRICT sum = this->sum->data();
    for (size_t y = 0; y < src.height; ++y) {
      float row_rgb2[3] = {0.0f};
      size_t src_row_offset = y * src.width;
      const uint8_t* RESTRICT r_row = src.r.data() + src_row_offset;
      const uint8_t* RESTRICT g_row = src.g.data() + src_row_offset;
      const uint8_t* RESTRICT b_row = src.b.data() + src_row_offset;
      size_t dstRowOffset = y * this->stride;
      for (size_t i = 0; i < 4; ++i) sum[dstRowOffset + i] = 0.0f;
      for (size_t x = 0; x < src.width; ++x) {
        size_t dstOffset = dstRowOffset + 4 * x;
        float r = r_row[x];
        float g = g_row[x];
        float b = b_row[x];
        sum[dstOffset + 4] = sum[dstOffset + 0] + r;
        sum[dstOffset + 5] = sum[dstOffset + 1] + g;
        sum[dstOffset + 6] = sum[dstOffset + 2] + b;
        sum[dstOffset + 7] = sum[dstOffset + 3] + 1.0f;
        row_rgb2[0] += r * r;
        row_rgb2[1] += g * g;
        row_rgb2[2] += b * b;
      }
      for (size_t c = 0; c < 3; ++c) this->rgb2[c] += row_rgb2[c];
    }
  }
};

class Cache {
 public:
  const UberCache* uber;
  Stats stats[CodecParams::kMaxLineLimit + 3];

  uint32_t count;
  Owned<Vector<int32_t>> row_offset;
  Owned<Vector<float>> y;
  Owned<Vector<int32_t>> x0;
  Owned<Vector<int32_t>> x1;
  Owned<Vector<int32_t>> x;

  explicit Cache(const UberCache& uber)
      : uber(&uber),
        row_offset(allocVector<int32_t, kDefaultAlign>(uber.height)),
        y(allocVector<float, kDefaultAlign>(uber.height)),
        x0(allocVector<int32_t, kDefaultAlign>(uber.height)),
        x1(allocVector<int32_t, kDefaultAlign>(uber.height)),
        x(allocVector<int32_t, kDefaultAlign>(uber.height)) {}
};

class Fragment {
 public:
  Owned<Vector<int32_t>> region;
  std::unique_ptr<Fragment> left_child;
  std::unique_ptr<Fragment> right_child;

  Stats stats;

  // Subdivision.
  uint32_t ordinal = 0x7FFFFFFF;
  int32_t level;
  uint32_t best_angle_code;
  uint32_t best_line;
  float best_score;
  uint32_t best_num_lines;
  float best_cost;

  Fragment(Fragment&&) = default;
  Fragment& operator=(Fragment&&) = default;
  Fragment(const Fragment&) = delete;
  Fragment& operator=(const Fragment&) = delete;

  explicit Fragment(Owned<Vector<int32_t>>&& region)
    : region(std::move(region)) {}

  template <typename EntropyEncoder>
  NOINLINE void encode(EntropyEncoder* dst, const CodecParams& cp, bool is_leaf,
                       const float* RESTRICT palette,
                       std::vector<Fragment*>* children);
};

class Partition {
 public:
  Partition(const UberCache& uber, const CodecParams& cp, size_t target_size);

  const std::vector<Fragment*>* getPartition() const;

  // CodecParams should be the same as passed to constructor; only color code
  // is allowed to be different.
  uint32_t subpartition(const CodecParams& cp, uint32_t target_size) const;

 private:
  Cache cache;
  Fragment root;
  std::vector<Fragment*> partition;
};

float bitCost(int32_t range) {
  static float kInvLog2 = 1.0f / std::log(2.0f);
  return static_cast<float>(std::log(range) * kInvLog2);
}

struct FragmentComparator {
  bool operator()(const Fragment* left, const Fragment* right) const {
    return left->best_score < right->best_score;
  }
};

Fragment makeRoot(uint32_t width, uint32_t height) {
  uint32_t step = vecSize<int32_t, kDefaultAlign>(height);
  Owned<Vector<int32_t>> root = allocVector<int32_t, kDefaultAlign>(3 * step);
  int32_t* RESTRICT data = root->data();
  for (uint32_t y = 0; y < height; ++y) {
    data[y] = y;
    data[step + y] = 0;
    data[2 * step + y] = width;
  }
  root->len = height;
  return Fragment(std::move(root));
}

#include <hwy/before_namespace-inl.h>
#include <hwy/begin_target-inl.h>

constexpr const HWY_FULL(uint32_t) kDu32;

HWY_ALIGN static const uint32_t kIotaArray[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const U32xN kIota = Load(kDu32, kIotaArray);
static const U32xN kStep = Set(kDu32, Lanes(kDu32));

INLINE void reset(Stats* stats) {
  constexpr HWY_CAPPED(float, 4) df;
  Store(Zero(df), df, stats->values);
}

INLINE void diff(Stats* diff, const Stats& plus, const Stats& minus) {
  constexpr HWY_CAPPED(float, 4) df;
  const auto p = Load(df, plus.values);
  const auto m = Load(df, minus.values);
  Store(p - m, df, diff->values);
}

INLINE void copy(Stats* to, const Stats& from) {
  constexpr HWY_CAPPED(float, 4) df;
  Store(Load(df, from.values), df, to->values);
}

void INLINE updateGeHorizontal(Cache* cache, int32_t d) {
  int32_t ny = SinCos::kCos[0];
  float dny = d / (float)ny;  // TODO: precalculate
  size_t count = cache->count;
  int32_t* RESTRICT row_offset = cache->row_offset->data();
  float* RESTRICT region_y = cache->y->data();
  int32_t* RESTRICT region_x0 = cache->x0->data();
  int32_t* RESTRICT region_x1 = cache->x1->data();
  int32_t* RESTRICT region_x = cache->x->data();

  for (size_t i = 0; i < count; i++) {
    float y = region_y[i];
    int32_t offset = row_offset[i];
    int32_t x0 = region_x0[i];
    int32_t x1 = region_x1[i];
    int32_t x = (y < dny) ? x1 : x0;
    int32_t x_off = 4 * x + offset;
    region_x[i] = x_off;
  }
}

INLINE void updateGeGeneric(Cache* cache, int32_t angle, int32_t d) {
  constexpr HWY_FULL(float) df;
  constexpr HWY_FULL(int32_t) di32;

  float m_ny_nx = SinCos::kMinusCot[angle];
  float d_nx = static_cast<float>(d * SinCos::kInvSin[angle] + 0.5);
  int32_t* RESTRICT row_offset = cache->row_offset->data();
  float* RESTRICT region_y = cache->y->data();
  int32_t* RESTRICT region_x0 = cache->x0->data();
  int32_t* RESTRICT region_x1 = cache->x1->data();
  int32_t* RESTRICT region_x = cache->x->data();

  const auto k4 = Set(di32, 4);
  const auto d_nx_ = Set(df, d_nx);
  const auto m_ny_nx_ = Set(df, m_ny_nx);
  const size_t count = cache->count;
  for (size_t i = 0; i < count; i += Lanes(df)) {
    const auto y = Load(df, region_y + i);
    const auto offset = Load(di32, row_offset + i);
    const auto xf = MulAdd(y, m_ny_nx_, d_nx_);
    const auto xi = ConvertTo(di32, xf);
    const auto x0 = Load(di32, region_x0 + i);
    const auto x1 = Load(di32, region_x1 + i);
    const auto x = Min(x1, Max(xi, x0));
    const auto x_off = k4 * x + offset;
    Store(x_off, di32, region_x + i);
  }
}

NOINLINE void updateGe(Cache* cache, int angle, int d) {
  if (angle == 0) {
    updateGeHorizontal(cache, d);
  } else {
    updateGeGeneric(cache, angle, d);
  }
}

NOINLINE float score(const Stats& whole, const Stats& left,
                     const Stats& right) {
  constexpr HWY_CAPPED(float, 4) df;

  if ((pixelCount(left) <= 0.0f) || (pixelCount(right) <= 0.0f)) return 0.0f;

  const auto k1 = Set(df, 1.0f);
  HWY_ALIGN float c_array[4] = {pixelCount(whole), pixelCount(left),
                                pixelCount(right), 1.0f};
  const auto c = Load(df, c_array);
  const auto inv_c = k1 / c;

  const auto k2 = Set(df, 2.0f);
  const auto whole_values = Load(df, whole.values);
  const auto whole_average = whole_values * Broadcast<0>(inv_c);

  const auto left_values = Load(df, left.values);
  const auto left_pixel_count = Set(df, pixelCount(left));
  const auto left_average = left_values *  Broadcast<1>(inv_c);
  const auto left_plus = whole_average + left_average;
  const auto left_minus = whole_average - left_average;
  const auto left_a = left_pixel_count * left_plus;
  const auto left_b = k2 * left_values;
  const auto left_sum = left_minus * (left_a - left_b);

  const auto right_values = Load(df, right.values);
  const auto right_pixel_count = Set(df, pixelCount(right));
  const auto right_average = right_values *  Broadcast<2>(inv_c);
  const auto right_plus = whole_average + right_average;
  const auto right_minus = whole_average - right_average;
  const auto right_a = right_pixel_count * right_plus;
  const auto right_b = k2 * right_values;
  const auto right_sum = right_minus * (right_a - right_b);

  return GetLane(SumOfLanes(left_sum + right_sum));
}

/* |length| is for scalar mode; vectorized versions look after the end of
   input up to complete vector, where "big" fillers have to be placed. */
INLINE uint32_t chooseMin(const float* RESTRICT values, size_t length) {
  constexpr HWY_FULL(float) df;
  constexpr HWY_FULL(uint32_t) du32;

#if HWY_TARGET == HWY_SCALAR
  bool use_scalar = true;
#else
  bool use_scalar = length < 4;
#endif
  if (use_scalar) {
    return std::distance(values, std::min_element(values, values + length));
  }

  auto idx = kIota;
  auto bestIdx = idx;
  auto bestValue = Load(df, values);
  for (size_t i = Lanes(df); i < length; i += Lanes(df)) {
    idx = idx + kStep;
    const auto value = Load(df, values + i);
    const auto selector = value < bestValue;
    bestValue = IfThenElse(selector, value, bestValue);
    bestIdx = IfThenElse(MaskFromVec(BitCast(du32, VecFromMask(selector))), idx,
                         bestIdx);
  }

#if HWY_TARGET == HWY_AVX2
  {
    const auto shuffledIdx = ConcatLowerUpper(bestIdx, bestIdx);
    const auto shuffledValue = ConcatLowerUpper(bestValue, bestValue);
    const auto selector = shuffledValue < bestValue;
    bestValue = IfThenElse(selector, shuffledValue, bestValue);
    bestIdx = IfThenElse(MaskFromVec(BitCast(du32, VecFromMask(selector))),
                         shuffledIdx, bestIdx);
  }
#endif
#if HWY_TARGET != HWY_SCALAR
  {
    const auto shuffledIdx = Shuffle1032(bestIdx);
    const auto shuffledValue = Shuffle1032(bestValue);
    const auto selector = shuffledValue < bestValue;
    bestValue = IfThenElse(selector, shuffledValue, bestValue);
    bestIdx = IfThenElse(MaskFromVec(BitCast(du32, VecFromMask(selector))),
                         shuffledIdx, bestIdx);
  }
  {
    const auto shuffledIdx = Shuffle0321(bestIdx);
    const auto shuffledValue = Shuffle0321(bestValue);
    const auto selector = shuffledValue < bestValue;
    // bestValue = IfThenElse(selector, shuffledValue, bestValue);
    bestIdx = IfThenElse(MaskFromVec(BitCast(du32, VecFromMask(selector))),
                         shuffledIdx, bestIdx);
  }
#endif
  return GetLane(bestIdx);
}

INLINE uint32_t chooseColor(float r, float g, float b,
                            const float* RESTRICT palette_r,
                            const float* RESTRICT palette_g,
                            const float* RESTRICT palette_b,
                            uint32_t palette_size, float* RESTRICT distance2) {
  constexpr HWY_FULL(float) df;
  const size_t m = palette_size;

  HWY_ALIGN float d2[32];

  const auto cr = Set(df, r);
  const auto cg = Set(df, g);
  const auto cb = Set(df, b);

  for (size_t i = 0; i < m; i += Lanes(df)) {
    const auto dr = cr - Load(df, palette_r + i);
    const auto dg = cg - Load(df, palette_g + i);
    const auto db = cb - Load(df, palette_b + i);
    Store(dr * dr + dg * dg + db * db, df, d2 + i);
  }

  uint32_t best = chooseMin(d2, m);
  *distance2 = d2[best];
  return best;
}

INLINE void makePalette(const float* stats, float* RESTRICT palette,
                        float* RESTRICT storage,
                        uint32_t num_patches, uint32_t palette_size) {
  constexpr HWY_FULL(float) df;
  constexpr HWY_FULL(int32_t) di32;

  const uint32_t n = num_patches;
  const uint32_t m = palette_size;
  const size_t centers_step = vecSize<float, kDefaultAlign>(m);
  const size_t stats_step = vecSize<float, kDefaultAlign>(n);
  float* RESTRICT centers_r = palette + 0 * centers_step;
  float* RESTRICT centers_g = palette + 1 * centers_step;
  float* RESTRICT centers_b = palette + 2 * centers_step;
  float* RESTRICT centers_acc_r = storage + 0 * centers_step;
  float* RESTRICT centers_acc_g = storage + 1 * centers_step;
  float* RESTRICT centers_acc_b = storage + 2 * centers_step;
  float* RESTRICT centers_acc_c = storage + 3 * centers_step;
  float* RESTRICT weights = storage + 0 * centers_step;
  const float* RESTRICT stats_r = stats + 0 * stats_step;
  const float* RESTRICT stats_g = stats + 1 * stats_step;
  const float* RESTRICT stats_b = stats + 2 * stats_step;
  const float* RESTRICT stats_c = stats + 3 * stats_step;
  const float* RESTRICT stats_wr = stats + 4 * stats_step;
  const float* RESTRICT stats_wg = stats + 5 * stats_step;
  const float* RESTRICT stats_wb = stats + 6 * stats_step;
  const auto k0 = Zero(df);
  const auto kOne = Set(df, 1.0f);

#if HWY_TARGET != HWY_SCALAR
  // Fill palette with nonsence for vectorized "chooseColor".
  const auto kInf = Set(df, 1024.0f);
  for (size_t j = 0; j < m; j += Lanes(df)) {
    Store(kInf, df, centers_r + j);
    Store(kInf, df, centers_g + j);
    Store(kInf, df, centers_b + j);
  }
#endif

  {
    // Choose one center uniformly at random from among the data points.
    float total = 0.0f;
    uint32_t i;
    for (i = 0; i < n; ++i) total += stats_c[i];
    float target = total * kRandom[0];
    float partial = 0.0f;
    for (i = 0; i < n; ++i) {
      partial += stats_c[i];
      if (partial >= target) break;
    }
    i = std::max(i, n - 1);
    centers_r[0] = stats_r[i];
    centers_g[0] = stats_g[i];
    centers_b[0] = stats_b[i];
  }

  for (uint32_t j = 1; j < m; ++j) {
    // D(x) is the distance to the nearest center.
    // Choose next with probability proportional to D(x)^2.
    uint32_t i;
    float total = 0.0f;
    for (i = 0; i < n; ++i) {
      float distance;
      chooseColor(stats_r[i], stats_g[i], stats_b[i], centers_r, centers_g,
                  centers_b, j, &distance);
      float weight = distance * stats_c[i];
      weights[i] = weight;
      total += weight;
    }
    float target = total * kRandom[j];
    float partial = 0;
    for (i = 0; i < n; ++i) {
      partial += weights[i];
      if (partial >= target) break;
    }
    i = std::max(i, n - 1);
    centers_r[j] = stats_r[i];
    centers_g[j] = stats_g[i];
    centers_b[j] = stats_b[i];
  }

  float last_score = 1e35f;
  while (true) {
    for (size_t j = 0; j < m; j += Lanes(df)) {
      Store(k0, df, centers_acc_r + j);
      Store(k0, df, centers_acc_g + j);
      Store(k0, df, centers_acc_b + j);
      Store(k0, df, centers_acc_c + j);
    }
    float score = 0.0f;
    for (size_t i = 0; i < n; ++i) {
      float d2;
      uint32_t index = chooseColor(stats_r[i], stats_g[i], stats_b[i],
                                   centers_r, centers_g, centers_b, m, &d2);
      float c = stats_c[i];
      score += d2 * c;
      centers_acc_r[index] += stats_wr[i];
      centers_acc_g[index] += stats_wg[i];
      centers_acc_b[index] += stats_wb[i];
      centers_acc_c[index] += c;
    }
    for (size_t j = 0; j < m; j += Lanes(df)) {
      const auto c = Load(df, centers_acc_c + j);
      const auto inv_c = kOne / Max(c, kOne);
      Store(Load(df, centers_acc_r + j) * inv_c, df, centers_r + j);
      Store(Load(df, centers_acc_g + j) * inv_c, df, centers_g + j);
      Store(Load(df, centers_acc_b + j) * inv_c, df, centers_b + j);
    }
    if (last_score - score < 1.0f) break;
    last_score = score;
  }

  // Round the pallete values.
  const auto k05 = Set(df, 0.5f);
  for (size_t j = 0; j < m; j += Lanes(df)) {
    const auto r =
        ConvertTo(df, ConvertTo(di32, Load(df, centers_r + j) + k05));
    const auto g =
        ConvertTo(df, ConvertTo(di32, Load(df, centers_g + j) + k05));
    const auto b =
        ConvertTo(df, ConvertTo(di32, Load(df, centers_b + j) + k05));
    Store(r, df, centers_r + j);
    Store(g, df, centers_g + j);
    Store(b, df, centers_b + j);
  }
}

NOINLINE Owned<Vector<float>> buildPalette(const Owned<Vector<float>>& patches,
                                           uint32_t palette_size) {
  uint32_t n = patches->len;
  uint32_t m = palette_size;
  uint32_t padded_m = vecSize<float, kDefaultAlign>(m);
  uint32_t palette_space = 3 * padded_m;
  uint32_t extra_space = std::max(4 * padded_m, ((m > 0) ? n : 1));
  Owned<Vector<float>> result =
      allocVector<float, kDefaultAlign>((m > 0) ? palette_space : 1);
  Owned<Vector<float>> extra = allocVector<float, kDefaultAlign>(extra_space);

  result->len = m;
  if (m > 0) makePalette(patches->data(), result->data(), extra->data(), n, m);

  return result;
}

NOINLINE Owned<Vector<float>> gatherPatches(
    const std::vector<Fragment*>* partition, uint32_t num_non_leaf) {
  constexpr HWY_FULL(float) df;
  const auto kOne = Set(df, 1.0f);

  /* In a binary tree the number of leaves is number of nodes plus one. */
  uint32_t n = num_non_leaf + 1;
  uint32_t stats_size = vecSize<float, kDefaultAlign>(n);
  Owned<Vector<float>> result =
      allocVector<float, kDefaultAlign>(7 * stats_size);
  float* RESTRICT stats = result->data();
  float* RESTRICT stats_r = stats + 0 * stats_size;
  float* RESTRICT stats_g = stats + 1 * stats_size;
  float* RESTRICT stats_b = stats + 2 * stats_size;
  float* RESTRICT stats_c = stats + 3 * stats_size;
  float* RESTRICT stats_wr = stats + 4 * stats_size;
  float* RESTRICT stats_wg = stats + 5 * stats_size;
  float* RESTRICT stats_wb = stats + 6 * stats_size;

  size_t pos = 0;
  const auto maybe_add_leaf = [&](Fragment* leaf) {
    if (leaf->ordinal < num_non_leaf) return;
    stats_wr[pos] = leaf->stats.values[0];
    stats_wg[pos] = leaf->stats.values[1];
    stats_wb[pos] = leaf->stats.values[2];
    stats_c[pos] = leaf->stats.values[3];
    pos++;
  };

  for (size_t i = 0; i < num_non_leaf; ++i) {
    Fragment* node = partition->at(i);
    maybe_add_leaf(node->left_child.get());
    maybe_add_leaf(node->right_child.get());
  }

  for (size_t i = 0; i < n; i += Lanes(df)) {
    const auto c = Load(df, stats_c + i);
    const auto inv_c = kOne / c;
    const auto r = Load(df, stats_wr + i);
    const auto g = Load(df, stats_wg + i);
    const auto b = Load(df, stats_wb + i);
    Store(r * inv_c, df, stats_r + i);
    Store(g * inv_c, df, stats_g + i);
    Store(b * inv_c, df, stats_b + i);
  }

  result->len = n;
  return result;
}

NOINLINE float simulateEncode(const Partition& partition_holder,
                              uint32_t target_size, const CodecParams& cp) {
  uint32_t num_non_leaf = partition_holder.subpartition(cp, target_size);
  if (num_non_leaf <= 1) {
    // Let's deal with flat image separately.
    return 1e35f;
  }
  Owned<Vector<float>> patches =
      gatherPatches(partition_holder.getPartition(), num_non_leaf);
  size_t patches_step = patches->capacity / 7;
  float* RESTRICT stats = patches->data();
  float* RESTRICT patches_r = stats + 0 * patches_step;
  float* RESTRICT patches_g = stats + 1 * patches_step;
  float* RESTRICT patches_b = stats + 2 * patches_step;
  float* RESTRICT patches_c = stats + 3 * patches_step;

  size_t n = patches->len;
  const uint32_t m = cp.palette_size;
  Owned<Vector<float>> palette = buildPalette(patches, m);
  const float* RESTRICT palette_r = palette->data();
  const size_t palette_step = vecSize<float, kDefaultAlign>(m);
  const float* RESTRICT palette_g = palette_r + palette_step;
  const float* RESTRICT palette_b = palette_g + palette_step;

  // pixel_score = (orig - color)^2
  // patch_score = sum(pixel_score)
  //             = sum(orig^2) + sum(color^2) - 2 * sum(orig * color)
  //             = sum(orig^2) + area * color ^ 2 - 2 * color * sum(orig)
  //             = sum(orig^2) + area * color * (color - 2 * avg(orig))
  // score = sum(patch_score)
  // sum(sum(orig^2)) is a constant => could be omitted from calculations

  float result[3] = {0.0f};

  typedef std::function<void(float* rgb)> ColorTransformer;
  auto accumulate_score = [&](const ColorTransformer& get_color) {
    for (size_t i = 0; i < n; ++i) {
      float orig[3] = {patches_r[i], patches_g[i], patches_b[i]};
      float color[3] = {orig[0], orig[1], orig[2]};
      float c = patches_c[i];
      get_color(color);
      for (size_t j = 0; j < 3; ++j) {
        // TODO(eustas): could use non-normalized patch color.
        result[j] += c * color[j] * (color[j] - 2.0f * orig[j]);
      }
    }
  };
  if (m == 0) {
    int32_t v_max = cp.color_quant - 1;
    const float quant = v_max / 255.0f;
    const float dequant = 255.0f / v_max;
    auto quantizer = [&](float* rgb) {
      for (size_t i = 0; i < 3; ++i) {
        rgb[i] = std::floorf(std::roundf(rgb[i] * quant) * dequant);
      }
    };
    accumulate_score(quantizer);
  } else {
    auto color_matcher = [&](float* rgb) {
      float dummy;
      uint32_t index = chooseColor(rgb[0], rgb[1], rgb[2], palette_r, palette_g,
                                   palette_b, m, &dummy);
      rgb[0] = palette_r[index];
      rgb[1] = palette_g[index];
      rgb[2] = palette_b[index];
    };
    accumulate_score(color_matcher);
  }

  return result[0] + result[1] + result[2];
}

void sumCache(const Cache* c, const int32_t* RESTRICT region_x, Stats* dst) {
  constexpr HWY_CAPPED(float, 4) df;

  size_t count = c->count;
  const int32_t* RESTRICT row_offset = c->row_offset->data();
  const float* RESTRICT sum = c->uber->sum->data();

  auto tmp = Zero(df);
  for (size_t i = 0; i < count; i++) {
    int32_t offset = row_offset[i] + 4 * region_x[i];
    tmp = tmp + Load(df, sum + offset);
  }
  Store(tmp, df, dst->values);
}

void sumCacheAbs(const Cache* c, const int32_t* RESTRICT region_x, Stats* dst) {
  constexpr HWY_CAPPED(float, 4) df;

  size_t count = c->count;
  const float* RESTRICT sum = c->uber->sum->data();

  auto tmp = Zero(df);
  for (size_t i = 0; i < count; i++) {
    tmp = tmp + Load(df, sum + region_x[i]);
  }
  Store(tmp, df, dst->values);
}

void INLINE prepareCache(Cache* c, Vector<int32_t>* region) {
  uint32_t count = region->len;
  uint32_t step = region->capacity / 3;
  uint32_t sum_stride = c->uber->stride;
  int32_t* RESTRICT data = region->data();
  float* RESTRICT y = c->y->data();
  int32_t* RESTRICT x0 = c->x0->data();
  int32_t* RESTRICT x1 = c->x1->data();
  int32_t* RESTRICT row_offset = c->row_offset->data();
  for (size_t i = 0; i < count; ++i) {
    int32_t row = data[i];
    y[i] = row;
    x0[i] = data[step + i];
    x1[i] = data[2 * step + i];
    row_offset[i] = row * sum_stride;
  }
  // TODO(eustas): 3 == kStride - 1
  while ((count & 3) != 0) {
    y[count] = 0;
    x0[count] = 0;
    x1[count] = 0;
    row_offset[count] = 0;
    count++;
  }
  c->count = count;
}

void NOINLINE findBestSubdivision(Fragment* f, Cache* cache, CodecParams cp) {
  Vector<int32_t>& region = *f->region;
  Stats& stats = f->stats;
  Stats plus;
  Stats minus;
  Stats* cache_stats = cache->stats;
  uint32_t level = cp.getLevel(region);
  // TODO(eustas): assert level is not kInvalid.
  uint32_t angle_max = 1u << cp.angle_bits[level];
  uint32_t angle_mult = (SinCos::kMaxAngle / angle_max);
  uint32_t best_angle_code = 0;
  uint32_t best_line = 0;
  float best_score = -1.0f;
  prepareCache(cache, &region);
  sumCache(cache, cache->x1->data(), &plus);
  sumCache(cache, cache->x0->data(), &minus);
  diff(&stats, plus, minus);

  // Find subdivision
  for (uint32_t angle_code = 0; angle_code < angle_max; ++angle_code) {
    int32_t angle = angle_code * angle_mult;
    DistanceRange distance_range(region, angle, cp);
    uint32_t num_lines = distance_range.num_lines;
    reset(&cache_stats[0]);
    for (uint32_t line = 0; line < num_lines; ++line) {
      updateGe(cache, angle, distance_range.distance(line));
      sumCacheAbs(cache, cache->x->data(), &minus);
      diff(&cache_stats[line + 1], plus, minus);
    }
    copy(&cache_stats[num_lines + 1], stats);

    for (uint32_t line = 0; line < num_lines; ++line) {
      Stats right;
      diff(&right, stats, cache_stats[line + 1]);
      float full_score = score(stats, cache_stats[line + 1], right);
      if (full_score > best_score) {
        best_angle_code = angle_code;
        best_line = line;
        best_score = full_score;
      }
    }
  }

  f->level = level;
  f->best_score = best_score;

  if (best_score < 0.0f) {
    f->best_cost = -1.0f;
  } else {
    DistanceRange distance_range(region, best_angle_code * angle_mult, cp);
    uint32_t child_step = vecSize<int32_t, kDefaultAlign>(region.len);
    f->left_child.reset(new Fragment(allocVector<int32_t, kDefaultAlign>(3 * child_step)));
    f->right_child.reset(new Fragment(allocVector<int32_t, kDefaultAlign>(3 * child_step)));
    Region::splitLine(region, best_angle_code * angle_mult,
                      distance_range.distance(best_line),
                      f->left_child->region.get(),
                      f->right_child->region.get());

    f->best_angle_code = best_angle_code;
    f->best_num_lines = distance_range.num_lines;
    f->best_line = best_line;
    f->best_cost =
        bitCost(NodeType::COUNT * angle_max * distance_range.num_lines);
  }
}

#include <hwy/end_target-inl.h>
#include <hwy/after_namespace-inl.h>

class SimulationTask {
 public:
  const uint32_t targetSize;
  Variant variant;
  const UberCache* uber;
  CodecParams cp;
  float bestSqe = 1e35f;
  uint32_t bestColorCode = (uint32_t)-1;
  std::unique_ptr<Partition> partitionHolder;

  SimulationTask(uint32_t targetSize, Variant variant, const UberCache& uber)
      : targetSize(targetSize),
        variant(variant),
        uber(&uber),
        cp(uber.width, uber.height) {}

  void run() {
    cp.setPartitionCode(variant.partitionCode);
    cp.line_limit = variant.lineLimit + 1;
    uint64_t colorOptions = variant.colorOptions;
    // TODO: color-options based taxes
    partitionHolder.reset(new Partition(*uber, cp, targetSize));
    for (uint32_t colorCode = 0; colorCode < CodecParams::kMaxColorCode;
         ++colorCode) {
      if (!(colorOptions & (1 << colorCode))) continue;
      cp.setColorCode(colorCode);
      float sqe = HWY_STATIC_DISPATCH(simulateEncode)(*partitionHolder, targetSize, cp);
      if (sqe < bestSqe) {
        bestSqe = sqe;
        bestColorCode = colorCode;
      }
    }
  }
};

class TaskExecutor {
 public:
  explicit TaskExecutor(size_t max_tasks) { tasks.reserve(max_tasks); }

  void run() {
    float bestSqe = 1e35f;
    size_t lastBestTask = (size_t)-1;

    while (true) {
      size_t myTask = nextTask++;
      if (myTask >= tasks.size()) return;
      SimulationTask& task = tasks[myTask];
      task.run();
      if (task.bestSqe < bestSqe) {
        bestSqe = task.bestSqe;
        if (lastBestTask < myTask) {
          tasks[lastBestTask].partitionHolder.reset();
        }
        lastBestTask = myTask;
      } else {
        task.partitionHolder.reset();
      }
    }
  }

  std::atomic<size_t> nextTask{0};
  std::vector<SimulationTask> tasks;
};

template <typename EntropyEncoder>
NOINLINE void Fragment::encode(EntropyEncoder* dst, const CodecParams& cp,
                               bool is_leaf, const float* RESTRICT palette,
                               std::vector<Fragment*>* children) {
  if (is_leaf) {
    EntropyEncoder::writeNumber(dst, NodeType::COUNT, NodeType::FILL);
    if (cp.palette_size == 0) {
      float quant = static_cast<float>(cp.color_quant - 1) / 255.0f;
      for (size_t c = 0; c < 3; ++c) {
        uint32_t v = static_cast<uint32_t>(
            roundf(quant * rgb(stats, c) / pixelCount(stats)));
        EntropyEncoder::writeNumber(dst, cp.color_quant, v);
      }
    } else {
      float r = rgb(stats, 0) / pixelCount(stats);
      float g = rgb(stats, 1) / pixelCount(stats);
      float b = rgb(stats, 2) / pixelCount(stats);
      size_t palette_step = vecSize<float, kDefaultAlign>(cp.palette_size);
      float  dummy;
      uint32_t index = HWY_STATIC_DISPATCH(chooseColor)(
          r, g, b, palette, palette + palette_step, palette + 2 * palette_step,
          cp.palette_size, &dummy);
      EntropyEncoder::writeNumber(dst, cp.palette_size, index);
    }
    return;
  }
  EntropyEncoder::writeNumber(dst, NodeType::COUNT, NodeType::HALF_PLANE);
  uint32_t maxAngle = 1u << cp.angle_bits[level];
  EntropyEncoder::writeNumber(dst, maxAngle, best_angle_code);
  EntropyEncoder::writeNumber(dst, best_num_lines, best_line);
  children->push_back(left_child.get());
  children->push_back(right_child.get());
}

template <typename EntropyEncoder>
std::vector<uint8_t> doEncode(uint32_t num_non_leaf,
                              const std::vector<Fragment*>* partition,
                              const CodecParams& cp,
                              const float* RESTRICT palette) {
  const uint32_t m = cp.palette_size;
  uint32_t n = num_non_leaf + 1;

  std::vector<Fragment*> queue;
  queue.reserve(n);
  queue.push_back(partition->at(0));

  EntropyEncoder dst;
  cp.write(&dst);

  size_t palette_step = vecSize<float, kDefaultAlign>(cp.palette_size);

  for (uint32_t j = 0; j < m; ++j) {
    for (size_t c = 0; c < 3; ++c) {
      uint32_t clr = static_cast<uint32_t>(palette[c * palette_step + j]);
      EntropyEncoder::writeNumber(&dst, 256, clr);
    }
  }

  size_t encoded = 0;
  while (encoded < queue.size()) {
    Fragment* node = queue[encoded++];
    bool is_leaf = (node->ordinal >= num_non_leaf);
    node->encode(&dst, cp, is_leaf, palette, &queue);
  }

  return dst.finish();
}

/**
 * Builds the space partition.
 *
 * Minimal color data cost is used.
 * Partition could be used to try multiple color quantization to see, which one
 * gives the best result.
 */
NOINLINE std::vector<Fragment*> buildPartition(Fragment* root,
                                               size_t size_limit,
                                               const CodecParams& cp,
                                               Cache* cache) {
  float tax = bitCost(NodeType::COUNT);
  float budget = size_limit * 8.0f - tax -
                 cp.getTax();  // Minus flat image cost.

  std::vector<Fragment*> result;
  std::priority_queue<Fragment*, std::vector<Fragment*>, FragmentComparator>
      queue;
  HWY_STATIC_DISPATCH(findBestSubdivision)(root, cache, cp);
  queue.push(root);
  while (!queue.empty()) {
    Fragment* candidate = queue.top();
    queue.pop();
    if (candidate->best_score < 0.0 || candidate->best_cost < 0.0) break;
    if (tax + candidate->best_cost <= budget) {
      budget -= tax + candidate->best_cost;
      candidate->ordinal = static_cast<uint32_t>(result.size());
      result.push_back(candidate);
      HWY_STATIC_DISPATCH(findBestSubdivision)(candidate->left_child.get(), cache, cp);
      queue.push(candidate->left_child.get());
      HWY_STATIC_DISPATCH(findBestSubdivision)(candidate->right_child.get(), cache, cp);
      queue.push(candidate->right_child.get());
    }
  }
  return result;
}

Partition::Partition(const UberCache& uber, const CodecParams& cp,
                     size_t target_size)
    : cache(uber),
      root(makeRoot(uber.width, uber.height)),
      partition(buildPartition(&root, target_size, cp, &cache)) {}

const std::vector<Fragment*>* Partition::getPartition() const {
  return &partition;
}

/* Returns the index of the first leaf node in partition. */
uint32_t Partition::subpartition(const CodecParams& cp,
                                 uint32_t target_size) const {
  const std::vector<Fragment*>* partition = getPartition();
  float node_tax = bitCost(NodeType::COUNT);
  float image_tax = cp.getTax();
  if (cp.palette_size == 0) {
    node_tax += 3.0f * bitCost(cp.color_quant);
  } else {
    node_tax += bitCost(cp.palette_size);
    image_tax += cp.palette_size * 3.0f * 8.0f;
  }
  float budget = 8.0f * target_size - 4.0f - image_tax - node_tax;
  uint32_t limit = static_cast<uint32_t>(partition->size());
  uint32_t i;
  for (i = 0; i < limit; ++i) {
    Fragment* node = partition->at(i);
    if (node->best_cost < 0.0f) break;
    float cost = node->best_cost + node_tax;
    if (budget < cost) break;
    budget -= cost;
  }
  return i;
}

namespace Encoder {

template <typename EntropyEncoder>
Result encode(const Image& src, const Params& params) {
  Result result;

  int32_t width = src.width;
  int32_t height = src.height;
  if (width < 8 || height < 8) {
    fprintf(stderr, "image is too small (%d x %d)\n", width, height);
    return {};
  }
  UberCache uber(src);
  const std::vector<Variant>& variants = params.variants;
  TaskExecutor executor(variants.size());
  std::vector<SimulationTask>& tasks = executor.tasks;
  for (auto& variant : variants) {
    tasks.emplace_back(params.targetSize, variant, uber);
  }

  std::vector<std::future<void>> futures;
  futures.reserve(params.numThreads);
  bool singleThreaded = (params.numThreads == 1);
  for (uint32_t i = 0; i < params.numThreads; ++i) {
    futures.push_back(
        std::async(singleThreaded ? std::launch::deferred : std::launch::async,
                   &TaskExecutor::run, &executor));
  }
  for (uint32_t i = 0; i < params.numThreads; ++i) futures[i].get();

  size_t bestTaskIndex = 0;
  float bestSqe = 1e35f;
  for (size_t taskIndex = 0; taskIndex < tasks.size(); ++taskIndex) {
    const SimulationTask& task = tasks[taskIndex];
    if (!task.partitionHolder) continue;
    if (task.bestSqe < bestSqe) {
      bestTaskIndex = taskIndex;
      bestSqe = task.bestSqe;
    }
  }
  bestSqe += uber.rgb2[0] + uber.rgb2[1] + uber.rgb2[2];
  SimulationTask& bestTask = tasks[bestTaskIndex];
  CodecParams& cp = bestTask.cp;
  uint32_t bestColorCode  = bestTask.bestColorCode;
  cp.setColorCode(bestColorCode);
  const Partition& partitionHolder = *bestTask.partitionHolder;

  // Encoder workflow >>
  // Partition partitionHolder(uber, cp, params.targetSize);
  uint32_t numNonLeaf = partitionHolder.subpartition(cp, params.targetSize);
  const std::vector<Fragment*>* partition = partitionHolder.getPartition();
  Owned<Vector<float>> patches = HWY_STATIC_DISPATCH(gatherPatches)(partition, numNonLeaf);
  Owned<Vector<float>> palette = HWY_STATIC_DISPATCH(buildPalette)(patches, cp.palette_size);
  float* RESTRICT colors = palette->data();
  result.data = doEncode<EntropyEncoder>(numNonLeaf, partition, cp, colors);
  // << Encoder workflow

  result.variant = bestTask.variant;
  result.variant.colorOptions = (uint64_t)1 << bestColorCode;
  result.mse = bestSqe / static_cast<float>(width * height);
  return result;
}

template Result encode<XRangeEncoder>(const Image& src, const Params& params);

}  // namespace Encoder

}  // namespace twim
