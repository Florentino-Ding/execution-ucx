/*Copyright 2025 He Jia <mofhejia@163.com>. All Rights Reserved.
 *
 *Licensed under the Apache License Version 2.0 with LLVM Exceptions
 *(the "License"); you may not use this file except in compliance with
 *the License. You may obtain a copy of the License at
 *
 *    https://llvm.org/LICENSE.txt
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 *==============================================================================*/

#include <gtest/gtest.h>

#if CUDA_ENABLED
#include <cuda_runtime_api.h>
#endif

#include <cstring>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include "ucx_context/ucx_context_data.hpp"
#include "ucx_context/ucx_memory_resource.hpp"

namespace eux {
namespace ucxx {

class UcxBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mr_ = std::make_unique<DefaultUcxMemoryResourceManager>();
  }

  void TearDown() override { mr_.reset(); }

  std::unique_ptr<UcxMemoryResourceManager> mr_;
};

TEST_F(UcxBufferTest, UnsupportedDeviceCopyRequiresRegistration) {
  EXPECT_THROW(
    mr_->get_memcpy_fn(ucx_memory_type::ROCM, ucx_memory_type::HOST),
    std::runtime_error);
#if !CUDA_ENABLED
  EXPECT_THROW(
    mr_->get_memcpy_fn(ucx_memory_type::CUDA, ucx_memory_type::HOST),
    std::runtime_error);
#endif
}

#if CUDA_ENABLED
TEST_F(UcxBufferTest, DefaultManagerCopiesHostPayloadToCuda) {
  int devices = 0;
  if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) {
    GTEST_SKIP() << "CUDA device unavailable";
  }
  int* destination = nullptr;
  ASSERT_EQ(
    cudaMalloc(reinterpret_cast<void**>(&destination), sizeof(int)),
    cudaSuccess);
  auto device_buffer =
    std::unique_ptr<int, decltype(&cudaFree)>(destination, &cudaFree);
  const int original = 42;
  mr_->memcpy(
    ucx_memory_type::CUDA, destination, ucx_memory_type::HOST, &original,
    sizeof(original));
  int received = 0;
  ASSERT_EQ(
    cudaMemcpy(
      &received, destination, sizeof(received), cudaMemcpyDeviceToHost),
    cudaSuccess);
  EXPECT_EQ(received, original);
}
#endif

TEST_F(UcxBufferTest, ZeroCopyConversion) {
  const size_t buffer_size = 100;
  UcxBuffer buffer(*mr_, ucx_memory_type::HOST, buffer_size);

  // Fill the buffer with some data
  std::vector<uint8_t> source_data(buffer_size);
  std::iota(source_data.begin(), source_data.end(), 0);
  std::memcpy(buffer.data(), source_data.data(), buffer_size);

  // Store the original pointer before moving the buffer
  void* original_buffer_ptr = buffer.data();

  const std::vector<size_t> sizes = {10, 20, 30, 40};
  auto buffer_vec = std::move(buffer).to_buffer_vec(sizes);

  EXPECT_EQ(buffer_vec.size(), sizes.size());

  size_t offset = 0;
  for (size_t i = 0; i < sizes.size(); ++i) {
    EXPECT_EQ(buffer_vec[i].size, sizes[i]);
    // Verify that the data pointer is just an offset into the original buffer
    EXPECT_EQ(
      buffer_vec[i].data, static_cast<uint8_t*>(original_buffer_ptr) + offset);
    // Verify the content of the sliced buffer
    EXPECT_EQ(
      0, std::memcmp(
           buffer_vec[i].data,
           static_cast<uint8_t*>(original_buffer_ptr) + offset, sizes[i]));
    offset += sizes[i];
  }

  // The original buffer should be invalidated
  EXPECT_EQ(buffer.data(), nullptr);
  EXPECT_EQ(buffer.size(), 0);
}

TEST_F(UcxBufferTest, ThrowsOnSizeMismatch) {
  const size_t buffer_size = 100;
  UcxBuffer buffer(*mr_, ucx_memory_type::HOST, buffer_size);
  const std::vector<size_t> sizes = {50, 51};  // Total > 100

  EXPECT_THROW(std::move(buffer).to_buffer_vec(sizes), std::length_error);
}

TEST_F(UcxBufferTest, ExtractedViewsKeepEagerAllocationUntilLastRelease) {
  uint8_t data[] = {11, 12, 21, 22, 23};
  int releases = 0;
  auto views = [&] {
    UcxBuffer buffer(
      *mr_, ucx_memory_type::HOST, data, sizeof(data), nullptr, true,
      [&](void* ptr) {
        EXPECT_EQ(ptr, data);
        ++releases;
      });
    auto sliced = std::move(buffer).to_buffer_vec({2, 0, 3});
    UcxBufferVec moved(std::move(sliced));
    UcxBufferVec moved_again(std::move(moved), true);
    return std::move(moved_again).ExtractBuffers();
  }();

  ASSERT_EQ(views.size(), 3);
  EXPECT_EQ(views[2].data(), data + 2);
  auto last = std::move(views[2]);
  views.clear();
  EXPECT_EQ(releases, 0);
  EXPECT_EQ(static_cast<uint8_t*>(last.data())[0], 21);
  last = UcxBuffer(*mr_, ucx_memory_type::HOST, 0);
  EXPECT_EQ(releases, 1);
}

TEST_F(UcxBufferTest, MoveAssignmentReleasesPreviousVector) {
  uint8_t original = 0;
  int releases = 0;
  UcxBufferVec destination(
    *mr_, ucx_memory_type::HOST, std::vector<ucx_buffer_t>{{&original, 1}},
    nullptr, true, [&](void* ptr) {
      EXPECT_EQ(ptr, &original);
      ++releases;
    });
  UcxBuffer source(*mr_, ucx_memory_type::HOST, 4);
  destination = std::move(source).to_buffer_vec({1, 3});

  EXPECT_EQ(releases, 1);
  EXPECT_TRUE(destination.has_backing_buffer());
  EXPECT_EQ(destination[1].size, 3);
}

TEST_F(UcxBufferTest, MoveWithOwnBufferTruePreservesReleaseCallback) {
  int release_count = 0;
  auto* external = new uint8_t[16];

  {
    UcxBuffer buffer(
      *mr_, ucx_memory_type::HOST, external, 16, nullptr, true,
      [&release_count](void* ptr) {
        ++release_count;
        delete[] static_cast<uint8_t*>(ptr);
      });

    auto moved = UcxBuffer(std::move(buffer), true);
    EXPECT_EQ(buffer.data(), nullptr);
    EXPECT_FALSE(buffer.own_buffer());
    EXPECT_EQ(moved.data(), external);
    EXPECT_TRUE(moved.own_buffer());
  }

  EXPECT_EQ(release_count, 1);
}

TEST_F(UcxBufferTest, MoveWithOwnBufferFalseSuppressesReleaseCallback) {
  int release_count = 0;
  auto* external = new uint8_t[16];

  {
    UcxBuffer buffer(
      *mr_, ucx_memory_type::HOST, external, 16, nullptr, true,
      [&release_count](void* ptr) {
        ++release_count;
        delete[] static_cast<uint8_t*>(ptr);
      });

    auto moved = UcxBuffer(std::move(buffer), false);
    EXPECT_EQ(buffer.data(), nullptr);
    EXPECT_FALSE(buffer.own_buffer());
    EXPECT_EQ(moved.data(), external);
    EXPECT_FALSE(moved.own_buffer());
  }

  EXPECT_EQ(release_count, 0);
  delete[] external;
}

}  // namespace ucxx
}  // namespace eux
