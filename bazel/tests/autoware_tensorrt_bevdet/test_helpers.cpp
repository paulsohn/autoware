// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/cuda_utils/cuda_unique_ptr.hpp>
#include <autoware/tensorrt_bevdet/marker_utils.hpp>
#include <autoware/tensorrt_bevdet/ros_utils.hpp>

#include <gtest/gtest.h>

namespace bev = autoware::tensorrt_bevdet;

TEST(BevDetHelpers, LabelsThresholdShapeAndTwist)
{
  using Label = autoware_perception_msgs::msg::ObjectClassification;
  EXPECT_EQ(bev::getSemanticType("car"), Label::CAR);
  EXPECT_EQ(bev::getSemanticType("bus"), Label::BUS);
  EXPECT_EQ(bev::getSemanticType("unmapped"), Label::UNKNOWN);
  Box box{};
  box.x = 1;
  box.y = 2;
  box.z = 3;
  box.l = 4;
  box.w = 2;
  box.h = 2;
  box.vx = 3;
  box.vy = 4;
  box.score = .75F;
  box.label = 0;
  auto low = box;
  low.score = .1F;
  autoware_perception_msgs::msg::DetectedObjects objects;
  objects.header.frame_id = "base_link";
  objects.header.stamp.sec = 42;
  bev::box3DToDetectedObjects({box, low}, objects, {"car"}, .2F, true);
  ASSERT_EQ(objects.objects.size(), 1U);
  auto & obj = objects.objects.front();
  EXPECT_FLOAT_EQ(obj.existence_probability, .75F);
  EXPECT_EQ(obj.classification.front().label, Label::CAR);
  EXPECT_DOUBLE_EQ(obj.kinematics.pose_with_covariance.pose.position.z, 4.);
  EXPECT_DOUBLE_EQ(obj.shape.dimensions.x, 4.);
  EXPECT_TRUE(obj.kinematics.has_twist);
  EXPECT_DOUBLE_EQ(obj.kinematics.twist_with_covariance.twist.linear.x, 5.);
}

TEST(BevDetHelpers, CameraIntrinsicsAndTransform)
{
  auto info = std::make_shared<sensor_msgs::msg::CameraInfo>();
  info->k = {400, 0, 320, 0, 410, 240, 0, 0, 1};
  Eigen::Matrix3f matrix;
  bev::getCameraIntrinsics(info, matrix);
  EXPECT_FLOAT_EQ(matrix(0, 0), 400);
  EXPECT_FLOAT_EQ(matrix(1, 1), 410);
  EXPECT_FLOAT_EQ(matrix(0, 2), 320);
  EXPECT_FLOAT_EQ(matrix(2, 2), 1);
  geometry_msgs::msg::TransformStamped tf;
  tf.transform.rotation.w = 1;
  tf.transform.translation.x = 2;
  tf.transform.translation.y = 3;
  tf.transform.translation.z = 4;
  Eigen::Quaternion<float> rotation;
  Eigen::Translation3f translation;
  bev::getTransform(tf, rotation, translation);
  EXPECT_TRUE(rotation.isApprox(Eigen::Quaternion<float>::Identity()));
  EXPECT_TRUE(translation.vector().isApprox(Eigen::Vector3f(2, 3, 4)));
}

TEST(BevDetHelpers, TwoCameraBgrPlanarDeviceLayout)
{
  std::vector<cv::Mat> images;
  images.emplace_back(1, 2, CV_8UC3, cv::Scalar(10, 20, 30));
  images.emplace_back(1, 2, CV_8UC3, cv::Scalar(40, 50, 60));
  auto output = autoware::cuda_utils::make_unique<uchar[]>(12);
  bev::imageTransport(images, output.get(), 2, 1);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  std::vector<uchar> values(12);
  ASSERT_EQ(cudaMemcpy(values.data(), output.get(), 12, cudaMemcpyDeviceToHost), cudaSuccess);
  EXPECT_EQ(values, (std::vector<uchar>{10, 10, 20, 20, 30, 30, 40, 40, 50, 50, 60, 60}));
}
