// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Demo interface to target-specific code in skeleton.cc

// Normal header with include guard and namespace.
#ifndef HWY_EXAMPLES_SKELETON_H_
#define HWY_EXAMPLES_SKELETON_H_

// Tiny subset of Highway API: essentials for declaring an interface, without
// any implementation details.
#include "hwy/base.h"  // HWY_RESTRICT

namespace skeleton {

// Computes out[i] = in1[i] * kMultiplier + in2[i] for i < 256.
void Skeleton(const float* HWY_RESTRICT in1, const float* HWY_RESTRICT in2,
              float* HWY_RESTRICT out);

}  // namespace skeleton

#endif  // HWY_EXAMPLES_SKELETON_H_
