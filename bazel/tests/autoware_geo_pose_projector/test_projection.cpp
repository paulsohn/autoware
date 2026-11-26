// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "geo_pose_projector.hpp"

#include <autoware_map_msgs/msg/map_projector_info.hpp>
#include <tf2_msgs/msg/tf_message.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <thread>
class Projection : public ::testing::Test
{
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
  void check(bool broadcast)
  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(
      {rclcpp::Parameter("publish_tf", broadcast), rclcpp::Parameter("parent_frame", "map"),
       rclcpp::Parameter("child_frame", "projected")});
    auto node = std::make_shared<autoware::geo_pose_projector::GeoPoseProjector>(options);
    auto peer = std::make_shared<rclcpp::Node>("projection_peer");
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(peer);
    auto pump = [&](auto done, int ms = 3000) {
      auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
      do {
        executor.spin_some();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      } while (std::chrono::steady_clock::now() < end);
      return done();
    };
    using Geo = geographic_msgs::msg::GeoPoseWithCovarianceStamped;
    using Pose = geometry_msgs::msg::PoseWithCovarianceStamped;
    using Map = autoware_map_msgs::msg::MapProjectorInfo;
    auto info =
      peer->create_publisher<Map>("/map/map_projector_info", rclcpp::QoS(1).transient_local());
    auto input = peer->create_publisher<Geo>("input_geo_pose", 10);
    std::optional<Pose> pose;
    std::optional<geometry_msgs::msg::TransformStamped> transform;
    auto sub = peer->create_subscription<Pose>(
      "output_pose", 10, [&](Pose::ConstSharedPtr p) { pose = *p; });
    auto tf = peer->create_subscription<tf2_msgs::msg::TFMessage>(
      "/tf", 10, [&](tf2_msgs::msg::TFMessage::ConstSharedPtr p) {
        for (auto & t : p->transforms)
          if (t.child_frame_id == "projected") transform = t;
      });
    ASSERT_TRUE(pump([&] {
      return input->get_subscription_count() > 0 && info->get_subscription_count() > 0 &&
             sub->get_publisher_count() > 0;
    }));
    Geo g;
    g.header.frame_id = "map";
    g.header.stamp.sec = 123;
    g.pose.pose.position.latitude = 36;
    g.pose.pose.position.longitude = 139;
    g.pose.pose.position.altitude = 50;
    g.pose.pose.orientation.w = 1;
    for (size_t i = 0; i < 36; ++i) g.pose.covariance[i] = i + 1;
    input->publish(g);
    EXPECT_FALSE(pump([&] { return pose.has_value(); }, 150));
    Map m;
    m.projector_type = Map::TRANSVERSE_MERCATOR;
    m.scale_factor = 1.;
    m.vertical_datum = Map::WGS84;
    m.map_origin.latitude = 36;
    m.map_origin.longitude = 139;
    m.map_origin.altitude = 0;
    info->publish(m);
    ASSERT_TRUE(pump([&] {
      input->publish(g);
      return pose.has_value() && (!broadcast || transform.has_value());
    }));
    EXPECT_NEAR(pose->pose.pose.position.x, 0., 1e-5);
    EXPECT_NEAR(pose->pose.pose.position.y, 0., 1e-5);
    EXPECT_NEAR(pose->pose.pose.position.z, 50., 1e-5);
    EXPECT_DOUBLE_EQ(pose->pose.pose.orientation.w, 1.);
    EXPECT_EQ(pose->header, g.header);
    auto expected = g.pose.covariance;
    std::swap(expected[0], expected[7]);
    EXPECT_EQ(pose->pose.covariance, expected);
    if (broadcast) {
      EXPECT_EQ(transform->header.frame_id, "map");
      EXPECT_EQ(transform->header.stamp, g.header.stamp);
      EXPECT_NEAR(transform->transform.translation.z, 50., 1e-5);
      EXPECT_DOUBLE_EQ(transform->transform.rotation.w, 1.);
    } else
      EXPECT_FALSE(transform);
  }
};
TEST_F(Projection, PoseWithoutTransform)
{
  check(false);
}
TEST_F(Projection, PoseAndTransform)
{
  check(true);
}
