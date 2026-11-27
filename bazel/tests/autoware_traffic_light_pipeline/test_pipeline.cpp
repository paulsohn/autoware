// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "traffic_light_recognition/traffic_light_recognition.hpp"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/lanelet2_utils/conversion.hpp>
#include <autoware_lanelet2_extension/regulatory_elements/autoware_traffic_light.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <opencv2/imgcodecs.hpp>
#include <tf2/buffer_core.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>

namespace traffic_light = autoware::traffic_light;
namespace fs = std::filesystem;

TEST(ComposedTrafficLightPipeline, RealImageMapSelectionBothClassifiersAndReset)
{
  const char * root_env = std::getenv("TL_PIPELINE_TEST_DATA_DIR");
  ASSERT_NE(root_env, nullptr);
  const fs::path root(root_env);
  traffic_light::TrafficLightRecognitionConfig config{};
  config.whole_image_detector_model_path =
    root / "tensorrt_yolox/yolox_s_car_ped_tl_detector_960_960_batch_1.onnx";
  config.whole_image_detector_label_path = root / "tensorrt_yolox/car_ped_tl_detector_labels.txt";
  config.whole_image_detector_roi_remap_path =
    ament_index_cpp::get_package_share_directory("autoware_tensorrt_yolox") +
    "/config/traffic_light_roi_label_remap.csv";
  config.whole_image_detector_precision = "fp16";
  config.whole_image_detector_score_threshold = .35F;
  config.whole_image_detector_nms_threshold = .7F;
  config.min_timestamp_offset = -.3;
  config.max_timestamp_offset = 0.;
  config.car_classifier = {
    (root / "traffic_light_classifier/traffic_light_classifier_mobilenetv2_batch_6.onnx").string(),
    (root / "traffic_light_classifier/lamp_labels.txt").string(),
    "fp16",
    {123.675F, 116.28F, 103.53F},
    {58.395F, 57.12F, 57.375F}};
  config.pedestrian_classifier = {
    (root / "traffic_light_classifier/ped_traffic_light_classifier_mobilenetv2_batch_6.onnx")
      .string(),
    (root / "traffic_light_classifier/lamp_labels_ped.txt").string(),
    "fp16",
    {123.675F, 116.28F, 103.53F},
    {58.395F, 57.12F, 57.375F}};
  config.over_exposure_threshold = .85;
  config.under_exposure_threshold = -.83;
  config.diagnostics_node_name = "composed_pipeline_fixture";

  auto pixels = cv::imread(
    ament_index_cpp::get_package_share_directory("autoware_tensorrt_yolox") +
    "/test/test_image.jpg");
  ASSERT_FALSE(pixels.empty());
  std_msgs::msg::Header header;
  header.frame_id = "camera_optical";
  header.stamp = rclcpp::Time(1000, 0);
  auto image = cv_bridge::CvImage(header, "bgr8", pixels).toImageMsg();
  sensor_msgs::msg::CameraInfo camera;
  camera.header = header;
  camera.width = pixels.cols;
  camera.height = pixels.rows;
  camera.distortion_model = "plumb_bob";
  camera.d = {0., 0., 0., 0., 0.};
  camera.k = {800., 0., 640., 0., 800., 413.5, 0., 0., 1.};
  camera.r = {1., 0., 0., 0., 1., 0., 0., 0., 1.};
  camera.p = {800., 0., 640., 0., 0., 800., 413.5, 0., 0., 0., 1., 0.};
  tf2::BufferCore transforms;
  geometry_msgs::msg::TransformStamped pose;
  pose.header.frame_id = "map";
  pose.header.stamp = header.stamp;
  pose.child_frame_id = header.frame_id;
  pose.transform.rotation.x = -.5;
  pose.transform.rotation.y = .5;
  pose.transform.rotation.z = -.5;
  pose.transform.rotation.w = .5;
  ASSERT_TRUE(transforms.setTransform(pose, "fixture", true));
  traffic_light::TrafficLightRecognition recognition(config, transforms);
  EXPECT_FALSE(recognition.run(*image, camera).has_value());
  EXPECT_FALSE(recognition.set_route(autoware_planning_msgs::msg::LaneletRoute{}).has_value());

  using namespace lanelet;
  utils::registerId(10000);
  auto left = LineString3d(
    utils::getId(), {Point3d(utils::getId(), 0., 2., 0.), Point3d(utils::getId(), 30., 2., 0.)});
  auto right = LineString3d(
    utils::getId(), {Point3d(utils::getId(), 0., -2., 0.), Point3d(utils::getId(), 30., -2., 0.)});
  auto map = std::make_shared<LaneletMap>();
  for (auto [id, category] : {std::pair{10L, "road"}, std::pair{20L, "crosswalk"}}) {
    Lanelet lane(id, left, right);
    lane.attributes()["subtype"] = category;
    const auto signal_id = id == 10 ? 1001 : 1002;
    LineString3d housing(
      signal_id, {Point3d(utils::getId(), 20., .9, 6.4), Point3d(utils::getId(), 20., -.9, 6.4)});
    housing.attributes()["subtype"] = "red_yellow_green";
    housing.attributes()["height"] = ".8";
    lane.addRegulatoryElement(
      lanelet::autoware::AutowareTrafficLight::make(utils::getId(), {}, {housing}));
    map->add(lane);
  }
  auto message = ::autoware::experimental::lanelet2_utils::to_autoware_map_msgs(map);
  recognition.set_map(message);
  const auto result = recognition.run(*image, camera);
  ASSERT_TRUE(result.has_value()) << result.error();
  ASSERT_EQ(result->selected_rois.rois.size(), 2U);
  std::set<std::pair<int64_t, uint8_t>> identities;
  for (const auto & roi : result->selected_rois.rois) {
    EXPECT_GT(roi.roi.width, 0U);
    EXPECT_GT(roi.roi.height, 0U);
    EXPECT_LE(roi.roi.x_offset + roi.roi.width, camera.width);
    EXPECT_LE(roi.roi.y_offset + roi.roi.height, camera.height);
    identities.emplace(roi.traffic_light_id, roi.traffic_light_type);
  }
  const std::set<std::pair<int64_t, uint8_t>> expected{{1001, 0}, {1002, 1}};
  EXPECT_EQ(identities, expected);
  ASSERT_EQ(result->merged_signals.signals.size(), 2U);
  identities.clear();
  for (const auto & signal : result->merged_signals.signals) {
    identities.emplace(signal.traffic_light_id, signal.traffic_light_type);
    ASSERT_FALSE(signal.elements.empty());
    for (const auto & element : signal.elements) EXPECT_TRUE(std::isfinite(element.confidence));
  }
  EXPECT_EQ(identities, expected);
  EXPECT_EQ(result->merged_signals.header, header);
  EXPECT_EQ(result->selected_rois.header, header);
  ASSERT_EQ(result->diagnostics.status.size(), 1U);

  auto empty = std::make_shared<LaneletMap>();
  recognition.set_map(::autoware::experimental::lanelet2_utils::to_autoware_map_msgs(empty));
  const auto reset = recognition.run(*image, camera);
  ASSERT_TRUE(reset.has_value());
  EXPECT_TRUE(reset->selected_rois.rois.empty());
  EXPECT_TRUE(reset->merged_signals.signals.empty());
}
