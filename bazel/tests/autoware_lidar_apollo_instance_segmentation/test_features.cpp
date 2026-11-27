// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/lidar_apollo_instance_segmentation/cluster2d.hpp>
#include <autoware/lidar_apollo_instance_segmentation/feature_generator.hpp>

#include <autoware_perception_msgs/msg/object_classification.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace apollo = autoware::lidar_apollo_instance_segmentation;

TEST(ApolloFeatures, HeightIntensityAndResetAcrossChannelLayouts)
{
  for (const bool intensity : {false, true}) {
    for (const bool constant : {false, true}) {
      SCOPED_TRACE(::testing::Message() << intensity << ", " << constant);
      apollo::FeatureGenerator generator(8, 8, 4, intensity, constant);
      auto cloud = std::make_shared<pcl::PointCloud<pcl::PointXYZI>>();
      cloud->emplace_back(1.1F, 1.1F, 1.F, 255.F);
      cloud->emplace_back(1.2F, 1.2F, 3.F, 127.5F);
      cloud->emplace_back(1.1F, 1.1F, 6.F, 255.F);  // Outside the height interval.
      cloud->emplace_back(20.F, 20.F, 1.F, 255.F);  // Outside the map.
      auto map = generator.generate(cloud);
      constexpr int cell = 2 * 8 + 2;
      EXPECT_FLOAT_EQ(map->max_height_data[cell], 3.F);
      EXPECT_FLOAT_EQ(map->mean_height_data[cell], 2.F);
      EXPECT_NEAR(map->count_data[cell], std::log(3.F), 1e-6F);
      EXPECT_FLOAT_EQ(map->nonempty_data[cell], 1.F);
      if (intensity) {
        EXPECT_FLOAT_EQ(map->top_intensity_data[cell], .5F);
        EXPECT_FLOAT_EQ(map->mean_intensity_data[cell], .75F);
      }
      cloud->clear();
      map = generator.generate(cloud);
      for (int i = 0; i < 64; ++i) {
        EXPECT_FLOAT_EQ(map->max_height_data[i], 0.F);
        EXPECT_FLOAT_EQ(map->mean_height_data[i], 0.F);
        EXPECT_FLOAT_EQ(map->count_data[i], 0.F);
        EXPECT_FLOAT_EQ(map->nonempty_data[i], 0.F);
      }
    }
  }
}

TEST(ApolloClustering, ClassifiesConfidentClusterAndRetainsHeader)
{
  auto points = std::make_shared<pcl::PointCloud<pcl::PointXYZI>>();
  points->emplace_back(1.1F, 1.1F, 0.F, 10.F);
  points->emplace_back(1.2F, 1.2F, 1.F, 20.F);
  points->emplace_back(1.3F, 1.3F, 1.2F, 30.F);
  pcl::PointIndices valid;
  valid.indices = {0, 1, 2};
  constexpr int size = 64, cell = 18;
  std::vector<float> output(12 * size, 0.F);
  output[cell] = 1.F;
  output[3 * size + cell] = 10.F;
  output[5 * size + cell] = 1.F;  // Small motor vehicle class.
  output[9 * size + cell] = 1.F;
  output[11 * size + cell] = 2.F;
  apollo::Cluster2D cluster(8, 8, 4.F);
  cluster.cluster(output.data(), points, valid, .5F, false);
  tier4_perception_msgs::msg::DetectedObjectsWithFeature objects;
  std_msgs::msg::Header header;
  header.frame_id = "base_link";
  header.stamp.sec = 42;
  cluster.getObjects(.5F, .5F, 3, objects, header);
  EXPECT_EQ(objects.header, header);
  ASSERT_EQ(objects.feature_objects.size(), 1U);
  const auto & result = objects.feature_objects.front();
  EXPECT_GT(result.object.existence_probability, .99F);
  ASSERT_EQ(result.object.classification.size(), 1U);
  EXPECT_EQ(
    result.object.classification.front().label,
    autoware_perception_msgs::msg::ObjectClassification::CAR);
  EXPECT_EQ(result.feature.cluster.header, header);
  EXPECT_EQ(result.feature.cluster.width * result.feature.cluster.height, 2U);
}
