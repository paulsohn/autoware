// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <pluginlib/class_loader.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rviz_common/display.hpp>
#include <rviz_common/panel.hpp>
#include <rviz_common/tool.hpp>

#include <gtest/gtest.h>
#include <tinyxml2.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
template <class T>
void instantiate(pluginlib::ClassLoader<T> & loader, const std::string & name)
{
  const auto declared = loader.getDeclaredClasses();
  ASSERT_NE(std::find(declared.begin(), declared.end(), name), declared.end()) << name;
  EXPECT_NE(loader.createSharedInstance(name), nullptr) << name;
  QApplication::processEvents();
}

TEST(RvizPluginPackage, EveryExportedFactoryConstructsAndUnloads)
{
  const auto share = std::filesystem::path(
    ament_index_cpp::get_package_share_directory(std::getenv("RVIZ_PLUGIN_PACKAGE")));
  tinyxml2::XMLDocument document;
  ASSERT_EQ(
    document.LoadFile((share / std::getenv("RVIZ_PLUGIN_XML")).c_str()), tinyxml2::XML_SUCCESS);
  pluginlib::ClassLoader<rviz_common::Display> displays("rviz_common", "rviz_common::Display");
  pluginlib::ClassLoader<rviz_common::Panel> panels("rviz_common", "rviz_common::Panel");
  pluginlib::ClassLoader<rviz_common::Tool> tools("rviz_common", "rviz_common::Tool");
  size_t count = 0;
  for (auto * item = document.RootElement()->FirstChildElement("class"); item;
       item = item->NextSiblingElement("class")) {
    const std::string name =
      item->Attribute("name") ? item->Attribute("name") : item->Attribute("type");
    const std::string base = item->Attribute("base_class_type");
    SCOPED_TRACE(name);
    if (base == "rviz_common::Display") {
      ASSERT_NO_THROW(instantiate(displays, name));
    } else if (base == "rviz_common::Panel") {
      ASSERT_NO_THROW(instantiate(panels, name));
    } else if (base == "rviz_common::Tool") {
      ASSERT_NO_THROW(instantiate(tools, name));
    } else {
      FAIL() << "Unsupported exported factory base " << base;
    }
    ++count;
  }
  EXPECT_GT(count, 0U);
  std::cout << "Constructed and destroyed " << count << " factories from " << share << std::endl;
}
}  // namespace

int main(int argc, char ** argv)
{
  if (!std::getenv("DISPLAY")) {
    std::cerr << "Factory tests require an accessible X11 display (DISPLAY and XAUTHORITY)."
              << std::endl;
    return 1;
  }
  QApplication application(argc, argv);
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
