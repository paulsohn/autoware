// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <Eigen/Core>

#include <gtest/gtest.h>
extern "C" int eigen_cuda_values(float * output);

TEST(EigenCuda, DeviceMatrixAndVectorExpressions)
{
  float output[3]{};
  ASSERT_EQ(eigen_cuda_values(output), 0);
  for (int i = 0; i < 3; ++i) EXPECT_FLOAT_EQ(output[i], 3.F * (i + 1));
}

TEST(EigenCuda, HostMatrixAndVectorExpressions)
{
  const Eigen::Matrix3f matrix = 2.F * Eigen::Matrix3f::Identity();
  const Eigen::Vector3f vector(1.F, 2.F, 3.F);
  const Eigen::Vector3f result = matrix * vector + vector;
  for (int i = 0; i < 3; ++i) EXPECT_FLOAT_EQ(result[i], 3.F * (i + 1));
}
