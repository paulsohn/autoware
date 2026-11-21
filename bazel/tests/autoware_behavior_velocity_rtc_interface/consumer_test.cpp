// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <autoware/behavior_velocity_rtc_interface/scene_module_interface_with_rtc.hpp>

#include <gtest/gtest.h>

namespace bvp = autoware::behavior_velocity_planner;

class ConsumerScene : public bvp::SceneModuleInterfaceWithRTC
{
public:
  ConsumerScene()
  : SceneModuleInterfaceWithRTC(
      42, rclcpp::get_logger("rtc_consumer"), std::make_shared<rclcpp::Clock>(), nullptr, nullptr)
  {
    setRTCEnabled(false);
  }
  using SceneModuleInterfaceWithRTC::setDistance;
  using SceneModuleInterfaceWithRTC::setSafe;
  bool modifyPathVelocity(bvp::PathWithLaneId *) override { return true; }
  visualization_msgs::msg::MarkerArray createDebugMarkerArray() override
  {
    return visualization_msgs::msg::MarkerArray{};
  }
  std::vector<autoware::motion_utils::VirtualWall> createVirtualWalls() override { return {}; }
};

TEST(RtcConsumer, SafetyAndApprovalRemainSeparate)
{
  ConsumerScene scene;
  EXPECT_EQ(scene.getModuleId(), 42);
  EXPECT_FALSE(scene.isSafe());
  EXPECT_FALSE(scene.isActivated());
  scene.setSafe(true);
  EXPECT_TRUE(scene.isActivated());
  scene.setRTCEnabled(true);
  scene.setActivation(false);
  scene.setSafe(true);
  EXPECT_TRUE(scene.isSafe());
  EXPECT_FALSE(scene.isActivated());
  scene.setActivation(true);
  EXPECT_TRUE(scene.isActivated());
  scene.setDistance(7.0);
  EXPECT_DOUBLE_EQ(scene.getDistance(), 7.0);
  scene.setRTCEnabled(false);
  scene.setSafe(false);
  EXPECT_FALSE(scene.isActivated());
}
