// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <ament_index_cpp/get_package_prefix.hpp>
#include <class_loader/class_loader.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <rclcpp_components/node_factory.hpp>
#include <tf2_ros/static_transform_broadcaster.hpp>
#include <yabloc_common/ground_plane.hpp>

#include <geometry_msgs/msg/pose_stamped.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <visualization_msgs/msg/marker.hpp>

#include <gtest/gtest.h>
#include <pcl_conversions/pcl_conversions.h>

#include <chrono>
#include <optional>
#include <thread>
using Image = sensor_msgs::msg::Image;
using Camera = sensor_msgs::msg::CameraInfo;
using Cloud = sensor_msgs::msg::PointCloud2;
class ImageComponents : public ::testing::Test
{
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
  std::unique_ptr<class_loader::ClassLoader> loader;
  rclcpp::executors::SingleThreadedExecutor executor;
  std::vector<rclcpp_components::NodeInstanceWrapper> nodes;
  std::shared_ptr<rclcpp::Node> peer;
  void SetUp() override
  {
    loader = std::make_unique<class_loader::ClassLoader>(
      ament_index_cpp::get_package_prefix("yabloc_image_processing") +
      "/lib/libyabloc_image_processing.so");
    peer = std::make_shared<rclcpp::Node>("image_peer");
    executor.add_node(peer);
  }
  void load(const std::string & type, std::vector<rclcpp::Parameter> parameters = {})
  {
    rclcpp::NodeOptions o;
    o.parameter_overrides(parameters);
    for (const auto & c : loader->getAvailableClasses<rclcpp_components::NodeFactory>())
      if (c.find(type) != std::string::npos) {
        auto f = loader->createInstance<rclcpp_components::NodeFactory>(c);
        nodes.push_back(f->create_node_instance(o));
        executor.add_node(nodes.back().get_node_base_interface());
        return;
      }
    FAIL() << "Missing component " << type;
  }
  template <class P>
  bool pump(P pred, int ms = 4000)
  {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
      executor.spin_some();
      if (pred()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    return pred();
  }
  Image image(const cv::Mat & pixels)
  {
    cv_bridge::CvImage p;
    p.encoding = "bgr8";
    p.header.frame_id = "camera";
    p.header.stamp.sec = 123;
    p.image = pixels;
    return *p.toImageMsg();
  }
  Camera camera()
  {
    Camera c;
    c.header.frame_id = "camera";
    c.width = 640;
    c.height = 480;
    c.distortion_model = "plumb_bob";
    c.d = {0., 0., 0., 0., 0.};
    c.k = {400., 0., 320., 0., 400., 240., 0., 0., 1.};
    c.r = {1., 0., 0., 0., 1., 0., 0., 0., 1.};
    c.p = {400., 0., 320., 0., 0., 400., 240., 0., 0., 0., 1., 0.};
    return c;
  }
  void undistort(bool compressed)
  {
    load(
      "UndistortNode",
      {rclcpp::Parameter("width", 320), rclcpp::Parameter("override_frame_id", "rectified"),
       rclcpp::Parameter("use_sensor_qos", false)});
    auto info = peer->create_publisher<Camera>("/undistort/input/camera_info", 10);
    auto raw = peer->create_publisher<Image>("/undistort/input/image_raw", 10);
    auto encoded = peer->create_publisher<sensor_msgs::msg::CompressedImage>(
      "/undistort/input/image_raw/compressed", 10);
    std::optional<Image> output;
    auto sub = peer->create_subscription<Image>(
      "/undistort/output/resized_image", 10, [&](Image::ConstSharedPtr p) { output = *p; });
    cv::Mat pixels(480, 640, CV_8UC3, cv::Scalar(10, 20, 30));
    auto input = image(pixels);
    sensor_msgs::msg::CompressedImage zipped;
    zipped.header = input.header;
    zipped.format = "bgr8; png compressed";
    std::vector<unsigned char> bytes;
    ASSERT_TRUE(cv::imencode(".png", pixels, bytes));
    zipped.data.assign(bytes.begin(), bytes.end());
    ASSERT_TRUE(pump([&] {
      info->publish(camera());
      if (compressed)
        encoded->publish(zipped);
      else
        raw->publish(input);
      return output.has_value();
    }));
    EXPECT_EQ(output->width, 320U);
    EXPECT_EQ(output->height, 240U);
    EXPECT_EQ(output->header.stamp, input.header.stamp);
    EXPECT_EQ(output->header.frame_id, "rectified");
    auto cv = cv_bridge::toCvCopy(*output, "bgr8")->image;
    EXPECT_EQ(cv.at<cv::Vec3b>(100, 100), cv::Vec3b(10, 20, 30));
  }
};
TEST_F(ImageComponents, RawUndistortion)
{
  undistort(false);
}
TEST_F(ImageComponents, CompressedUndistortion)
{
  undistort(true);
}
TEST_F(ImageComponents, DetectionSegmentationAndColoredOverlay)
{
  load("LineSegmentDetector");
  load(
    "GraphSegment",
    {rclcpp::Parameter("target_height_ratio", .7),
     rclcpp::Parameter("target_candidate_box_width", 10), rclcpp::Parameter("sigma", .5),
     rclcpp::Parameter("k", 300.), rclcpp::Parameter("min_size", 20.),
     rclcpp::Parameter("pickup_additional_areas", false)});
  load("LineSegmentsOverlay");
  auto in = peer->create_publisher<Image>("/line_detector/input/image_raw", 10);
  auto graph = peer->create_publisher<Image>("/graph_segment/input/image_raw", 10);
  auto overlay = peer->create_publisher<Image>("/line_segments_overlay/input/image_raw", 10);
  auto edges = peer->create_publisher<Cloud>("/line_segments_overlay/input/line_segments", 10);
  std::optional<Cloud> lines;
  std::optional<Image> mask, colored;
  auto ls = peer->create_subscription<Cloud>(
    "/line_detector/output/line_segments_cloud", 10, [&](Cloud::ConstSharedPtr p) {
      if (p->width > 0) lines = *p;
    });
  auto gs = peer->create_subscription<Image>(
    "/graph_segment/output/mask_image", 10, [&](Image::ConstSharedPtr p) { mask = *p; });
  auto os = peer->create_subscription<Image>(
    "/line_segments_overlay/debug/image_with_colored_line_segments", 10,
    [&](Image::ConstSharedPtr p) { colored = *p; });
  cv::Mat pixels(480, 640, CV_8UC3, cv::Scalar(20, 20, 20));
  cv::rectangle(pixels, cv::Rect(350, 250, 150, 100), cv::Scalar(240, 240, 240), 4);
  auto msg = image(pixels);
  ASSERT_TRUE(pump([&] {
    in->publish(msg);
    graph->publish(msg);
    overlay->publish(msg);
    return lines.has_value() && mask.has_value();
  }));
  EXPECT_EQ(lines->header.stamp, msg.header.stamp);
  auto m = cv_bridge::toCvCopy(*mask, "mono8")->image;
  EXPECT_GT(cv::countNonZero(m), 0);
  EXPECT_EQ(m.cols, 640);
  EXPECT_EQ(m.rows, 480);
  pcl::PointCloud<pcl::PointXYZLNormal> segments;
  pcl::PointXYZLNormal p;
  p.getVector3fMap() = Eigen::Vector3f(100, 100, 0);
  p.getNormalVector3fMap() = Eigen::Vector3f(200, 100, 0);
  p.label = 255;
  segments.push_back(p);
  Cloud cloud;
  pcl::toROSMsg(segments, cloud);
  cloud.header = msg.header;
  ASSERT_TRUE(pump([&] {
    edges->publish(cloud);
    return colored.has_value();
  }));
  auto c = cv_bridge::toCvCopy(*colored, "bgr8")->image;
  EXPECT_EQ(c.at<cv::Vec3b>(100, 150), cv::Vec3b(0, 255, 0));
}
TEST_F(ImageComponents, GroundProjectionAndMapOverlay)
{
  load(
    "SegmentFilter",
    {rclcpp::Parameter("image_size", 200), rclcpp::Parameter("max_range", 10.),
     rclcpp::Parameter("min_segment_length", .01), rclcpp::Parameter("max_segment_distance", 10.),
     rclcpp::Parameter("max_lateral_distance", 5.)});
  load("Lanelet2Overlay");
  tf2_ros::StaticTransformBroadcaster tf(peer);
  geometry_msgs::msg::TransformStamped t;
  t.header.frame_id = "base_link";
  t.child_frame_id = "camera";
  t.transform.rotation.x = 1.;
  t.transform.rotation.w = 0.;
  t.transform.translation.z = 1.;
  tf.sendTransform(t);
  auto ci1 = peer->create_publisher<Camera>("/segment_filter/input/camera_info", 10);
  auto ci2 = peer->create_publisher<Camera>("/lanelet2_overlay/input/camera_info", 10);
  auto lines = peer->create_publisher<Cloud>("/segment_filter/input/line_segments_cloud", 10);
  auto mask = peer->create_publisher<Image>("/segment_filter/input/graph_segmented", 10);
  auto pose =
    peer->create_publisher<geometry_msgs::msg::PoseStamped>("/lanelet2_overlay/input/pose", 10);
  auto road = peer->create_publisher<Cloud>("/lanelet2_overlay/input/ll2_road_marking", 10);
  auto frame = peer->create_publisher<Image>("/lanelet2_overlay/input/image_raw", 10);
  auto projected =
    peer->create_publisher<Cloud>("/lanelet2_overlay/input/projected_line_segments_cloud", 10);
  std::optional<Cloud> out;
  std::optional<Image> vis;
  std::optional<visualization_msgs::msg::Marker> marker;
  auto s = peer->create_subscription<Cloud>(
    "/segment_filter/output/projected_line_segments_cloud", 10, [&](Cloud::ConstSharedPtr p) {
      if (p->width > 0) out = *p;
    });
  auto vs = peer->create_subscription<Image>(
    "/lanelet2_overlay/debug/lanelet2_overlay_image", 10,
    [&](Image::ConstSharedPtr p) { vis = *p; });
  auto ms = peer->create_subscription<visualization_msgs::msg::Marker>(
    "/lanelet2_overlay/debug/projected_marker", 10,
    [&](visualization_msgs::msg::Marker::ConstSharedPtr p) { marker = *p; });
  pcl::PointCloud<pcl::PointNormal> pixels;
  pcl::PointNormal line;
  line.getVector3fMap() = Eigen::Vector3f(400, 300, 0);
  line.getNormalVector3fMap() = Eigen::Vector3f(500, 300, 0);
  pixels.push_back(line);
  Cloud cloud;
  pcl::toROSMsg(pixels, cloud);
  cloud.header.stamp.sec = 123;
  cloud.header.frame_id = "camera";
  cv_bridge::CvImage mono;
  mono.header = cloud.header;
  mono.encoding = "mono8";
  mono.image = cv::Mat(480, 640, CV_8UC1, cv::Scalar(255));
  auto white = *mono.toImageMsg();
  ASSERT_TRUE(pump([&] {
    ci1->publish(camera());
    ci2->publish(camera());
    lines->publish(cloud);
    mask->publish(white);
    return out.has_value();
  }));
  pcl::PointCloud<pcl::PointXYZLNormal> result;
  pcl::fromROSMsg(*out, result);
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].label, 255U);
  EXPECT_NEAR(result[0].x, .2, 1e-5);
  EXPECT_NEAR(result[0].y, -.15, 1e-5);
  EXPECT_NEAR(result[0].normal_x, .45, 1e-5);
  EXPECT_NEAR(result[0].z, 0, 1e-5);
  geometry_msgs::msg::PoseStamped self;
  self.header.stamp.sec = 123;
  self.pose.orientation.w = 1.;
  Cloud ground_cloud = *out;
  ASSERT_TRUE(pump([&] {
    pose->publish(self);
    road->publish(ground_cloud);
    frame->publish(image(cv::Mat::zeros(480, 640, CV_8UC3)));
    projected->publish(*out);
    return vis.has_value() && marker.has_value() &&
           cv_bridge::toCvCopy(*vis, "bgr8")->image.at<cv::Vec3b>(300, 450)[1] > 150;
  }));
  EXPECT_EQ(marker->points.size(), 2U);
  EXPECT_EQ(marker->header.frame_id, "map");
  EXPECT_EQ(marker->header.stamp, cloud.header.stamp);
  auto rendered = cv_bridge::toCvCopy(*vis, "bgr8")->image;
  EXPECT_EQ(rendered.at<cv::Vec3b>(300, 450), cv::Vec3b(1, 205, 205));
}
