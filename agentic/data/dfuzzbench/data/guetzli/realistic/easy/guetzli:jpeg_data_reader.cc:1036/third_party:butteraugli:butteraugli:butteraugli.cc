
// Author: Jyrki Alakuijala (jyrki.alakuijala@gmail.com)
//
// The physical architecture of butteraugli is based on the following naming
// convention:
//   * Opsin - dynamics of the photosensitive chemicals in the retina
//             with their immediate electrical processing
//   * Xyb - hybrid opponent/trichromatic color space
//     x is roughly red-subtract-green.
//     y is yellow.
//     b is blue.
//     Xyb values are computed from Opsin mixing, not directly from rgb.
//   * Mask - for visual masking
//   * Hf - color modeling for spatially high-frequency features
//   * Lf - color modeling for spatially low-frequency features
//   * Diffmap - to cluster and build an image of error between the images
//   * Blur - to hold the smoothing code

#include "butteraugli/butteraugli.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <array>


// Restricted pointers speed up Convolution(); MSVC uses a different keyword.
#ifdef _MSC_VER
#define __restrict__ __restrict
#endif

#ifndef PROFILER_ENABLED
#define PROFILER_ENABLED 0
#endif
#if PROFILER_ENABLED
#else
#define PROFILER_FUNC
#define PROFILER_ZONE(name)
#endif

namespace butteraugli {

void *CacheAligned::Allocate(const size_t bytes) {
  char *const allocated = static_cast<char *>(malloc(bytes + kCacheLineSize));
  if (allocated == nullptr) {
    return nullptr;
  }
  const uintptr_t misalignment =
      reinterpret_cast<uintptr_t>(allocated) & (kCacheLineSize - 1);
  // malloc is at least kPointerSize aligned, so we can store the "allocated"
  // pointer immediately before the aligned memory.
  assert(misalignment % kPointerSize == 0);
  char *const aligned = allocated + kCacheLineSize - misalignment;
  memcpy(aligned - kPointerSize, &allocated, kPointerSize);
  return BUTTERAUGLI_ASSUME_ALIGNED(aligned, 64);
}

void CacheAligned::Free(void *aligned_pointer) {
  if (aligned_pointer == nullptr) {
    return;
  }
  char *const aligned = static_cast<char *>(aligned_pointer);
  assert(reinterpret_cast<uintptr_t>(aligned) % kCacheLineSize == 0);
  char *allocated;
  memcpy(&allocated, aligned - kPointerSize, kPointerSize);
  assert(allocated <= aligned - kPointerSize);
  assert(allocated >= aligned - kCacheLineSize);
  free(allocated);
}

static inline bool IsNan(const float x) {
  uint32_t bits;
  memcpy(&bits, &x, sizeof(bits));
  const uint32_t bitmask_exp = 0x7F800000;
  return (bits & bitmask_exp) == bitmask_exp && (bits & 0x7FFFFF);
}

static inline bool IsNan(const double x) {
  uint64_t bits;
  memcpy(&bits, &x, sizeof(bits));
  return (0x7ff0000000000001ULL <= bits && bits <= 0x7fffffffffffffffULL) ||
         (0xfff0000000000001ULL <= bits && bits <= 0xffffffffffffffffULL);
}

static inline void CheckImage(const ImageF &image, const char *name) {
  for (size_t y = 0; y < image.ysize(); ++y) {
    const float * const BUTTERAUGLI_RESTRICT row = image.Row(y);
    for (size_t x = 0; x < image.xsize(); ++x) {
      if (IsNan(row[x])) {
        printf("Image %s @ %lu,%lu (of %lu,%lu)\n", name, x, y, image.xsize(),
               image.ysize());
        exit(1);
      }
    }
  }
}

#if BUTTERAUGLI_ENABLE_CHECKS

#define CHECK_NAN(x, str)                \
  do {                                   \
    if (IsNan(x)) {                      \
      printf("%d: %s\n", __LINE__, str); \
      abort();                           \
    }                                    \
  } while (0)

#define CHECK_IMAGE(image, name) CheckImage(image, name)

#else

#define CHECK_NAN(x, str)
#define CHECK_IMAGE(image, name)

#endif


// Purpose of kInternalGoodQualityThreshold:
// Normalize 'ok' image degradation to 1.0 across different versions of
// butteraugli.
static const double kInternalGoodQualityThreshold = 20.35;
static const double kGlobalScale = 1.0 / kInternalGoodQualityThreshold;

inline float DotProduct(const float u[3], const float v[3]) {
  return u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
}

std::vector<float> ComputeKernel(float sigma) {
  const float m = 2.25;  // Accuracy increases when m is increased.
  const float scaler = -1.0 / (2 * sigma * sigma);
  const int diff = std::max<int>(1, m * fabs(sigma));
  std::vector<float> kernel(2 * diff + 1);
  for (int i = -diff; i <= diff; ++i) {
    kernel[i + diff] = exp(scaler * i * i);
  }
  return kernel;
}

void ConvolveBorderColumn(
    const ImageF& in,
    const std::vector<float>& kernel,
    const float weight_no_border,
    const float border_ratio,
    const size_t x,
    float* const BUTTERAUGLI_RESTRICT row_out) {
  const int offset = kernel.size() / 2;
  int minx = x < offset ? 0 : x - offset;
  int maxx = std::min<int>(in.xsize() - 1, x + offset);
  float weight = 0.0f;
  for (int j = minx; j <= maxx; ++j) {
    weight += kernel[j - x + offset];
  }
  // Interpolate linearly between the no-border scaling and border scaling.
  weight = (1.0f - border_ratio) * weight + border_ratio * weight_no_border;
  float scale = 1.0f / weight;
  for (size_t y = 0; y < in.ysize(); ++y) {
    const float* const BUTTERAUGLI_RESTRICT row_in = in.Row(y);
    float sum = 0.0f;
    for (int j = minx; j <= maxx; ++j) {
      sum += row_in[j] * kernel[j - x + offset];
    }
    row_out[y] = sum * scale;
  }
}

// Computes a horizontal convolution and transposes the result.
ImageF Convolution(const ImageF& in,
                   const std::vector<float>& kernel,
                   const float border_ratio) {
  ImageF out(in.ysize(), in.xsize());
  const int len = kernel.size();
  const int offset = kernel.size() / 2;
  float weight_no_border = 0.0f;
  for (int j = 0; j < len; ++j) {
    weight_no_border += kernel[j];
  }
  float scale_no_border = 1.0f / weight_no_border;
  const int border1 = in.xsize() <= offset ? in.xsize() : offset;
  const int border2 = in.xsize() - offset;
  std::vector<float> scaled_kernel = kernel;
  for (int i = 0; i < scaled_kernel.size(); ++i) {
    scaled_kernel[i] *= scale_no_border;
  }
  // left border
  for (int x = 0; x < border1; ++x) {
    ConvolveBorderColumn(in, kernel, weight_no_border, border_ratio, x,
                         out.Row(x));
  }
  // middle
  for (size_t y = 0; y < in.ysize(); ++y) {
    const float* const BUTTERAUGLI_RESTRICT row_in = in.Row(y);
    for (int x = border1; x < border2; ++x) {
      const int d = x - offset;
      float* const BUTTERAUGLI_RESTRICT row_out = out.Row(x);
      float sum = 0.0f;
      for (int j = 0; j < len; ++j) {
        sum += row_in[d + j] * scaled_kernel[j];
      }
      row_out[y] = sum;
    }
  }
  // right border
  for (int x = border2; x < in.xsize(); ++x) {
    ConvolveBorderColumn(in, kernel, weight_no_border, border_ratio, x,
                         out.Row(x));
  }
  return out;
}

// A blur somewhat similar to a 2D Gaussian blur.
// See: https://en.wikipedia.org/wiki/Gaussian_blur
ImageF Blur(const ImageF& in, float sigma, float border_ratio) {
  std::vector<float> kernel = ComputeKernel(sigma);
  return Convolution(Convolution(in, kernel, border_ratio),
                     kernel, border_ratio);
}

// Clamping linear interpolator.
inline double InterpolateClampNegative(const double *array,
                                       int size, double ix) {
  if (ix < 0) {
    ix = 0;
  }
  int baseix = static_cast<int>(ix);
  double res;
  if (baseix >= size - 1) {
    res = array[size - 1];
  } else {
    double mix = ix - baseix;
    int nextix = baseix + 1;
    res = array[baseix] + mix * (array[nextix] - array[baseix]);
  }
  return res;
}

double GammaMinArg() {
  double out0, out1, out2;
  OpsinAbsorbance(0.0, 0.0, 0.0, &out0, &out1, &out2);
  return std::min(out0, std::min(out1, out2));
}

double GammaMaxArg() {
  double out0, out1, out2;
  OpsinAbsorbance(255.0, 255.0, 255.0, &out0, &out1, &out2);
  return std::max(out0, std::max(out1, out2));
}

double SimpleGamma(double v) {
  static const double kGamma = 0.372322653176;
  static const double limit = 37.8000499603;
  double bright = v - limit;
  if (bright >= 0) {
    static const double mul = 0.0950819040934;
    v -= bright * mul;
  }
  {
    static const double limit2 = 74.6154406429;
    double bright2 = v - limit2;
    if (bright2 >= 0) {
      static const double mul = 0.01;
      v -= bright2 * mul;
    }
  }
  {
    static const double limit2 = 82.8505938033;
    double bright2 = v - limit2;
    if (bright2 >= 0) {
      static const double mul = 0.0316722592629;
      v -= bright2 * mul;
    }
  }
  {
    static const double limit2 = 92.8505938033;
    double bright2 = v - limit2;
    if (bright2 >= 0) {
      static const double mul = 0.221249885752;
      v -= bright2 * mul;
    }
  }
  {
    static const double limit2 = 102.8505938033;
    double bright2 = v - limit2;
    if (bright2 >= 0) {
      static const double mul = 0.0402547853939;
      v -= bright2 * mul;
    }
  }
  {
    static const double limit2 = 112.8505938033;
    double bright2 = v - limit2;
    if (bright2 >= 0) {
      static const double mul = 0.021471798711500003;
      v -= bright2 * mul;
    }
  }
  static const double offset = 0.106544447664;
  static const double scale = 10.7950943969;
  double retval = scale * (offset + pow(v, kGamma));
  return retval;
}

static inline double Gamma(double v) {
  //return SimpleGamma(v);
  return GammaPolynomial(v);
}

std::vector<ImageF> OpsinDynamicsImage(const std::vector<ImageF>& rgb) {
  PROFILER_FUNC;
  std::vector<ImageF> xyb(3);
  std::vector<ImageF> blurred(3);
  const double kSigma = 1.2;
  for (int i = 0; i < 3; ++i) {
    xyb[i] = ImageF(rgb[i].xsize(), rgb[i].ysize());
    blurred[i] = Blur(rgb[i], kSigma, 0.0f);
  }
  for (size_t y = 0; y < rgb[0].ysize(); ++y) {
    const float* const BUTTERAUGLI_RESTRICT row_r = rgb[0].Row(y);
    const float* const BUTTERAUGLI_RESTRICT row_g = rgb[1].Row(y);
    const float* const BUTTERAUGLI_RESTRICT row_b = rgb[2].Row(y);
    const float* const BUTTERAUGLI_RESTRICT row_blurred_r = blurred[0].Row(y);
    const float* const BUTTERAUGLI_RESTRICT row_blurred_g = blurred[1].Row(y);
    const float* const BUTTERAUGLI_RESTRICT row_blurred_b = blurred[2].Row(y);
    float* const BUTTERAUGLI_RESTRICT row_out_x = xyb[0].Row(y);
    float* const BUTTERAUGLI_RESTRICT row_out_y = xyb[1].Row(y);
    float* const BUTTERAUGLI_RESTRICT row_out_b = xyb[2].Row(y);
    for (size_t x = 0; x < rgb[0].xsize(); ++x) {
      float sensitivity[3];
      {
        // Calculate sensitivity based on the smoothed image gamma derivative.
        float pre_mixed0, pre_mixed1, pre_mixed2;
        OpsinAbsorbance(row_blurred_r[x], row_blurred_g[x], row_blurred_b[x],
                        &pre_mixed0, &pre_mixed1, &pre_mixed2);
        // TODO: use new polynomial to compute Gamma(x)/x derivative.
        sensitivity[0] = Gamma(pre_mixed0) / pre_mixed0;
        sensitivity[1] = Gamma(pre_mixed1) / pre_mixed1;
        sensitivity[2] = Gamma(pre_mixed2) / pre_mixed2;
      }
      float cur_mixed0, cur_mixed1, cur_mixed2;
      OpsinAbsorbance(row_r[x], row_g[x], row_b[x],
                      &cur_mixed0, &cur_mixed1, &cur_mixed2);
      cur_mixed0 *= sensitivity[0];
      cur_mixed1 *= sensitivity[1];
      cur_mixed2 *= sensitivity[2];
      RgbToXyb(cur_mixed0, cur_mixed1, cur_mixed2,
               &row_out_x[x], &row_out_y[x], &row_out_b[x]);
    }
  }
  return xyb;
}

// Make area around zero less important (remove it).
static BUTTERAUGLI_INLINE float RemoveRangeAroundZero(float w, float x) {
  return x > w ? x - w : x < -w ? x + w : 0.0f;
}

// Make area around zero more important (2x it until the limit).
static BUTTERAUGLI_INLINE float AmplifyRangeAroundZero(float w, float x) {
  return x > w ? x + w : x < -w ? x - w : 2.0f * x;
}

// XybLowFreqToVals converts from low-frequency XYB space to the 'vals' space.
// Vals space can be converted to L2-norm space (Euclidean and normalized)
// through visual masking.
template <class V>
BUTTERAUGLI_INLINE void XybLowFreqToVals(const V &x, const V &y, const V &b_arg,
                                         V *BUTTERAUGLI_RESTRICT valx,
                                         V *BUTTERAUGLI_RESTRICT valy,
                                         V *BUTTERAUGLI_RESTRICT valb) {
  static const double xmuli = 5.57547552483;
  static const double ymuli = 1.20828034498;
  static const double bmuli = 6.08319517575;
  static const double y_to_b_muli = -0.628811683685;

  const V xmul(xmuli);
  const V ymul(ymuli);
  const V bmul(bmuli);
  const V y_to_b_mul(y_to_b_muli);
  const V b = b_arg + y_to_b_mul * y;
  *valb = b * bmul;
  *valx = x * xmul;
  *valy = y * ymul;
}

static ImageF SuppressInBrightAreas(size_t xsize, size_t ysize,
                                    double mul, double mul2, double reg,
                                    const ImageF& hf,
                                    const ImageF& brightness) {
  ImageF inew(xsize, ysize);
  for (size_t y = 0; y < ysize; ++y) {
    const float* const rowhf = hf.Row(y);
    const float* const rowbr = brightness.Row(y);
    float* const rownew = inew.Row(y);
    for (size_t x = 0; x < xsize; ++x) {
      float v = rowhf[x];
      float scaler = mul * reg / (reg + rowbr[x]);
      rownew[x] = scaler * v;
    }
  }
  return inew;
}


static float SuppressHfInBrightAreas(float hf, float brightness,
                                     float mul, float reg) {
  float scaler = mul * reg / (reg + brightness);
  return scaler * hf;
}

static float SuppressUhfInBrightAreas(float hf, float brightness,
                                      float mul, float reg) {
  float scaler = mul * reg / (reg + brightness);
  return scaler * hf;
}

static float MaximumClamp(float v, float maxval) {
  static const double kMul = 0.688059627878;
  if (v >= maxval) {
    v -= maxval;
    v *= kMul;
    v += maxval;
  } else if (v < -maxval) {
    v += maxval;
    v *= kMul;
    v -= maxval;
  }
  return v;
}

static ImageF MaximumClamping(size_t xsize, size_t ysize, const ImageF& ix,
                              double yw) {
  static const double kMul = 0.688059627878;
  ImageF inew(xsize, ysize);
  for (size_t y = 0; y < ysize; ++y) {
    const float* const rowx = ix.Row(y);
    float* const rownew = inew.Row(y);
    for (size_t x = 0; x < xsize; ++x) {
      double v = rowx[x];
      if (v >= yw) {
        v -= yw;
        v *= kMul;
        v += yw;
      } else if (v < -yw) {
        v += yw;
        v *= kMul;
        v -= yw;
      }
      rownew[x] = v;
    }
  }
  return inew;
}

static ImageF SuppressXByY(size_t xsize, size_t ysize,
                           const ImageF& ix, const ImageF& iy,
                           const double yw) {
  static const double s = 0.745954517135;
  ImageF inew(xsize, ysize);
  for (size_t y = 0; y < ysize; ++y) {
    const float* const rowx = ix.Row(y);
    const float* const rowy = iy.Row(y);
    float* const rownew = inew.Row(y);
    for (size_t x = 0; x < xsize; ++x) {
      const double xval = rowx[x];
      const double yval = rowy[x];
      const double scaler = s + (yw * (1.0 - s)) / (yw + yval * yval);
      rownew[x] = scaler * xval;
    }
  }
  return inew;
}

static void SeparateFrequencies(
    size_t xsize, size_t ysize,
    const std::vector<ImageF>& xyb,
    PsychoImage &ps) {
  PROFILER_FUNC;
  ps.lf.resize(3);   // XYB
  ps.mf.resize(3);   // XYB
  ps.hf.resize(2);   // XY
  ps.uhf.resize(2);  // XY
  // Extract lf ...
  static const double kSigmaLf = 7.46953768697;
  static const double kSigmaHf = 3.734768843485;
  static const double kSigmaUhf = 1.8673844217425;
  // At borders we move some more of the energy to the high frequency
  // parts, because there can be unfortunate continuations in tiling
  // background color etc. So we want to represent the borders with
  // some more accuracy.
  static double border_lf = -0.00457628248637;
  static double border_mf = -0.271277366628;
  static double border_hf = 0.147068973249;
  for (int i = 0; i < 3; ++i) {
    ps.lf[i] = Blur(xyb[i], kSigmaLf, border_lf);
    // ... and keep everything else in mf.
    ps.mf[i] = ImageF(xsize, ysize);
    for (size_t y = 0; y < ysize; ++y) {
      for (size_t x = 0; x < xsize; ++x) {
        ps.mf[i].Row(y)[x] = xyb[i].Row(y)[x] - ps.lf[i].Row(y)[x];
      }
    }
    if (i == 2) {
      ps.mf[i] = Blur(ps.mf[i], kSigmaHf, border_mf);
      break;
    }
    // Divide mf into mf and hf.
    ps.hf[i] = ImageF(xsize, ysize);
    for (size_t y = 0; y < ysize; ++y) {
      float* BUTTERAUGLI_RESTRICT const row_mf = ps.mf[i].Row(y);
      float* BUTTERAUGLI_RESTRICT const row_hf = ps.hf[i].Row(y);
      for (size_t x = 0; x < xsize; ++x) {
        row_hf[x] = row_mf[x];
      }
    }
    ps.mf[i] = Blur(ps.mf[i], kSigmaHf, border_mf);
    static const double w0 = 0.120079806822;
    static const double w1 = 0.03430529365;
    if (i == 0) {
      for (size_t y = 0; y < ysize; ++y) {
        float* BUTTERAUGLI_RESTRICT const row_mf = ps.mf[0].Row(y);
        float* BUTTERAUGLI_RESTRICT const row_hf = ps.hf[0].Row(y);
        for (size_t x = 0; x < xsize; ++x) {
          row_hf[x] -= row_mf[x];
          row_mf[x] = RemoveRangeAroundZero(w0, row_mf[x]);
        }
      }
    } else {
      for (size_t y = 0; y < ysize; ++y) {
        float* BUTTERAUGLI_RESTRICT const row_mf = ps.mf[1].Row(y);
        float* BUTTERAUGLI_RESTRICT const row_hf = ps.hf[1].Row(y);
        for (size_t x = 0; x < xsize; ++x) {
          row_hf[x] -= row_mf[x];
          row_mf[x] = AmplifyRangeAroundZero(w1, row_mf[x]);
        }
      }
    }
  }
  // Suppress red-green by intensity change in the high freq channels.
  static const double suppress = 2.96534974403;
  ps.hf[0] = SuppressXByY(xsize, ysize, ps.hf[0], ps.hf[1], suppress);

  for (int i = 0; i < 2; ++i) {
    // Divide hf into hf and uhf.
    ps.uhf[i] = ImageF(xsize, ysize);
    for (size_t y = 0; y < ysize; ++y) {
      float* BUTTERAUGLI_RESTRICT const row_uhf = ps.uhf[i].Row(y);
      float* BUTTERAUGLI_RESTRICT const row_hf = ps.hf[i].Row(y);
      for (size_t x = 0; x < xsize; ++x) {
        row_uhf[x] = row_hf[x];
      }
    }
    ps.hf[i] = Blur(ps.hf[i], kSigmaUhf, border_hf);
    static const double kRemoveHfRange = 0.0287615200377;
    static const double kMaxclampHf = 78.8223237675;
    static const double kMaxclampUhf = 5.8907152736;
    static const float kMulSuppressHf = 1.10684769012;
    static const float kMulRegHf = 0.478741530298;
    static const float kRegHf = 2000 * kMulRegHf;
    static const float kMulSuppressUhf = 1.76905001176;
    static const float kMulRegUhf = 0.310148420674;
    static const float kRegUhf = 2000 * kMulRegUhf;

    if (i == 0) {
      for (size_t y = 0; y < ysize; ++y) {
        float* BUTTERAUGLI_RESTRICT const row_uhf = ps.uhf[0].Row(y);
        float* BUTTERAUGLI_RESTRICT const row_hf = ps.hf[0].Row(y);
        for (size_t x = 0; x < xsize; ++x) {
          row_uhf[x] -= row_hf[x];
          row_hf[x] = RemoveRangeAroundZero(kRemoveHfRange, row_hf[x]);
        }
      }
    } else {
      for (size_t y = 0; y < ysize; ++y) {
        float* BUTTERAUGLI_RESTRICT const row_uhf = ps.uhf[1].Row(y);
        float* BUTTERAUGLI_RESTRICT const row_hf = ps.hf[1].Row(y);
        float* BUTTERAUGLI_RESTRICT const row_lf = ps.lf[1].Row(y);
        for (size_t x = 0; x < xsize; ++x) {
          row_uhf[x] -= row_hf[x];
          row_hf[x] = MaximumClamp(row_hf[x], kMaxclampHf);
          row_uhf[x] = MaximumClamp(row_uhf[x], kMaxclampUhf);
          row_uhf[x] = SuppressUhfInBrightAreas(row_uhf[x], row_lf[x],
                                                kMulSuppressUhf, kRegUhf);
          row_hf[x] = SuppressHfInBrightAreas(row_hf[x], row_lf[x],
                                              kMulSuppressHf, kRegHf);

        }
      }
    }
  }
  // Modify range around zero code only concerns the high frequency
  // planes and only the X and Y channels.
  // Convert low freq xyb to vals space so that we can do a simple squared sum
  // diff on the low frequencies later.
  for (size_t y = 0; y < ysize; ++y) {
    float* BUTTERAUGLI_RESTRICT const row_x = ps.lf[0].Row(y);
    float* BUTTERAUGLI_RESTRICT const row_y = ps.lf[1].Row(y);
    float* BUTTERAUGLI_RESTRICT const row_b = ps.lf[2].Row(y);
    for (size_t x = 0; x < xsize; ++x) {
      float valx, valy, valb;
      XybLowFreqToVals(row_x[x], row_y[x], row_b[x], &valx, &valy, &valb);
      row_x[x] = valx;
      row_y[x] = valy;
      row_b[x] = valb;
    }
  }
}

static void SameNoiseLevels(const ImageF& i0, const ImageF& i1,
                            const double kSigma,
                            const double w,
                            const double maxclamp,
                            ImageF* BUTTERAUGLI_RESTRICT diffmap) {
  ImageF blurred(i0.xsize(), i0.ysize());
  for (size_t y = 0; y < i0.ysize(); ++y) {
    const float* BUTTERAUGLI_RESTRICT const row0 = i0.Row(y);
    const float* BUTTERAUGLI_RESTRICT const row1 = i1.Row(y);
    float* BUTTERAUGLI_RESTRICT const to = blurred.Row(y);
    for (size_t x = 0; x < i0.xsize(); ++x) {
      double v0 = fabs(row0[x]);
      double v1 = fabs(row1[x]);
      if (v0 > maxclamp) v0 = maxclamp;
      if (v1 > maxclamp) v1 = maxclamp;
      to[x] = v0 - v1;
    }

  }
  blurred = Blur(blurred, kSigma, 0.0);
  for (size_t y = 0; y < i0.ysize(); ++y) {
    const float* BUTTERAUGLI_RESTRICT const row = blurred.Row(y);
    float* BUTTERAUGLI_RESTRICT const row_diff = diffmap->Row(y);
    for (size_t x = 0; x < i0.xsize(); ++x) {
      double diff = row[x];
      row_diff[x] += w * diff * diff;
    }
  }
}

static void L2Diff(const ImageF& i0, const ImageF& i1, const double w,
                   ImageF* BUTTERAUGLI_RESTRICT diffmap) {
  if (w == 0) {
    return;
  }
  for (size_t y = 0; y < i0.ysize(); ++y) {
    const float* BUTTERAUGLI_RESTRICT const row0 = i0.Row(y);
    const float* BUTTERAUGLI_RESTRICT const row1 = i1.Row(y);
    float* BUTTERAUGLI_RESTRICT const row_diff = diffmap->Row(y);
    for (size_t x = 0; x < i0.xsize(); ++x) {
      double diff = row0[x] - row1[x];
      row_diff[x] += w * diff * diff;
    }
  }
}

// i0 is the original image.
// i1 is the deformed copy.
static void L2DiffAsymmetric(const ImageF& i0, const ImageF& i1,
                             double w_0gt1,
                             double w_0lt1,
                             ImageF* BUTTERAUGLI_RESTRICT diffmap) {
  if (w_0gt1 == 0 && w_0lt1 == 0) {
    return;
  }
  w_0gt1 *= 0.8;
  w_0lt1 *= 0.8;
  for (size_t y = 0; y < i0.ysize(); ++y) {
    const float* BUTTERAUGLI_RESTRICT const row0 = i0.Row(y);
    const float* BUTTERAUGLI_RESTRICT const row1 = i1.Row(y);
    float* BUTTERAUGLI_RESTRICT const row_diff = diffmap->Row(y);
    for (size_t x = 0; x < i0.xsize(); ++x) {
      // Primary symmetric quadratic objective.
      double diff = row0[x] - row1[x];
      row_diff[x] += w_0gt1 * diff * diff;

      // Secondary half-open quadratic objectives.
      const double fabs0 = fabs(row0[x]);
      const double too_small = 0.4 * fabs0;
      const double too_big = 1.0 * fabs0;

      if (row0[x] < 0) {
        if (row1[x] > -too_small) {
          double v = row1[x] + too_small;
          row_diff[x] += w_0lt1 * v * v;
        } else if (row1[x] < -too_big) {
          double v = -row1[x] - too_big;
          row_diff[x] += w_0lt1 * v * v;
        }
      } else {
        if (row1[x] < too_small) {
          double v = too_small - row1[x];
          row_diff[x] += w_0lt1 * v * v;
        } else if (row1[x] > too_big) {
          double v = row1[x] - too_big;
          row_diff[x] += w_0lt1 * v * v;
        }
      }
    }
  }
}

// Making a cluster of local errors to be more impactful than
// just a single error.
ImageF CalculateDiffmap(const ImageF& diffmap_in) {
  PROFILER_FUNC;
  // Take square root.
  ImageF diffmap(diffmap_in.xsize(), diffmap_in.ysize());
  static const float kInitialSlope = 100.0f;
  for (size_t y = 0; y < diffmap.ysize(); ++y) {
    const float* const BUTTERAUGLI_RESTRICT row_in = diffmap_in.Row(y);
    float* const BUTTERAUGLI_RESTRICT row_out = diffmap.Row(y);
    for (size_t x = 0; x < diffmap.xsize(); ++x) {
      const float orig_val = row_in[x];
      // TODO(b/29974893): Until that is fixed do not call sqrt on very small
      // numbers.
      row_out[x] = (orig_val < (1.0f / (kInitialSlope * kInitialSlope))
                    ? kInitialSlope * orig_val
                    : std::sqrt(orig_val));
    }
  }
  {
    static const double kSigma = 1.72547472444;
    static const double mul1 = 0.458794906198;
    static const float scale = 1.0f / (1.0f + mul1);
    static const double border_ratio = 1.0; // 2.01209066992;
    ImageF blurred = Blur(diffmap, kSigma, border_ratio);
    for (int y = 0; y < diffmap.ysize(); ++y) {
      const float* const BUTTERAUGLI_RESTRICT row_blurred = blurred.Row(y);
      float* const BUTTERAUGLI_RESTRICT row = diffmap.Row(y);
      for (int x = 0; x < diffmap.xsize(); ++x) {
        row[x] += mul1 * row_blurred[x];
        row[x] *= scale;
      }
    }
  }
  return diffmap;
}

void MaskPsychoImage(const PsychoImage& pi0, const PsychoImage& pi1,
                     const size_t xsize, const size_t ysize,
                     std::vector<ImageF>* BUTTERAUGLI_RESTRICT mask,
                     std::vector<ImageF>* BUTTERAUGLI_RESTRICT mask_dc) {
  std::vector<ImageF> mask_xyb0 = CreatePlanes<float>(xsize, ysize, 3);
  std::vector<ImageF> mask_xyb1 = CreatePlanes<float>(xsize, ysize, 3);
  static const double muls[4] = {
    0,
    1.64178305129,
    0.831081703362,
    3.23680933546,
  };
  for (int i = 0; i < 2; ++i) {
    double a = muls[2 * i];
    double b = muls[2 * i + 1];
    for (size_t y = 0; y < ysize; ++y) {
      const float* const BUTTERAUGLI_RESTRICT row_hf0 = pi0.hf[i].Row(y);
      const float* const BUTTERAUGLI_RESTRICT row_hf1 = pi1.hf[i].Row(y);
      const float* const BUTTERAUGLI_RESTRICT row_uhf0 = pi0.uhf[i].Row(y);
      const float* const BUTTERAUGLI_RESTRICT row_uhf1 = pi1.uhf[i].Row(y);
      float* const BUTTERAUGLI_RESTRICT row0 = mask_xyb0[i].Row(y);
      float* const BUTTERAUGLI_RESTRICT row1 = mask_xyb1[i].Row(y);
      for (size_t x = 0; x < xsize; ++x) {
        row0[x] = a * row_uhf0[x] + b * row_hf0[x];
        row1[x] = a * row_uhf1[x] + b * row_hf1[x];
      }
    }
  }
  Mask(mask_xyb0, mask_xyb1, mask, mask_dc);
}

ButteraugliComparator::ButteraugliComparator(const std::vector<ImageF>& rgb0)
    : xsize_(rgb0[0].xsize()),
      ysize_(rgb0[0].ysize()),
      num_pixels_(xsize_ * ysize_) {
  if (xsize_ < 8 || ysize_ < 8) return;
  std::vector<ImageF> xyb0 = OpsinDynamicsImage(rgb0);
  SeparateFrequencies(xsize_, ysize_, xyb0, pi0_);
}

void ButteraugliComparator::Mask(
    std::vector<ImageF>* BUTTERAUGLI_RESTRICT mask,
    std::vector<ImageF>* BUTTERAUGLI_RESTRICT mask_dc) const {
  MaskPsychoImage(pi0_, pi0_, xsize_, ysize_, mask, mask_dc);
}

void ButteraugliComparator::Diffmap(const std::vector<ImageF>& rgb1,
                                    ImageF &result) const {
  PROFILER_FUNC;
  if (xsize_ < 8 || ysize_ < 8) return;
  DiffmapOpsinDynamicsImage(OpsinDynamicsImage(rgb1), result);
}

void ButteraugliComparator::DiffmapOpsinDynamicsImage(
    const std::vector<ImageF>& xyb1,
    ImageF &result) const {
  PROFILER_FUNC;
  if (xsize_ < 8 || ysize_ < 8) return;
  PsychoImage pi1;
  SeparateFrequencies(xsize_, ysize_, xyb1, pi1);
  result = ImageF(xsize_, ysize_);
  DiffmapPsychoImage(pi1, result);
}

void ButteraugliComparator::DiffmapPsychoImage(const PsychoImage& pi1,
                                               ImageF& result) const {
  PROFILER_FUNC;
  const float hf_asymmetry_ = 0.8f;
  if (xsize_ < 8 || ysize_ < 8) {
    return;
  }
  std::vector<ImageF> block_diff_dc(3);
  std::vector<ImageF> block_diff_ac(3);
  for (int c = 0; c < 3; ++c) {
    block_diff_dc[c] = ImageF(xsize_, ysize_, 0.0);
    block_diff_ac[c] = ImageF(xsize_, ysize_, 0.0);
  }

  static const double wUhfMalta = 5.1409625726;
  static const double norm1Uhf = 58.5001247061;
  MaltaDiffMap(pi0_.uhf[1], pi1.uhf[1],
               wUhfMalta * hf_asymmetry_,
               wUhfMalta / hf_asymmetry_,
               norm1Uhf,
               &block_diff_ac[1]);

  static const double wUhfMaltaX = 4.91743441556;
  static const double norm1UhfX = 687196.39002;
  MaltaDiffMap(pi0_.uhf[0], pi1.uhf[0],
               wUhfMaltaX * hf_asymmetry_,
               wUhfMaltaX / hf_asymmetry_,
               norm1UhfX,
               &block_diff_ac[0]);

  static const double wHfMalta = 153.671655716;
  static const double norm1Hf = 83150785.9592;
  MaltaDiffMapLF(pi0_.hf[1], pi1.hf[1],
                 wHfMalta * sqrt(hf_asymmetry_),
                 wHfMalta / sqrt(hf_asymmetry_),
                 norm1Hf,
                 &block_diff_ac[1]);

  static const double wHfMaltaX = 668.358918152;
  static const double norm1HfX = 0.882954368025;
  MaltaDiffMapLF(pi0_.hf[0], pi1.hf[0],
                 wHfMaltaX * sqrt(hf_asymmetry_),
                 wHfMaltaX / sqrt(hf_asymmetry_),
                 norm1HfX,
                 &block_diff_ac[0]);

  static const double wMfMalta = 6841.81248144;
  static const double norm1Mf = 0.0135134962487;
  MaltaDiffMapLF(pi0_.mf[1], pi1.mf[1], wMfMalta, wMfMalta, norm1Mf,
                 &block_diff_ac[1]);

  static const double wMfMaltaX = 813.901703816;
  static const double norm1MfX = 16792.9322251;
  MaltaDiffMapLF(pi0_.mf[0], pi1.mf[0], wMfMaltaX, wMfMaltaX, norm1MfX,
                 &block_diff_ac[0]);

  static const double wmul[9] = {
    0,
    32.4449876135,
    0,
    0,
    0,
    0,
    1.01370836411,
    0,
    1.74566011615,
  };

  static const double maxclamp = 85.7047444518;
  static const double kSigmaHfX = 10.6666499623;
  static const double w = 884.809801415;
  SameNoiseLevels(pi0_.hf[1], pi1.hf[1], kSigmaHfX, w, maxclamp,
                  &block_diff_ac[1]);

  for (int c = 0; c < 3; ++c) {
    if (c < 2) {
      L2DiffAsymmetric(pi0_.hf[c], pi1.hf[c],
                       wmul[c] * hf_asymmetry_,
                       wmul[c] / hf_asymmetry_,
                       &block_diff_ac[c]);
    }
    L2Diff(pi0_.mf[c], pi1.mf[c], wmul[3 + c], &block_diff_ac[c]);
    L2Diff(pi0_.lf[c], pi1.lf[c], wmul[6 + c], &block_diff_dc[c]);
  }

  std::vector<ImageF> mask_xyb;
  std::vector<ImageF> mask_xyb_dc;
  MaskPsychoImage(pi0_, pi1, xsize_, ysize_, &mask_xyb, &mask_xyb_dc);

  result = CalculateDiffmap(
      CombineChannels(mask_xyb, mask_xyb_dc, block_diff_dc, block_diff_ac));
}

// Allows PaddedMaltaUnit to call either function via overloading.
struct MaltaTagLF {};
struct MaltaTag {};

static float MaltaUnit(MaltaTagLF, const float* BUTTERAUGLI_RESTRICT d,
                       const int xs) {
  const int xs3 = 3 * xs;
  float retval = 0;
  {
    // x grows, y constant
    float sum =
        d[-4] +
        d[-2] +
        d[0] +
        d[2] +
        d[4];
    retval += sum * sum;
  }
  {
    // y grows, x constant
    float sum =
        d[-xs3 - xs] +
        d[-xs - xs] +
        d[0] +
        d[xs + xs] +
        d[xs3 + xs];
    retval += sum * sum;
  }
  {
    // both grow
    float sum =
        d[-xs3 - 3] +
        d[-xs - xs - 2] +
        d[0] +
        d[xs + xs + 2] +
        d[xs3 + 3];
    retval += sum * sum;
  }
  {
    // y grows, x shrinks
    float sum =
        d[-xs3 + 3] +
        d[-xs - xs + 2] +
        d[0] +
        d[xs + xs - 2] +
        d[xs3 - 3];
    retval += sum * sum;
  }
  {
    // y grows -4 to 4, x shrinks 1 -> -1
    float sum =
        d[-xs3 - xs + 1] +
        d[-xs - xs + 1] +
        d[0] +
        d[xs + xs - 1] +
        d[xs3 + xs - 1];
    retval += sum * sum;
  }
  {
    //  y grows -4 to 4, x grows -1 -> 1
    float sum =
        d[-xs3 - xs - 1] +
        d[-xs - xs - 1] +
        d[0] +
        d[xs + xs + 1] +
        d[xs3 + xs + 1];
    retval += sum * sum;
  }
  {
    // x grows -4 to 4, y grows -1 to 1
    float sum =
        d[-4 - xs] +
        d[-2 - xs] +
        d[0] +
        d[2 + xs] +
        d[4 + xs];
    retval += sum * sum;
  }
  {
    // x grows -4 to 4, y shrinks 1 to -1
    float sum =
        d[-4 + xs] +
        d[-2 + xs] +
        d[0] +
        d[2 - xs] +
        d[4 - xs];
    retval += sum * sum;
  }
  {
    /* 0_________
       1__*______
       2___*_____
       3_________
       4____0____
       5_________
       6_____*___
       7______*__
       8_________ */
    float sum =
        d[-xs3 - 2] +
        d[-xs - xs - 1] +
        d[0] +
        d[xs + xs + 1] +
        d[xs3 + 2];
    retval += sum * sum;
  }
  {
    /* 0_________
       1______*__
       2_____*___
       3_________
       4____0____
       5_________
       6___*_____
       7__*______
       8_________ */
    float sum =
        d[-xs3 + 2] +
        d[-xs - xs + 1] +
        d[0] +
        d[xs + xs - 1] +
        d[xs3 - 2];
    retval += sum * sum;
  }
  {
    /* 0_________
       1_________
       2_*_______
       3__*______
       4____0____
       5______*__
       6_______*_
       7_________
       8_________ */
    float sum =
        d[-xs - xs - 3] +
        d[-xs - 2] +
