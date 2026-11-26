// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/landmark_manager/landmark_manager.hpp>
#include <autoware/lanelet2_utils/conversion.hpp>

#include <gtest/gtest.h>
#include <lanelet2_core/LaneletMap.h>

#include <memory>
autoware_map_msgs::msg::LaneletMapBin::ConstSharedPtr map_fixture()
{
  using namespace lanelet;
  auto map = std::make_shared<LaneletMap>();
  for (int i = 0; i < 3; ++i) {
    const double x = 10. + i * 20;
    Polygon3d p(
      utils::getId(), {Point3d(utils::getId(), x - 1, -1, 0), Point3d(utils::getId(), x + 1, -1, 0),
                       Point3d(utils::getId(), x + 1, 1, 0), Point3d(utils::getId(), x - 1, 1, 0)});
    p.attributes()["type"] = "pose_marker";
    p.attributes()["subtype"] = i == 2 ? "other" : "artag";
    p.attributes()["marker_id"] = std::to_string(i + 1);
    map->add(p);
  }
  return std::make_shared<autoware_map_msgs::msg::LaneletMapBin>(
    autoware::experimental::lanelet2_utils::to_autoware_map_msgs(map));
}
TEST(Landmarks, ParsesSubtypeAndTargetIdsAndPublishesGeometry)
{
  landmark_manager::LandmarkManager all;
  all.parse_landmarks(map_fixture(), "artag");
  const auto landmarks = all.get_landmarks();
  ASSERT_EQ(landmarks.size(), 2U);
  EXPECT_EQ(landmarks[0].id, "1");
  EXPECT_DOUBLE_EQ(landmarks[0].pose.position.x, 10.);
  EXPECT_DOUBLE_EQ(landmarks[0].pose.orientation.w, 1.);
  landmark_manager::LandmarkManager selected;
  selected.parse_landmarks(map_fixture(), "artag", {"2"});
  const auto only = selected.get_landmarks();
  ASSERT_EQ(only.size(), 1U);
  EXPECT_EQ(only[0].id, "2");
  EXPECT_DOUBLE_EQ(only[0].pose.position.x, 30.);
  const auto markers = selected.get_landmarks_as_marker_array_msg();
  ASSERT_EQ(markers.markers.size(), 2U);
  EXPECT_EQ(markers.markers[0].type, visualization_msgs::msg::Marker::CUBE);
  EXPECT_EQ(markers.markers[0].header.frame_id, "map");
  EXPECT_EQ(markers.markers[1].text, "(2)");
  EXPECT_DOUBLE_EQ(markers.markers[1].pose.position.x, 30.);
}
TEST(Landmarks, RecoversTranslationWithAndWithoutOrientation)
{
  landmark_manager::LandmarkManager manager;
  manager.parse_landmarks(map_fixture(), "artag", {"1"});
  geometry_msgs::msg::Pose self;
  self.orientation.w = 1.;
  self.position.x = 3.;
  landmark_manager::Landmark detected;
  detected.id = "1";
  detected.pose.orientation.w = 1.;
  detected.pose.position.x = 8.;
  for (bool orientation : {false, true}) {
    auto pose = manager.calculate_new_self_pose({detected}, self, orientation);
    EXPECT_NEAR(pose.position.x, 2., 1e-6);
    EXPECT_NEAR(pose.position.y, 0., 1e-6);
    EXPECT_NEAR(pose.orientation.w, 1., 1e-6);
  }
}
