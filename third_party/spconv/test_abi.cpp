// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <spconvlib/spconv/csrc/sparse/all/SpconvOps.h>
#include <spconvlib/spconv/csrc/sparse/alloc/StaticAllocator.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

int main()
{
  using Allocator = spconvlib::spconv::csrc::sparse::alloc::StaticAllocator;
  using Ops = spconvlib::spconv::csrc::sparse::all::SpconvOps;
  float floats[] = {1.0F, 2.0F};
  auto converted = tv::from_blob(floats, {2}, tv::float32).astype(tv::float16).astype(tv::float32);
  if (converted.data<float>()[0] != 1.0F || converted.data<float>()[1] != 2.0F) return 2;
  std::int64_t integers[] = {1, 2};
  bool rejected = false;
  try {
    tv::from_blob(integers, {2}, tv::int64).astype(tv::float16);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  if (!rejected) return 3;
  std::int32_t values[] = {4, 1, 3, 2};
  auto gpu = tv::from_blob(values, {4}, tv::int32).cuda();
  Allocator alloc(std::unordered_map<std::string, tv::Tensor>{{"input", gpu}});
  auto input = alloc.get_tensor_by_name("input");
  alloc.set_new_tensor_dict({{"renamed", input}});
  auto indices = Ops::sort_1d_by_key(alloc.get_tensor_by_name("renamed"));
  auto sorted = input.cpu();
  auto permutation = indices.cpu();
  const std::int32_t expected_indices[] = {1, 3, 2, 0};
  for (int i = 0; i < 4; ++i) {
    if (
      sorted.data<std::int32_t>()[i] != i + 1 ||
      permutation.data<std::int32_t>()[i] != expected_indices[i]) {
      std::cerr << "Sparse SDK GPU sort produced incorrect values or indices\n";
      return 1;
    }
  }
  std::cout << "GNU container API, TensorView ownership and SDK GPU sort passed\n";
}
