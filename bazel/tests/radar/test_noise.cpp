// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "ros_fixture.hpp"

#include <radar_tracks_noise_filter_node.hpp>

#include <radar_msgs/msg/radar_tracks.hpp>

TEST_F(RadarRosTest, PartitionsTracksAndAppliesUpdatedLateralThreshold)
{
  using radar_msgs::msg::RadarTracks;
  rclcpp::NodeOptions options;
  options.append_parameter_override("velocity_y_threshold", 3.0);
  auto filter = attach(
    std::make_shared<autoware::radar_tracks_noise_filter::RadarTrackCrossingNoiseFilterNode>(
      options));
  auto peer = attach(std::make_shared<rclcpp::Node>("radar_noise_peer"));
  RadarTracks::ConstSharedPtr noise, clean;
  auto noise_sub = peer->create_subscription<RadarTracks>(
    "/radar_tracks_noise_filter/output/noise_tracks", 1,
    [&](RadarTracks::ConstSharedPtr msg) { noise = msg; });
  auto clean_sub = peer->create_subscription<RadarTracks>(
    "/radar_tracks_noise_filter/output/filtered_tracks", 1,
    [&](RadarTracks::ConstSharedPtr msg) { clean = msg; });
  auto pub = peer->create_publisher<RadarTracks>(
    "/radar_tracks_noise_filter/input/tracks", rclcpp::SensorDataQoS());
  RadarTracks input;
  input.header.frame_id = "radar";
  input.header.stamp = peer->now();
  radar_msgs::msg::RadarTrack first, second;
  first.velocity.y = 2.0;
  second.velocity.y = 3.0;
  input.tracks = {first, second};
  ASSERT_TRUE(await([&] { return noise && clean; }, [&] { pub->publish(input); }));
  ASSERT_EQ(noise->tracks.size(), 1U);
  ASSERT_EQ(clean->tracks.size(), 1U);
  EXPECT_EQ(noise->tracks[0].velocity.y, 3.0);
  EXPECT_EQ(clean->tracks[0].velocity.y, 2.0);
  EXPECT_EQ(noise->header, input.header);
  EXPECT_EQ(clean->header, input.header);
  ASSERT_TRUE(filter->set_parameter(rclcpp::Parameter("velocity_y_threshold", 4.0)).successful);
  input.header.frame_id = "updated";
  ASSERT_TRUE(await(
    [&] { return noise->header.frame_id == "updated" && clean->header.frame_id == "updated"; },
    [&] { pub->publish(input); }));
  EXPECT_TRUE(noise->tracks.empty());
  EXPECT_EQ(clean->tracks.size(), 2U);
}
