// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include <autoware/qos_utils/qos_compatibility.hpp>

#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/trigger.hpp>

#include <gtest/gtest.h>

#include <memory>

class QosCompatibility : public ::testing::Test
{
protected:
  void SetUp() override { rclcpp::init(0, nullptr); }
  void TearDown() override { rclcpp::shutdown(); }
};

TEST_F(QosCompatibility, DefaultsWorkWithRclcppEntities)
{
  auto node = std::make_shared<rclcpp::Node>("qos_compatibility_test");
  auto publisher =
    node->create_publisher<std_msgs::msg::String>("topic", AUTOWARE_DEFAULT_TOPIC_QOS_PROFILE());
  using Service = std_srvs::srv::Trigger;
  auto service = node->create_service<Service>(
    "service",
    [](const std::shared_ptr<Service::Request>, std::shared_ptr<Service::Response> response) {
      response->success = true;
    },
    AUTOWARE_DEFAULT_SERVICES_QOS_PROFILE());

  EXPECT_EQ(publisher->get_actual_qos().depth(), 10u);
  EXPECT_STREQ(service->get_service_name(), "/service");
}
