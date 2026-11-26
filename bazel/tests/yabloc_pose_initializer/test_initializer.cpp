// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/lanelet2_utils/conversion.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <tf2_ros/static_transform_broadcaster.hpp>
#include <yabloc_pose_initializer/camera/camera_pose_initializer.hpp>
#include <yabloc_pose_initializer/camera/semantic_segmentation.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/utility/Utilities.h>

#include <chrono>
#include <optional>
#include <thread>
std::string model_path()
{
  return ament_index_cpp::get_package_share_directory("autoware_yabloc_pose_initializer_models") +
         "/saved_model/model_float32.onnx";
}
TEST(SemanticModel, CpuInferenceThresholdsAndShape)
{
  SemanticSegmentation net(model_path());
  cv::Mat image(120, 160, CV_8UC3, cv::Scalar(60, 70, 80));
  const auto accepted = net.inference(image, 0.);
  const auto rejected = net.inference(image, 1.);
  EXPECT_EQ(accepted.size(), image.size());
  EXPECT_EQ(accepted.type(), CV_8UC3);
  EXPECT_EQ(rejected.size(), image.size());
  EXPECT_EQ(cv::countNonZero(accepted.reshape(1)), image.rows * image.cols * 3);
  EXPECT_EQ(cv::countNonZero(rejected.reshape(1)), 0);
}
class Initializer : public ::testing::Test
{
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
  rclcpp::executors::MultiThreadedExecutor executor{rclcpp::ExecutorOptions(), 2};
  std::shared_ptr<rclcpp::Node> peer = std::make_shared<rclcpp::Node>("initializer_peer");
  template <class P>
  bool pump(P p, int ms = 4000)
  {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
      executor.spin_some();
      if (p()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    return p();
  }
  void check(bool missing)
  {
    rclcpp::NodeOptions o;
    o.parameter_overrides(
      {rclcpp::Parameter("angle_resolution", 3),
       rclcpp::Parameter("model_path", missing ? "/nonexistent/yabloc-test.onnx" : model_path())});
    auto node = std::make_shared<yabloc::CameraPoseInitializer>(o);
    executor.add_node(node);
    executor.add_node(peer);
    using Align = autoware_internal_localization_msgs::srv::PoseWithCovarianceStamped;
    using Camera = sensor_msgs::msg::CameraInfo;
    using Map = autoware_map_msgs::msg::LaneletMapBin;
    using Image = sensor_msgs::msg::Image;
    auto client = peer->create_client<Align>("/camera_pose_initializer/yabloc_align_srv");
    auto request = std::make_shared<Align::Request>();
    request->pose_with_covariance.header.frame_id = "map";
    request->pose_with_covariance.header.stamp = peer->now();
    auto & p = request->pose_with_covariance.pose;
    p.pose.orientation.z = std::sin(.1);
    p.pose.orientation.w = std::cos(.1);
    p.covariance[35] = .09;
    auto call = [&] {
      auto f = client->async_send_request(request);
      bool ready =
        pump([&] { return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; });
      EXPECT_TRUE(ready);
      return ready ? f.get() : std::make_shared<Align::Response>();
    };
    ASSERT_TRUE(pump([&] { return client->service_is_ready(); }));
    EXPECT_FALSE(call()->success);
    tf2_ros::StaticTransformBroadcaster tf(peer);
    geometry_msgs::msg::TransformStamped t;
    t.header.frame_id = "base_link";
    t.child_frame_id = "camera";
    t.transform.translation.z = 1;
    t.transform.rotation.x = 1;
    t.transform.rotation.w = 0;
    tf.sendTransform(t);
    auto info = peer->create_publisher<Camera>("/camera_pose_initializer/input/camera_info", 10);
    auto map = peer->create_publisher<Map>(
      "/camera_pose_initializer/input/vector_map", rclcpp::QoS(1).transient_local());
    auto image = peer->create_publisher<Image>("/camera_pose_initializer/input/image_raw", 10);
    std::optional<visualization_msgs::msg::MarkerArray> markers;
    auto sub = peer->create_subscription<visualization_msgs::msg::MarkerArray>(
      "/camera_pose_initializer/debug/init_candidates", 10,
      [&](visualization_msgs::msg::MarkerArray::ConstSharedPtr p) { markers = *p; });
    auto lm = std::make_shared<lanelet::LaneletMap>();
    using lanelet::utils::getId;
    lanelet::LineString3d left(
      getId(), {lanelet::Point3d(getId(), -10, 2, 0), lanelet::Point3d(getId(), 10, 2, 0)}),
      right(getId(), {lanelet::Point3d(getId(), -10, -2, 0), lanelet::Point3d(getId(), 10, -2, 0)});
    lanelet::Lanelet lane(getId(), left, right);
    lane.attributes()["subtype"] = "road";
    lm->add(lane);
    auto m = autoware::experimental::lanelet2_utils::to_autoware_map_msgs(lm);
    Camera c;
    c.header.frame_id = "camera";
    c.width = 160;
    c.height = 120;
    c.k = {100., 0., 80., 0., 100., 60., 0., 0., 1.};
    cv_bridge::CvImage bridge;
    bridge.encoding = "bgr8";
    bridge.image = cv::Mat(120, 160, CV_8UC3, cv::Scalar(60, 70, 80));
    auto img = bridge.toImageMsg();
    ASSERT_TRUE(pump([&] {
      return info->get_subscription_count() > 0 && map->get_subscription_count() > 0 &&
             image->get_subscription_count() > 0;
    }));
    info->publish(c);
    map->publish(m);
    image->publish(*img);
    pump([] { return false; }, 150);
    auto response = call();
    ASSERT_TRUE(response->success);
    EXPECT_TRUE(response->reliable);
    const auto & out = response->pose_with_covariance;
    EXPECT_EQ(out.header, request->pose_with_covariance.header);
    EXPECT_EQ(out.pose.covariance, p.covariance);
    EXPECT_EQ(out.pose.pose.position, p.pose.position);
    double yaw = 2 * std::atan2(out.pose.pose.orientation.z, out.pose.pose.orientation.w);
    EXPECT_TRUE(std::isfinite(yaw));
    if (missing) {
      EXPECT_NEAR(yaw, .2, 1e-6);
      EXPECT_FALSE(markers.has_value());
    } else {
      EXPECT_GE(yaw, -.10001);
      EXPECT_LE(yaw, .40001);
      ASSERT_TRUE(pump([&] { return markers.has_value(); }));
      ASSERT_EQ(markers->markers.size(), 6U);
      for (const auto & marker : markers->markers) EXPECT_EQ(marker.header.frame_id, "map");
    }
    executor.remove_node(node);
    executor.remove_node(peer);
  }
};
TEST_F(Initializer, ModelBackedAlignmentAndReadiness)
{
  check(false);
}
TEST_F(Initializer, ExplicitMissingModelFallback)
{
  check(true);
}
