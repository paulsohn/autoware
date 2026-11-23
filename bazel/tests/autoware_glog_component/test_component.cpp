// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <ament_index_cpp/get_resource.hpp>
#include <autoware/glog_component/glog_component.hpp>

#include <gtest/gtest.h>

#include <csignal>
#include <string>
#include <vector>

namespace
{
class Capture : public google::LogSink
{
public:
  Capture() { google::AddLogSink(this); }
  ~Capture() override { google::RemoveLogSink(this); }
  void send(
    google::LogSeverity, const char *, const char *, int, const google::LogMessageTime &,
    const char * message, size_t length) override
  {
    messages.emplace_back(message, length);
  }
  std::vector<std::string> messages;
};

class GlogComponentTest : public testing::Test
{
protected:
  void SetUp() override { rclcpp::init(0, nullptr); }
  void TearDown() override
  {
    if (google::IsGoogleLoggingInitialized()) google::ShutdownGoogleLogging();
    FLAGS_minloglevel = 0;
    rclcpp::shutdown();
  }
};

TEST_F(GlogComponentTest, InitializesOnceAndSurvivesComponentDestruction)
{
  std::string record;
  ASSERT_TRUE(
    ament_index_cpp::get_resource("rclcpp_components", "autoware_glog_component", record));
  EXPECT_NE(record.find("autoware::glog_component::GlogComponent"), std::string::npos);
  ASSERT_FALSE(google::IsGoogleLoggingInitialized());
  Capture capture;
  {
    auto first = std::make_shared<autoware::glog_component::GlogComponent>(rclcpp::NodeOptions());
    EXPECT_TRUE(google::IsGoogleLoggingInitialized());
    struct sigaction action
    {
    };
    ASSERT_EQ(sigaction(SIGABRT, nullptr, &action), 0);
    EXPECT_NE(action.sa_handler, SIG_DFL);
    rclcpp::NodeOptions second_options;
    second_options.arguments({"--ros-args", "-r", "__node:=another_logger"});
    auto second = std::make_shared<autoware::glog_component::GlogComponent>(second_options);
    LOG(INFO) << "both components active";
  }
  EXPECT_TRUE(google::IsGoogleLoggingInitialized());
  LOG(INFO) << "after component destruction";
  EXPECT_EQ(
    capture.messages,
    (std::vector<std::string>{"both components active", "after component destruction"}));
}

TEST_F(GlogComponentTest, ReusesExternallyConfiguredLogger)
{
  google::InitGoogleLogging("external_logger");
  FLAGS_minloglevel = google::GLOG_WARNING;
  Capture capture;
  auto component = std::make_shared<autoware::glog_component::GlogComponent>(rclcpp::NodeOptions());
  EXPECT_EQ(FLAGS_minloglevel, google::GLOG_WARNING);
  LOG(INFO) << "suppressed";
  LOG(WARNING) << "existing configuration";
  EXPECT_EQ(capture.messages, (std::vector<std::string>{"existing configuration"}));
}
}  // namespace
