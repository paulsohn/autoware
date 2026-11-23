// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/pcl_extensions/voxel_grid_nearest_centroid.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace
{
using Point = pcl::PointXYZI;
using Cloud = pcl::PointCloud<Point>;
using Filter = pcl::VoxelGridNearestCentroid<Point>;

Cloud::Ptr cloud(const std::vector<float> & positions)
{
  auto result = std::make_shared<Cloud>();
  for (size_t i = 0; i < positions.size(); ++i) {
    Point p;
    p.x = positions[i];
    p.y = p.z = 0.1;
    p.intensity = 10 + i;
    result->push_back(p);
  }
  return result;
}

TEST(PclExtensions, SelectsAnOriginalPointNearestTheCentroidAndKeepsItsFields)
{
  Filter filter;
  filter.setLeafSize(1, 1, 1);
  filter.setInputCloud(cloud({0.1, 0.2, 0.9, 2.1, 2.2, 2.9}));
  Cloud output;
  filter.filter(output);
  ASSERT_EQ(output.size(), 2U);
  EXPECT_FLOAT_EQ(output[0].x, 0.2);
  EXPECT_FLOAT_EQ(output[1].x, 2.2);
  EXPECT_FLOAT_EQ(output[0].intensity, 11);
  EXPECT_FLOAT_EQ(output[1].intensity, 14);
  EXPECT_EQ(output.height, 1U);
  EXPECT_TRUE(output.is_dense);
}

TEST(PclExtensions, RejectsSparseVoxelsAndIndexesSearchableLeaves)
{
  Filter filter;
  filter.setLeafSize(1, 1, 1);
  filter.setMinPointPerVoxel(2);
  auto input = cloud({0.1, 0.2, 0.9, 2.1});
  filter.setInputCloud(input);
  Cloud output;
  filter.filter(output, true);
  ASSERT_EQ(output.size(), 1U);
  std::vector<Filter::LeafConstPtr> leaves;
  std::vector<float> distances;
  ASSERT_EQ(filter.nearestKSearch(output[0], 1, leaves, distances), 1);
  ASSERT_EQ(leaves.size(), 1U);
  EXPECT_EQ(leaves[0]->getPointCount(), 3);
  EXPECT_FLOAT_EQ(distances[0], 0);
  EXPECT_EQ(filter.radiusSearch(*input, -1, 1, leaves, distances), 0);
  EXPECT_EQ(filter.nearestKSearch(*input, 10, 1, leaves, distances), 0);
}

TEST(PclExtensions, IgnoresNonFinitePointsInNonDenseInput)
{
  Filter filter;
  filter.setLeafSize(1, 1, 1);
  auto input = cloud({0.1, 0.2, 0.9, std::numeric_limits<float>::quiet_NaN()});
  input->is_dense = false;
  filter.setInputCloud(input);
  Cloud output;
  filter.filter(output);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_FLOAT_EQ(output[0].x, 0.2);
}

TEST(PclExtensions, MissingInputProducesAnEmptyCloud)
{
  Filter filter;
  filter.setLeafSize(1, 1, 1);
  Cloud output;
  filter.filter(output);
  EXPECT_TRUE(output.empty());
  EXPECT_EQ(output.width, 0U);
}

TEST(PclExtensions, FieldLimitsKeepTheRepresentativeOfAcceptedPoints)
{
  Filter filter;
  filter.setLeafSize(1, 1, 1);
  filter.setInputCloud(cloud({0.1, 0.2, 0.9, 2.1}));
  filter.setFilterFieldName("x");
  filter.setFilterLimits(0, 1);
  Cloud output;
  filter.filter(output);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_FLOAT_EQ(output[0].x, 0.2);
  EXPECT_FLOAT_EQ(output[0].intensity, 11);
}

TEST(PclExtensions, RebuildsLeafSearchMappingWhenTheInputChanges)
{
  Filter filter;
  filter.setLeafSize(1, 1, 1);
  filter.setInputCloud(cloud({0.1, 0.2, 0.9}));
  filter.filter(true);
  filter.setInputCloud(cloud({0.1, 0.2, 0.9, 5.1, 5.2, 5.9}));
  Cloud output;
  filter.filter(output, true);
  ASSERT_EQ(output.size(), 2U);
  std::vector<Filter::LeafConstPtr> leaves;
  std::vector<float> distances;
  ASSERT_EQ(filter.radiusSearch(output[1], 0.1, leaves, distances), 1);
  ASSERT_EQ(leaves.size(), 1U);
  EXPECT_GT(leaves[0]->centroid[0], 5);
}
}  // namespace
