// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/lanelet2_utils/conversion.hpp>
#include <opencv2/imgcodecs.hpp>
#include <yabloc_common/cv_decompress.hpp>
#include <yabloc_common/extract_line_segments.hpp>
#include <yabloc_common/ground_plane.hpp>
#include <yabloc_common/ground_server/ground_server.hpp>
#include <yabloc_common/ll2_decomposer/ll2_decomposer.hpp>
#include <yabloc_common/pose_conversions.hpp>
#include <yabloc_common/transform_line_segments.hpp>

#include <gtest/gtest.h>
#include <pcl_conversions/pcl_conversions.h>

#include <chrono>
#include <optional>
#include <thread>
TEST(YablocGeometry, PoseAndSegmentTransforms)
{
  geometry_msgs::msg::Pose pose;
  pose.position.x = 3;
  pose.orientation.w = 1.;
  auto se = yabloc::common::pose_to_se3(pose);
  auto round = yabloc::common::se3_to_pose(se);
  EXPECT_EQ(round, pose);
  pcl::PointCloud<pcl::PointXYZLNormal> lines;
  pcl::PointXYZLNormal line;
  line.getVector3fMap() = Eigen::Vector3f(0, 0, 0);
  line.getNormalVector3fMap() = Eigen::Vector3f(1, 0, 0);
  line.label = 42;
  lines.push_back(line);
  auto transformed = yabloc::common::transform_line_segments(lines, se);
  ASSERT_EQ(transformed.size(), 1U);
  EXPECT_EQ(transformed[0].label, 42);
  EXPECT_FLOAT_EQ(transformed[0].x, 3);
  EXPECT_FLOAT_EQ(transformed[0].normal_x, 4);
}
TEST(YablocGeometry, GroundPlaneSlopeAndMessage)
{
  yabloc::common::GroundPlane ground;
  ground.xyz = Eigen::Vector3f(1, 2, 3);
  ground.normal = Eigen::Vector3f(0, -.6, .8);
  yabloc::common::GroundPlane parsed(ground.msg());
  EXPECT_NEAR(parsed.height(), 3, 1e-6);
  Eigen::Matrix3f identity = Eigen::Matrix3f::Identity();
  auto rotation = parsed.align_with_slope(identity);
  EXPECT_NEAR((rotation.col(2) - ground.normal).norm(), 0, 1e-6);
  EXPECT_NEAR(rotation.determinant(), 1, 1e-6);
}
class CommonNodes : public ::testing::Test
{
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
};
TEST_F(CommonNodes, MapDecompositionAndGround)
{
  rclcpp::NodeOptions d;
  d.parameter_overrides(
    {rclcpp::Parameter("road_marking_labels", std::vector<std::string>{"line_thin"}),
     rclcpp::Parameter("sign_board_labels", std::vector<std::string>{"sign_board"}),
     rclcpp::Parameter("bounding_box_labels", std::vector<std::string>{"bounding_box"})});
  auto decomposer = std::make_shared<yabloc::ll2_decomposer::Ll2Decomposer>(d);
  rclcpp::NodeOptions g;
  g.parameter_overrides(
    {rclcpp::Parameter("force_zero_tilt", false), rclcpp::Parameter("R", 10),
     rclcpp::Parameter("K", 20)});
  auto ground = std::make_shared<yabloc::ground_server::GroundServer>(g);
  auto peer = std::make_shared<rclcpp::Node>("common_peer");
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(decomposer);
  executor.add_node(ground);
  executor.add_node(peer);
  auto pump = [&](auto pred, int ms = 4000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
      executor.spin_some();
      if (pred()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    return pred();
  };
  using Map = autoware_map_msgs::msg::LaneletMapBin;
  using Cloud = sensor_msgs::msg::PointCloud2;
  using Pose = geometry_msgs::msg::PoseStamped;
  using Plane = std_msgs::msg::Float32MultiArray;
  auto map1 =
    peer->create_publisher<Map>("/ll2_to_image/input/vector_map", rclcpp::QoS(1).transient_local());
  auto map2 = peer->create_publisher<Map>(
    "/ground_server/input/vector_map", rclcpp::QoS(1).transient_local());
  auto pose = peer->create_publisher<Pose>("/ground_server/input/pose", 10);
  std::optional<Cloud> roads;
  std::optional<Plane> plane;
  auto sub = peer->create_subscription<Cloud>(
    "/ll2_to_image/output/ll2_road_marking", rclcpp::QoS(1).transient_local(),
    [&](Cloud::ConstSharedPtr p) { roads = *p; });
  auto ps = peer->create_subscription<Plane>(
    "/ground_server/output/ground", 10, [&](Plane::ConstSharedPtr p) { plane = *p; });
  ASSERT_TRUE(pump([&] {
    return map1->get_subscription_count() > 0 && map2->get_subscription_count() > 0 &&
           pose->get_subscription_count() > 0;
  }));
  auto lm = std::make_shared<lanelet::LaneletMap>();
  using lanelet::utils::getId;
  for (int y = -5; y <= 5; ++y) {
    lanelet::LineString3d line(
      getId(), {lanelet::Point3d(getId(), -5, y, 2), lanelet::Point3d(getId(), 5, y, 2)});
    line.attributes()["type"] = "line_thin";
    lm->add(line);
  }
  auto map = autoware::experimental::lanelet2_utils::to_autoware_map_msgs(lm);
  map.header.stamp.sec = 123;
  map1->publish(map);
  map2->publish(map);
  ASSERT_TRUE(pump([&] { return roads.has_value(); }));
  Pose p;
  p.pose.orientation.w = 1.;
  ASSERT_TRUE(pump([&] {
    pose->publish(p);
    return plane.has_value();
  }));
  EXPECT_EQ(roads->header.frame_id, "map");
  EXPECT_EQ(roads->header.stamp, map.header.stamp);
  pcl::PointCloud<pcl::PointNormal> decoded;
  pcl::fromROSMsg(*roads, decoded);
  ASSERT_EQ(decoded.size(), 11U);
  for (const auto & l : decoded) {
    EXPECT_FLOAT_EQ(l.x, -5);
    EXPECT_FLOAT_EQ(l.normal_x, 5);
    EXPECT_FLOAT_EQ(l.z, 2);
  }
  ASSERT_EQ(plane->data.size(), 6U);
  EXPECT_NEAR(plane->data[2], 2., 1e-5);
  EXPECT_NEAR(plane->data[3], 0., 1e-5);
  EXPECT_NEAR(plane->data[4], 0., 1e-5);
  EXPECT_NEAR(plane->data[5], 1., 1e-5);
}

TEST(YablocGeometry, CompressedColorAndBayer)
{
  for (bool bayer : {false, true}) {
    cv::Mat pixels(
      8, 8, bayer ? CV_8UC1 : CV_8UC3, bayer ? cv::Scalar(80) : cv::Scalar(10, 20, 30));
    std::vector<unsigned char> bytes;
    ASSERT_TRUE(cv::imencode(".png", pixels, bytes));
    sensor_msgs::msg::CompressedImage msg;
    msg.format = bayer ? "bayer_rggb8; png compressed" : "bgr8; png compressed";
    msg.data.assign(bytes.begin(), bytes.end());
    auto decoded = yabloc::common::decompress_to_cv_mat(msg);
    EXPECT_EQ(decoded.type(), CV_8UC3);
    EXPECT_EQ(decoded.size(), pixels.size());
    const auto value = decoded.at<cv::Vec3b>(4, 4);
    for (int c = 0; c < 3; ++c) EXPECT_EQ(value[c], bayer ? 80 : 10 * (c + 1));
    EXPECT_EQ(std::vector<unsigned char>(msg.data.begin(), msg.data.end()), bytes);
  }
}
