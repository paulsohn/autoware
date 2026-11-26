// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "lidar_marker_localizer.hpp"

#include <autoware/lanelet2_utils/conversion.hpp>
#include <autoware/point_types/types.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>

#include <chrono>
#include <optional>
#include <thread>
#include <type_traits>
class LidarMarkerTransport : public ::testing::Test
{
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
  template <class Point>
  void check(bool pattern)
  {
    using Pose = geometry_msgs::msg::PoseWithCovarianceStamped;
    using Cloud = sensor_msgs::msg::PointCloud2;
    using Map = autoware_map_msgs::msg::LaneletMapBin;
    using Markers = visualization_msgs::msg::MarkerArray;
    using Diag = diagnostic_msgs::msg::DiagnosticArray;
    std::vector<double> covariance(36, 0.);
    covariance[0] = .04;
    covariance[7] = .09;
    covariance[14] = .01;
    for (int i : {21, 28, 35}) covariance[i] = .001;
    rclcpp::NodeOptions opts;
    opts.parameter_overrides(
      {rclcpp::Parameter("enable_read_all_target_ids", true),
       rclcpp::Parameter("target_ids", std::vector<std::string>{"0"}),
       rclcpp::Parameter("queue_size_for_output_pose", 10),
       rclcpp::Parameter("marker_name", "reflector"),
       rclcpp::Parameter("resolution", 1.),
       rclcpp::Parameter("intensity_pattern", std::vector<int64_t>{-1, 1, -1}),
       rclcpp::Parameter("match_intensity_difference_threshold", 20),
       rclcpp::Parameter("positive_match_num_threshold", 1),
       rclcpp::Parameter("negative_match_num_threshold", 2),
       rclcpp::Parameter("vote_threshold_for_detect_marker", 1),
       rclcpp::Parameter("marker_to_vehicle_offset_y", 1.),
       rclcpp::Parameter("marker_height_from_ground", 1.),
       rclcpp::Parameter("lower_ring_id_init", 128),
       rclcpp::Parameter("upper_ring_id_init", 0),
       rclcpp::Parameter("reference_ring_number", 255),
       rclcpp::Parameter("self_pose_timeout_sec", 5.),
       rclcpp::Parameter("self_pose_distance_tolerance_m", 1.),
       rclcpp::Parameter("limit_distance_from_self_pose_to_nearest_marker", 5.),
       rclcpp::Parameter("limit_distance_from_self_pose_to_nearest_marker_y", 2.),
       rclcpp::Parameter("limit_distance_from_self_pose_to_marker", 1.),
       rclcpp::Parameter("base_covariance", covariance),
       rclcpp::Parameter("marker_width", 2.),
       rclcpp::Parameter("enable_save_log", false),
       rclcpp::Parameter("save_file_directory_path", "unused"),
       rclcpp::Parameter("save_file_name", "unused"),
       rclcpp::Parameter("save_frame_id", "base_link"),
       rclcpp::Parameter("radius_for_extracting_marker_pointcloud", 1.),
       rclcpp::Parameter("queue_size_for_debug_pub_msg", 10)});
    auto node = std::make_shared<autoware::lidar_marker_localizer::LidarMarkerLocalizer>(opts);
    auto peer = std::make_shared<rclcpp::Node>("lidar_marker_peer");
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(peer);
    auto pump = [&](auto pred, int ms = 3000) {
      auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
      do {
        executor.spin_some();
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      } while (std::chrono::steady_clock::now() < end);
      return pred();
    };
    const std::string prefix = "/lidar_marker_localizer/";
    auto map =
      peer->create_publisher<Map>(prefix + "input/lanelet2_map", rclcpp::QoS(10).transient_local());
    auto ekf = peer->create_publisher<Pose>(prefix + "input/ekf_pose", 1);
    auto cloud =
      peer->create_publisher<Cloud>(prefix + "input/pointcloud", rclcpp::SensorDataQoS());
    auto trigger = peer->create_client<std_srvs::srv::SetBool>(prefix + "service/trigger_node_srv");
    std::optional<Pose> output;
    std::optional<Diag> diagnostic;
    std::optional<Cloud> extracted;
    size_t mapped_count = 0;
    auto sub = peer->create_subscription<Pose>(
      prefix + "output/pose_with_covariance", 10, [&](Pose::ConstSharedPtr p) { output = *p; });
    auto marker = peer->create_subscription<Markers>(
      prefix + "debug/marker_mapped", rclcpp::QoS(10).transient_local(),
      [&](Markers::ConstSharedPtr p) { mapped_count = p->markers.size(); });
    auto diag = peer->create_subscription<Diag>("/diagnostics", 10, [&](Diag::ConstSharedPtr p) {
      if (diagnostic || p->status.size() != 1) return;
      for (const auto & kv : p->status[0].values) {
        if (kv.key == "is_received_self_pose" && kv.value == "True") diagnostic = *p;
      }
    });
    auto points = peer->create_subscription<Cloud>(
      prefix + "debug/marker_pointcloud", 10, [&](Cloud::ConstSharedPtr p) { extracted = *p; });
    ASSERT_TRUE(pump([&] {
      return map->get_subscription_count() > 0 && ekf->get_subscription_count() > 0 &&
             cloud->get_subscription_count() > 0 && trigger->service_is_ready();
    }));
    auto req = std::make_shared<std_srvs::srv::SetBool::Request>();
    req->data = true;
    auto response = trigger->async_send_request(req);
    ASSERT_TRUE(pump(
      [&] { return response.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }));
    EXPECT_TRUE(response.get()->success);
    auto lm = std::make_shared<lanelet::LaneletMap>();
    using lanelet::utils::getId;
    lanelet::Polygon3d poly(
      getId(), {lanelet::Point3d(getId(), 11., .5, 1.), lanelet::Point3d(getId(), 12., .5, 1.),
                lanelet::Point3d(getId(), 12., 1.5, 1.), lanelet::Point3d(getId(), 11., 1.5, 1.)});
    poly.attributes()["type"] = "pose_marker";
    poly.attributes()["subtype"] = "reflector";
    poly.attributes()["marker_id"] = "0";
    lm->add(poly);
    map->publish(autoware::experimental::lanelet2_utils::to_autoware_map_msgs(lm));
    ASSERT_TRUE(pump([&] { return mapped_count == 2; }));
    Pose self;
    self.header.frame_id = "map";
    self.pose.pose.orientation.w = 1.;
    self.pose.pose.position.x = 9.8;
    self.header.stamp.sec = 100;
    ekf->publish(self);
    pump([] { return false; }, 50);
    self.header.stamp.sec = 102;
    ekf->publish(self);
    pump([] { return false; }, 50);
    pcl::PointCloud<Point> pc;
    for (int ring = 0; ring < 2; ++ring)
      for (int i = 0; i < 5; ++i) {
        Point p{};
        p.x = i;
        p.y = 1;
        p.z = 1;
        p.intensity = pattern ? (i == 1 ? 100 : 0) : 100;
        if constexpr (std::is_same_v<Point, autoware::point_types::PointXYZIRC>)
          p.channel = ring;
        else
          p.ring = ring;
        pc.push_back(p);
      }
    Cloud input;
    pcl::PCLPointCloud2 padded;
    pcl::toPCLPointCloud2(pc, padded, true);
    pcl_conversions::moveFromPCL(padded, input);
    ASSERT_EQ(input.point_step, sizeof(Point));
    input.header.frame_id = "base_link";
    input.header.stamp.sec = 101;
    ASSERT_TRUE(pump(
      [&] {
        cloud->publish(input);
        return diagnostic.has_value() &&
               (!pattern || (output.has_value() && extracted.has_value()));
      },
      5000));
    ASSERT_TRUE(diagnostic);
    ASSERT_EQ(diagnostic->status.size(), 1U);
    EXPECT_EQ(diagnostic->header.stamp, input.header.stamp);
    EXPECT_EQ(
      diagnostic->status[0].level, pattern ? diagnostic_msgs::msg::DiagnosticStatus::OK
                                           : diagnostic_msgs::msg::DiagnosticStatus::WARN);
    bool counted = false;
    for (const auto & kv : diagnostic->status[0].values) {
      if (kv.key == "detect_marker_num") {
        counted = true;
        EXPECT_EQ(kv.value, pattern ? "1" : "0");
      }
    }
    EXPECT_TRUE(counted);
    if (!pattern) {
      pump([] { return false; }, 100);
      EXPECT_FALSE(output);
      return;
    }
    ASSERT_TRUE(output);
    EXPECT_EQ(output->header.frame_id, "map");
    EXPECT_EQ(output->header.stamp, input.header.stamp);
    EXPECT_NEAR(output->pose.pose.position.x, 10., 1e-6);
    EXPECT_NEAR(output->pose.pose.position.y, 0., 1e-6);
    EXPECT_NEAR(output->pose.pose.position.z, 0., 1e-6);
    EXPECT_DOUBLE_EQ(output->pose.pose.orientation.w, 1.);
    for (size_t i = 0; i < 36; ++i) EXPECT_NEAR(output->pose.covariance[i], covariance[i], 1e-12);
    ASSERT_TRUE(extracted);
    EXPECT_EQ(extracted->header, input.header);
    EXPECT_EQ(extracted->width, 4U);
  }
};
TEST_F(LidarMarkerTransport, PatternXYZIRC)
{
  check<autoware::point_types::PointXYZIRC>(true);
}
TEST_F(LidarMarkerTransport, PatternXYZIRADRT)
{
  check<autoware::point_types::PointXYZIRADRT>(true);
}
TEST_F(LidarMarkerTransport, FlatIntensityProducesWarningWithoutPose)
{
  check<autoware::point_types::PointXYZIRC>(false);
}
