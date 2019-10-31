#ifndef TWIM_IMAGE
#define TWIM_IMAGE

#include <memory>
#include <string>
#include <vector>

#include "platform.h"

namespace twim {

struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> r;
  std::vector<uint8_t> g;
  std::vector<uint8_t> b;

  static Image fromRgba(const uint8_t* src, uint32_t width, uint32_t height);
};

}  // namespace twim

#endif  // TWIM_IMAGE
