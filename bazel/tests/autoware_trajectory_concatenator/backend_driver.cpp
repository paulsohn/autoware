// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#define main original_rclcpp_main
#include <test/test_trajectory_concatenator_wrapper.cpp>
#undef main
int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  autoware::agnocast_wrapper::init(argc, argv, true);
  if (autoware::agnocast_wrapper::use_agnocast()) EXPECT_FALSE(rclcpp::ok());
  int result = RUN_ALL_TESTS();
  autoware::agnocast_wrapper::shutdown();
  return result;
}
