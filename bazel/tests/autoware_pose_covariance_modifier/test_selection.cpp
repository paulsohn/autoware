// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "pose_covariance_modifier.hpp"

#include <std_msgs/msg/string.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <thread>
class Covariance : public ::testing::Test
{
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
  void check(double gnss_std, const std::string & expected, bool expire = false)
  {
    using Pose = geometry_msgs::msg::PoseWithCovarianceStamped;
    rclcpp::NodeOptions opts;
    opts.parameter_overrides(
      {rclcpp::Parameter("threshold_gnss_stddev_yaw_deg_max", 10.),
       rclcpp::Parameter("threshold_gnss_stddev_z_max", 1.),
       rclcpp::Parameter("threshold_gnss_stddev_xy_bound_lower", .1),
       rclcpp::Parameter("threshold_gnss_stddev_xy_bound_upper", .3),
       rclcpp::Parameter("ndt_std_dev_bound_lower", .1),
       rclcpp::Parameter("ndt_std_dev_bound_upper", .5),
       rclcpp::Parameter("gnss_pose_timeout_sec", expire ? .05 : 10.),
       rclcpp::Parameter("enable_debug_topics", false)});
    auto node =
      std::make_shared<autoware::pose_covariance_modifier::PoseCovarianceModifierNode>(opts);
    auto peer = std::make_shared<rclcpp::Node>("covariance_peer");
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(peer);
    auto pump = [&](auto done, int ms = 2000) {
      auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
      do {
        executor.spin_some();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      } while (std::chrono::steady_clock::now() < end);
      return done();
    };
    auto gnss = peer->create_publisher<Pose>("input_gnss_pose_with_cov_topic", 10);
    auto ndt = peer->create_publisher<Pose>("input_ndt_pose_with_cov_topic", 10);
    std::optional<Pose> out;
    std::string source;
    auto sub = peer->create_subscription<Pose>(
      "output_pose_with_covariance_topic", 10, [&](Pose::ConstSharedPtr p) { out = *p; });
    auto sel = peer->create_subscription<std_msgs::msg::String>(
      "/PoseCovarianceModifierNode/selected_pose_type", 10,
      [&](std_msgs::msg::String::ConstSharedPtr p) { source = p->data; });
    ASSERT_TRUE(pump([&] {
      return gnss->get_subscription_count() > 0 && ndt->get_subscription_count() > 0 &&
             sub->get_publisher_count() > 0 && sel->get_publisher_count() > 0;
    }));
    Pose g;
    g.header.frame_id = "map";
    g.header.stamp.sec = 11;
    g.pose.pose.orientation.w = 1.;
    g.pose.pose.position.x = 10.;
    for (int i : {0, 7, 14}) g.pose.covariance[i] = gnss_std * gnss_std;
    if (gnss_std >= 0) {
      gnss->publish(g);
      ASSERT_TRUE(pump([&] { return source == expected; }));
    }
    Pose n = g;
    n.header.stamp.sec = 12;
    n.pose.pose.position.x = 20.;
    n.pose.covariance.fill(0.);
    for (int i : {0, 7, 14}) n.pose.covariance[i] = .04;
    if (expected == "GNSS") {
      ASSERT_TRUE(out);
      EXPECT_EQ(*out, g);
      ndt->publish(n);
      pump([] { return false; }, 100);
      EXPECT_EQ(*out, g);
      return;
    }
    if (expected == "NDT") EXPECT_FALSE(out);
    if (expire) pump([] { return false; }, 100);
    out.reset();
    ndt->publish(n);
    ASSERT_TRUE(pump([&] { return out.has_value(); }));
    auto wanted = n;
    if (expected == "GNSS_NDT" && !expire)
      for (int i : {0, 7, 14}) wanted.pose.covariance[i] = .09;
    EXPECT_EQ(out->header, wanted.header);
    EXPECT_EQ(out->pose.pose, wanted.pose.pose);
    for (size_t i = 0; i < 36; ++i)
      EXPECT_NEAR(out->pose.covariance[i], wanted.pose.covariance[i], 1e-12);
  }
};
TEST_F(Covariance, NdtBeforeGnss)
{
  check(-1, "NDT");
}
TEST_F(Covariance, PreciseGnssSuppressesNdt)
{
  check(.05, "GNSS");
}
TEST_F(Covariance, MixedProfileUpdatesNdtVariance)
{
  check(.2, "GNSS_NDT");
}
TEST_F(Covariance, PoorGnssKeepsNdt)
{
  check(.4, "NDT");
}
TEST_F(Covariance, ExpiredMixedProfileKeepsNdtVariance)
{
  check(.2, "GNSS_NDT", true);
}
