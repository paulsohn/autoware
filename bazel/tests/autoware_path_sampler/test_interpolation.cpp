// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware_path_sampler/node.hpp>
#include <autoware_test_utils/autoware_test_utils.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace
{
class Sampler : public autoware::path_sampler::PathSampler
{
public:
  explicit Sampler(const rclcpp::NodeOptions & options) : PathSampler(options) {}
  using PathSampler::copyVelocity;
  using PathSampler::copyZ;
};

auto options()
{
  rclcpp::NodeOptions options;
  const auto sampler = ament_index_cpp::get_package_share_directory("autoware_path_sampler");
  const auto fixtures = ament_index_cpp::get_package_share_directory("autoware_test_utils");
  autoware::test_utils::updateNodeOptions(
    options,
    {sampler + "/config/path_sampler.param.yaml", fixtures + "/config/test_vehicle_info.param.yaml",
     fixtures + "/config/test_nearest_search.param.yaml"});
  return options;
}

using Point = autoware_planning_msgs::msg::TrajectoryPoint;

Point point(const double x, const float velocity = 0.0, const double z = 0.0)
{
  Point p;
  p.pose.position.x = x;
  p.pose.position.z = z;
  p.pose.orientation.w = 1.0;
  p.longitudinal_velocity_mps = velocity;
  return p;
}
}  // namespace

TEST(PathSamplerInterpolation, TwoPointReferenceAndEgoAtStart)
{
  Sampler sampler(options());
  const std::vector<Point> reference{point(0.0, 1.0F, 2.0), point(10.0, 3.0F, 4.0)};
  std::vector<Point> output{point(0.0), point(5.0), point(10.0)};
  sampler.copyVelocity(reference, output, reference.front().pose);
  sampler.copyZ(reference, output);
  for (size_t i = 0; i < output.size(); ++i) {
    EXPECT_NEAR(output[i].longitudinal_velocity_mps, 1.0 + i, 1e-8);
    EXPECT_NEAR(output[i].pose.position.z, 2.0 + i, 1e-8);
  }
}

TEST(PathSamplerInterpolation, NonuniformReferenceAndInteriorLastOutput)
{
  Sampler sampler(options());
  const std::vector<Point> reference{
    point(0.0, 1.0F, 2.0), point(2.0, 2.0F, 3.0), point(10.0, 6.0F, 7.0)};
  std::vector<Point> output{point(0.0), point(1.0), point(4.0)};
  sampler.copyVelocity(reference, output, reference.front().pose);
  sampler.copyZ(reference, output);
  for (size_t i = 0; i < output.size(); ++i) {
    EXPECT_NEAR(output[i].longitudinal_velocity_mps, 1.0 + output[i].pose.position.x / 2.0, 1e-8);
    EXPECT_NEAR(output[i].pose.position.z, 2.0 + output[i].pose.position.x / 2.0, 1e-8);
  }
}

TEST(PathSamplerInterpolation, EmptySinglePointAndStopReferences)
{
  Sampler sampler(options());
  std::vector<Point> empty;
  const std::vector<Point> singleton{point(0.0, 2.0F, 3.0)};
  EXPECT_NO_THROW(sampler.copyVelocity(singleton, empty, singleton.front().pose));
  EXPECT_NO_THROW(sampler.copyZ(singleton, empty));
  std::vector<Point> output{point(0.0), point(1.0)};
  sampler.copyVelocity(singleton, output, singleton.front().pose);
  sampler.copyZ(singleton, output);
  for (const auto & p : output) {
    EXPECT_NEAR(p.longitudinal_velocity_mps, 2.0, 1e-8);
    EXPECT_NEAR(p.pose.position.z, 3.0, 1e-8);
  }
  const std::vector<Point> reference{point(0.0, 2.0F), point(10.0, 0.0F)};
  sampler.copyVelocity(reference, output, reference.front().pose);
  for (const auto & p : output) EXPECT_EQ(p.longitudinal_velocity_mps, 0.0F);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
