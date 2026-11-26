// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "ar_tag_based_localizer.hpp"

#include <autoware/lanelet2_utils/conversion.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <opencv2/imgproc.hpp>
#include <tf2_ros/static_transform_broadcaster.hpp>

#include <aruco/dictionary.h>
#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>

#include <chrono>
#include <optional>
#include <thread>
class ArTagTransport : public ::testing::Test
{
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
  void check(bool pattern)
  {
    std::vector<double> covariance(36, 0.);
    for (int i : {0, 7, 14}) covariance[i] = .2;
    for (int i : {21, 28, 35}) covariance[i] = .02;
    rclcpp::NodeOptions opts;
    opts.parameter_overrides(
      {rclcpp::Parameter("marker_size", .6),
       rclcpp::Parameter("target_tag_ids", std::vector<std::string>{"0"}),
       rclcpp::Parameter("base_covariance", covariance),
       rclcpp::Parameter("distance_threshold", 13.),
       rclcpp::Parameter("consider_orientation", false),
       rclcpp::Parameter("ekf_time_tolerance", 5.), rclcpp::Parameter("ekf_position_tolerance", 2.),
       rclcpp::Parameter("detection_mode", "DM_NORMAL"),
       rclcpp::Parameter("min_marker_size", .02)});
    auto node = std::make_shared<ArTagBasedLocalizer>(opts);
    auto peer = std::make_shared<rclcpp::Node>("artag_peer");
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
    using Map = autoware_map_msgs::msg::LaneletMapBin;
    using Camera = sensor_msgs::msg::CameraInfo;
    using Image = sensor_msgs::msg::Image;
    using Pose = geometry_msgs::msg::PoseWithCovarianceStamped;
    const std::string prefix = "/ar_tag_based_localizer/";
    auto map =
      peer->create_publisher<Map>(prefix + "input/lanelet2_map", rclcpp::QoS(1).transient_local());
    auto camera =
      peer->create_publisher<Camera>(prefix + "input/camera_info", rclcpp::SensorDataQoS());
    auto image = peer->create_publisher<Image>(prefix + "input/image", rclcpp::SensorDataQoS());
    auto ekf = peer->create_publisher<Pose>(prefix + "input/ekf_pose", rclcpp::SensorDataQoS());
    std::optional<Pose> result;
    bool debug = false;
    size_t markers = 0;
    auto out = peer->create_subscription<Pose>(
      prefix + "output/pose_with_covariance", 10, [&](Pose::ConstSharedPtr p) { result = *p; });
    auto drawn =
      peer->create_subscription<Image>(prefix + "debug/image", 10, [&](Image::ConstSharedPtr p) {
        debug = p->width == 640 && p->height == 480 && p->header.stamp.sec == 101;
      });
    auto mapped = peer->create_subscription<visualization_msgs::msg::MarkerArray>(
      prefix + "debug/mapped_tag", rclcpp::QoS(1).transient_local(),
      [&](visualization_msgs::msg::MarkerArray::ConstSharedPtr p) { markers = p->markers.size(); });
    ASSERT_TRUE(pump([&] {
      return map->get_subscription_count() > 0 && camera->get_subscription_count() > 0 &&
             image->get_subscription_count() > 0 && ekf->get_subscription_count() > 0 &&
             out->get_publisher_count() > 0;
    }));
    auto lm = std::make_shared<lanelet::LaneletMap>();
    using lanelet::utils::getId;
    lanelet::Polygon3d poly(
      getId(), {lanelet::Point3d(getId(), .7, -.3, 1.5), lanelet::Point3d(getId(), 1.3, -.3, 1.5),
                lanelet::Point3d(getId(), 1.3, .3, 1.5), lanelet::Point3d(getId(), .7, .3, 1.5)});
    poly.attributes()["type"] = "pose_marker";
    poly.attributes()["subtype"] = "apriltag_16h5";
    poly.attributes()["marker_id"] = "0";
    lm->add(poly);
    map->publish(autoware::experimental::lanelet2_utils::to_autoware_map_msgs(lm));
    tf2_ros::StaticTransformBroadcaster broadcaster(peer);
    geometry_msgs::msg::TransformStamped tf;
    tf.header.frame_id = "base_link";
    tf.child_frame_id = "camera";
    tf.transform.rotation.w = 1.;
    tf.transform.translation.x = 1.;
    broadcaster.sendTransform(tf);
    ASSERT_TRUE(pump([&] { return markers == 2; }));
    Camera c;
    c.width = 640;
    c.height = 480;
    c.k = {400., 0., 320., 0., 400., 240., 0., 0., 1.};
    c.p = {400., 0., 320., 0., 0., 400., 240., 0., 0., 0., 1., 0.};
    camera->publish(c);
    Pose self;
    self.header.frame_id = "map";
    self.pose.pose.orientation.w = 1.;
    self.header.stamp.sec = 100;
    ekf->publish(self);
    pump([] { return false; }, 50);
    self.header.stamp.sec = 102;
    ekf->publish(self);
    pump([] { return false; }, 100);
    cv::Mat pixels(480, 640, CV_8UC3, cv::Scalar(255, 255, 255));
    if (pattern) {
      auto dict = aruco::Dictionary::loadPredefined(aruco::Dictionary::TAG16h5);
      auto marker = dict.getMarkerImage_id(0, 20, false);
      cv::Mat resized, rgb;
      cv::resize(marker, resized, cv::Size(160, 160), 0, 0, cv::INTER_NEAREST);
      cv::cvtColor(resized, rgb, cv::COLOR_GRAY2RGB);
      rgb.copyTo(pixels(cv::Rect(240, 160, 160, 160)));
    }
    cv_bridge::CvImage input;
    input.header.frame_id = "camera";
    input.header.stamp.sec = 101;
    input.encoding = "rgb8";
    input.image = pixels;
    auto msg = input.toImageMsg();
    ASSERT_TRUE(pump(
      [&] {
        image->publish(*msg);
        return debug && (!pattern || result.has_value());
      },
      5000));
    if (!pattern) {
      pump([] { return false; }, 150);
      EXPECT_FALSE(result);
      return;
    }
    ASSERT_TRUE(result);
    EXPECT_EQ(result->header.frame_id, "map");
    EXPECT_EQ(result->header.stamp, msg->header.stamp);
    EXPECT_NEAR(result->pose.pose.position.x, 0., .03);
    EXPECT_NEAR(result->pose.pose.position.y, 0., .03);
    EXPECT_NEAR(result->pose.pose.position.z, 0., .03);
    EXPECT_DOUBLE_EQ(result->pose.pose.orientation.w, 1.);
    for (size_t i = 0; i < 36; ++i) EXPECT_DOUBLE_EQ(result->pose.covariance[i], covariance[i]);
  }
};
TEST_F(ArTagTransport, KnownMarkerProducesPose)
{
  check(true);
}
TEST_F(ArTagTransport, BlankImageDoesNotProducePose)
{
  check(false);
}
