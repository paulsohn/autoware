// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <ament_index_cpp/get_package_share_path.hpp>
#include <autoware/tensorrt_yolox/label.hpp>
#include <autoware/tensorrt_yolox/tensorrt_yolox_detector.hpp>
#include <autoware/tensorrt_yolox/tensorrt_yolox_node.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <rclcpp/rclcpp.hpp>

#include <sensor_msgs/image_encodings.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;
using autoware::tensorrt_yolox::TrtYoloXDetector;
using autoware::tensorrt_yolox::TrtYoloXDetectorConfig;
using autoware::tensorrt_yolox::TrtYoloXNode;

class YoloXCache : public testing::Test
{
protected:
  void SetUp() override
  {
    int count = 0;
    ASSERT_EQ(cudaGetDeviceCount(&count), cudaSuccess);
    ASSERT_GT(count, 0);
    directory = fs::path(std::getenv("TEST_TMPDIR")) /
                testing::UnitTest::GetInstance()->current_test_info()->name();
    fs::create_directories(directory / "models");
    fs::create_directories(directory / "cache");
    const fs::path source =
      ament_index_cpp::get_package_share_path("autoware_tensorrt_yolox_models");
    package = ament_index_cpp::get_package_share_path("autoware_tensorrt_yolox");
    model = directory / "models/detector.onnx";
    engine = directory / "cache/detector.engine";
    fs::copy_file(source / "yolox_s_car_ped_tl_detector_960_960_batch_1.onnx", model);
    supplied = directory / "models/detector.EntropyV2-calibration.table";
    fs::copy_file(
      source / "yolox_s_car_ped_tl_detector_960_960_batch_1.EntropyV2-calibration.table", supplied);
    supplied_bytes = read(supplied);
    fs::permissions(supplied, fs::perms::owner_read);
    fs::permissions(model, fs::perms::owner_read);
    fs::permissions(model.parent_path(), fs::perms::owner_read | fs::perms::owner_exec);
    std::ofstream forbidden(model.parent_path() / "cannot_write");
    ASSERT_FALSE(forbidden.is_open());
    config.model_path = model;
    config.engine_path = engine;
    config.precision = "fp16";
    config.score_threshold = 0.35F;
    config.nms_threshold = 0.7F;
    config.calibration_algorithm = "Entropy";
    config.dla_core_id = -1;
    config.clip_value = 6.0;
    config.roi_labels = autoware::tensorrt_yolox::load_label_maps(
      source / "car_ped_tl_detector_labels.txt",
      package / "config/traffic_light_roi_label_remap.csv", "");
  }

  void TearDown() override
  {
    if (rclcpp::ok()) rclcpp::shutdown();
    if (directory.empty()) return;
    fs::permissions(model.parent_path(), fs::perms::owner_all);
    fs::permissions(directory / "cache", fs::perms::owner_all);
    fs::remove_all(directory);
  }

  void infer(TrtYoloXDetector & detector)
  {
    auto image = cv::imread((package / "test/test_image.jpg").string());
    ASSERT_FALSE(image.empty());
    std_msgs::msg::Header header;
    header.frame_id = "cache_test";
    const auto message =
      cv_bridge::CvImage(header, sensor_msgs::image_encodings::BGR8, image).toImageMsg();
    const auto output = detector.detect(*message);
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->image.header.frame_id, "cache_test");
    EXPECT_FALSE(output->objects.feature_objects.empty());
    if (!config.semseg_color_map.empty()) {
      ASSERT_TRUE(output->mask.has_value());
      ASSERT_TRUE(output->color_mask.has_value());
      EXPECT_GT(output->mask->width, 0U);
      EXPECT_GT(output->mask->height, 0U);
      EXPECT_EQ(output->color_mask->encoding, sensor_msgs::image_encodings::BGR8);
    }
  }

  void use_segmentation_model()
  {
    const auto source = ament_index_cpp::get_package_share_path("autoware_tensorrt_yolox_models");
    fs::permissions(model, fs::perms::owner_read | fs::perms::owner_write);
    fs::copy_file(
      source / "yolox-sPlus-opt-pseudoV2-T4-960x960-T4-seg16cls.onnx", model,
      fs::copy_options::overwrite_existing);
    fs::permissions(model, fs::perms::owner_read);
    fs::permissions(supplied, fs::perms::owner_read | fs::perms::owner_write);
    fs::copy_file(
      source / "yolox-sPlus-opt-pseudoV2-T4-960x960-T4-seg16cls.EntropyV2-calibration.table",
      supplied, fs::copy_options::overwrite_existing);
    supplied_bytes = read(supplied);
    fs::permissions(supplied, fs::perms::owner_read);
    config.roi_labels = autoware::tensorrt_yolox::load_label_maps(
      source / "label.txt", package / "config/roi_label_remap.csv",
      package / "config/roi_to_semseg_label_remap.csv");
    config.semseg_color_map =
      autoware::tensorrt_yolox::load_segmentation_colormap(source / "semseg_color_map.csv");
    config.is_roi_overlap_semseg = true;
    config.overlap_roi_score_threshold = 0.3F;
    config.is_publish_color_mask = true;
  }

  static std::string read(const fs::path & path)
  {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  void check_sources(int count = 2) const
  {
    EXPECT_EQ(
      std::distance(fs::directory_iterator(model.parent_path()), fs::directory_iterator()), count);
    EXPECT_EQ(read(supplied), supplied_bytes);
    EXPECT_TRUE(fs::exists(directory / "cache/detector.json"));
  }

  fs::path directory, model, engine, package, supplied;
  std::string supplied_bytes;
  TrtYoloXDetectorConfig config{};
};

TEST_F(YoloXCache, ReadOnlyModelSupportsColdAndWarmInference)
{
  {
    TrtYoloXDetector detector(config);
    infer(detector);
  }
  ASSERT_TRUE(fs::exists(engine));
  const auto changed = fs::last_write_time(engine);
  const auto bytes = fs::file_size(engine);
  {
    TrtYoloXDetector detector(config);
    infer(detector);
  }
  EXPECT_EQ(fs::last_write_time(engine), changed);
  EXPECT_EQ(fs::file_size(engine), bytes);
  check_sources();
}

TEST_F(YoloXCache, BuildOnlyPreparesTheConfiguredCacheAndExits)
{
  for (int pass = 0; pass < 2; ++pass) {
    rclcpp::init(0, nullptr);
    rclcpp::NodeOptions options;
    options.arguments(
      {"--ros-args", "--params-file",
       (package / "config/yolox_traffic_light_detector.param.yaml").string()});
    options.append_parameter_override("model_path", model.string());
    options.append_parameter_override("engine_path", engine.string());
    const fs::path source =
      ament_index_cpp::get_package_share_path("autoware_tensorrt_yolox_models");
    options.append_parameter_override(
      "label_path", (source / "car_ped_tl_detector_labels.txt").string());
    options.append_parameter_override(
      "roi_remap_path", (package / "config/traffic_light_roi_label_remap.csv").string());
    options.append_parameter_override("build_only", true);
    const auto changed = pass ? fs::last_write_time(engine) : fs::file_time_type{};
    TrtYoloXNode node(options);
    EXPECT_FALSE(rclcpp::ok());
    ASSERT_TRUE(fs::exists(engine));
    EXPECT_EQ(node.get_parameter("engine_path").as_string(), engine.string());
    if (pass) EXPECT_EQ(fs::last_write_time(engine), changed);
  }
  check_sources();
}

TEST_F(YoloXCache, Int8CalibrationWritesAlongsideTheEngine)
{
  use_segmentation_model();
  config.precision = "int8";
  config.calibration_algorithm = "MinMax";
  config.clip_value = 0.0;
  const auto list = directory / "calibration_images.txt";
  std::ofstream(list) << (package / "test/test_image.jpg").string() << '\n';
  config.calibration_image_list_path = list;
  {
    TrtYoloXDetector detector(config);
    // This is a calibration/cache-location check, not an INT8 accuracy evaluation.
    infer(detector);
  }
  ASSERT_TRUE(fs::exists(engine));
  const auto table = directory / "cache/detector.MinMax-calibration.table";
  ASSERT_TRUE(fs::exists(table));
  EXPECT_GT(fs::file_size(table), 0U);
  const auto changed = fs::last_write_time(table);
  const auto engine_changed = fs::last_write_time(engine);
  config.calibration_image_list_path.clear();
  fs::remove(list);
  {
    TrtYoloXDetector detector(config);
    infer(detector);
  }
  EXPECT_EQ(fs::last_write_time(table), changed);
  EXPECT_EQ(fs::last_write_time(engine), engine_changed);

  // Publish the current-SDK table with the model, then build a new engine without images.
  const auto provided_minmax = directory / "models/detector.MinMax-calibration.table";
  fs::permissions(model.parent_path(), fs::perms::owner_all);
  fs::copy_file(table, provided_minmax);
  fs::permissions(provided_minmax, fs::perms::owner_read);
  fs::permissions(model.parent_path(), fs::perms::owner_read | fs::perms::owner_exec);
  const auto table_bytes = read(provided_minmax);
  const auto provided_changed = fs::last_write_time(provided_minmax);
  engine = directory / "cache/detector-from-model.engine";
  config.engine_path = engine;
  {
    TrtYoloXDetector detector(config);
    infer(detector);
  }
  ASSERT_TRUE(fs::exists(engine));
  EXPECT_TRUE(fs::exists(directory / "cache/detector-from-model.json"));
  const auto from_model_changed = fs::last_write_time(engine);
  fs::permissions(engine.parent_path(), fs::perms::owner_read | fs::perms::owner_exec);
  {
    TrtYoloXDetector detector(config);
    infer(detector);
  }
  EXPECT_EQ(fs::last_write_time(engine), from_model_changed);
  EXPECT_EQ(fs::last_write_time(provided_minmax), provided_changed);
  EXPECT_EQ(read(provided_minmax), table_bytes);
  check_sources(3);
}

TEST_F(YoloXCache, UnsupportedTrafficLightCalibrationFailsWithoutModifyingModelInputs)
{
  config.precision = "int8";
  config.calibration_algorithm = "MinMax";
  config.clip_value = 0.0;
  const auto list = directory / "calibration_images.txt";
  std::ofstream(list) << (package / "test/test_image.jpg").string() << '\n';
  config.calibration_image_list_path = list;
  try {
    TrtYoloXDetector detector(config);
    FAIL() << "Revisit this pinned SDK regression if the compiler now accepts the model";
  } catch (const std::runtime_error & error) {
    EXPECT_STREQ(error.what(), "Failed to setup TensorRT engine with calibrator");
  }
  EXPECT_FALSE(fs::exists(engine));
  const auto table = directory / "cache/detector.MinMax-calibration.table";
  ASSERT_TRUE(fs::exists(table));
  EXPECT_GT(fs::file_size(table), 0U);
  EXPECT_EQ(read(supplied), supplied_bytes);
  EXPECT_EQ(
    std::distance(fs::directory_iterator(model.parent_path()), fs::directory_iterator()), 2);
}

TEST_F(YoloXCache, InvalidBuilderOptimizationLevelIsRejected)
{
  for (auto level : {int64_t{-1}, int64_t{6}, INT64_MAX}) {
    config.builder_optimization_level = level;
    EXPECT_THROW({ TrtYoloXDetector detector(config); }, std::invalid_argument);
  }
}

TEST_F(YoloXCache, TrafficLightInt8AtLevelZeroSupportsInferenceAndBuildOnlyReuse)
{
  config.precision = "int8";
  config.calibration_algorithm = "MinMax";
  config.clip_value = 0.0;
  config.builder_optimization_level = 0;
  engine = directory / "cache/traffic-light-minmax-opt0.engine";
  config.engine_path = engine;
  const auto list = directory / "calibration_images.txt";
  std::ofstream(list) << (package / "test/test_image.jpg").string() << '\n';
  config.calibration_image_list_path = list;
  {
    TrtYoloXDetector detector(config);
    infer(detector);
  }
  ASSERT_TRUE(fs::exists(engine));
  const auto changed = fs::last_write_time(engine);
  config.calibration_image_list_path.clear();
  fs::remove(list);
  {
    TrtYoloXDetector detector(config);
    infer(detector);
  }
  EXPECT_EQ(fs::last_write_time(engine), changed);
  rclcpp::init(0, nullptr);
  const auto source = ament_index_cpp::get_package_share_path("autoware_tensorrt_yolox_models");
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file",
     (package / "config/yolox_traffic_light_detector.param.yaml").string()});
  options.append_parameter_override("model_path", model.string());
  options.append_parameter_override("engine_path", engine.string());
  options.append_parameter_override("precision", "int8");
  options.append_parameter_override("calibration_algorithm", "MinMax");
  options.append_parameter_override("clip_value", 0.0);
  options.append_parameter_override("builder_optimization_level", 0);
  options.append_parameter_override(
    "label_path", (source / "car_ped_tl_detector_labels.txt").string());
  options.append_parameter_override(
    "roi_remap_path", (package / "config/traffic_light_roi_label_remap.csv").string());
  options.append_parameter_override("build_only", true);
  TrtYoloXNode node(options);
  EXPECT_FALSE(rclcpp::ok());
  EXPECT_EQ(node.get_parameter("builder_optimization_level").as_int(), 0);
  EXPECT_EQ(fs::last_write_time(engine), changed);
  EXPECT_EQ(read(supplied), supplied_bytes);
  EXPECT_EQ(
    std::distance(fs::directory_iterator(model.parent_path()), fs::directory_iterator()), 2);
}

TEST_F(YoloXCache, PublishedSegmentationInt8ProfileSupportsColdAndWarmInference)
{
  use_segmentation_model();
  config.precision = "int8";
  {
    TrtYoloXDetector detector(config);
    infer(detector);
  }
  ASSERT_TRUE(fs::exists(engine));
  const auto changed = fs::last_write_time(engine);
  {
    TrtYoloXDetector detector(config);
    infer(detector);
  }
  EXPECT_EQ(fs::last_write_time(engine), changed);
  check_sources();
}
}  // namespace
