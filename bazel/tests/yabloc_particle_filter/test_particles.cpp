// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <yabloc_particle_filter/camera_corrector/camera_particle_corrector.hpp>
#include <yabloc_particle_filter/common/prediction_util.hpp>
#include <yabloc_particle_filter/gnss_corrector/gnss_particle_corrector.hpp>
#include <yabloc_particle_filter/prediction/predictor.hpp>

#include <gtest/gtest.h>
#include <pcl_conversions/pcl_conversions.h>

#include <chrono>
#include <optional>
#include <thread>
namespace mpf = yabloc::modularized_particle_filter;
using Particles = yabloc_particle_filter::msg::ParticleArray;
using Pose = geometry_msgs::msg::PoseWithCovarianceStamped;
using Cloud = sensor_msgs::msg::PointCloud2;
class ParticleNodes : public ::testing::Test
{
protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
  rclcpp::executors::SingleThreadedExecutor executor;
  std::shared_ptr<rclcpp::Node> peer = std::make_shared<rclcpp::Node>("particle_peer");
  void SetUp() override
  {
    executor.add_node(peer);
    mpf::util::engine.seed(41);
  }
  template <class P>
  bool pump(P p, int ms = 3000)
  {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
      executor.spin_some();
      if (p()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    return p();
  }
  Particles particles()
  {
    Particles a;
    a.header.frame_id = "map";
    a.header.stamp = peer->now();
    a.id = 7;
    for (int i = 0; i < 5; ++i) {
      yabloc_particle_filter::msg::Particle p;
      p.pose.orientation.w = 1;
      p.pose.position.x = 10. + i * .5;
      p.pose.position.y = (i % 2) * .2;
      p.weight = .2;
      a.particles.push_back(p);
    }
    return a;
  }
  rclcpp::NodeOptions corrector(std::vector<rclcpp::Parameter> ps)
  {
    ps.emplace_back("acceptable_max_delay", 1.);
    ps.emplace_back("visualize", false);
    rclcpp::NodeOptions o;
    o.parameter_overrides(ps);
    return o;
  }
};
TEST_F(ParticleNodes, PredictionAndActivation)
{
  rclcpp::NodeOptions o;
  o.parameter_overrides(
    {rclcpp::Parameter("num_of_particles", 100),
     rclcpp::Parameter("resampling_interval_seconds", 1.),
     rclcpp::Parameter("static_linear_covariance", .01),
     rclcpp::Parameter("static_angular_covariance", .01),
     rclcpp::Parameter("cov_xx_yy", std::vector<double>{.01, .01}),
     rclcpp::Parameter("prediction_rate", 20.)});
  auto node = std::make_shared<mpf::Predictor>(o);
  executor.add_node(node);
  auto initial = peer->create_publisher<Pose>("/predictor/input/initialpose", 1);
  auto ekf = peer->create_publisher<Pose>("/predictor/input/ekf_pose", 10);
  auto twist = peer->create_publisher<geometry_msgs::msg::TwistWithCovarianceStamped>(
    "/predictor/input/twist_with_covariance", 10);
  auto height = peer->create_publisher<std_msgs::msg::Float32>("/predictor/input/height", 10);
  std::optional<Particles> output;
  auto sub = peer->create_subscription<Particles>(
    "/predictor/output/predicted_particles", 10, [&](Particles::ConstSharedPtr a) { output = *a; });
  auto client = peer->create_client<std_srvs::srv::SetBool>("/predictor/yabloc_trigger_srv");
  ASSERT_TRUE(
    pump([&] { return initial->get_subscription_count() > 0 && client->service_is_ready(); }));
  auto call = [&](bool enable) {
    auto req = std::make_shared<std_srvs::srv::SetBool::Request>();
    req->data = enable;
    auto f = client->async_send_request(req);
    bool ready =
      pump([&] { return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; });
    EXPECT_TRUE(ready);
    return ready && f.get()->success;
  };
  EXPECT_TRUE(call(false));
  EXPECT_FALSE(call(true));
  Pose p;
  p.header.stamp = peer->now();
  p.header.frame_id = "map";
  p.pose.pose.position.x = 10;
  p.pose.pose.orientation.w = 1;
  ASSERT_TRUE(pump([&] {
    ekf->publish(p);
    return ekf->get_subscription_count() > 0;
  }));
  // Drain the delivered pose before requesting activation.
  pump([] { return false; }, 100);
  EXPECT_TRUE(call(true));
  geometry_msgs::msg::TwistWithCovarianceStamped v;
  v.twist.twist.linear.x = 1.;
  std_msgs::msg::Float32 z;
  z.data = 2;
  ASSERT_TRUE(pump([&] {
    twist->publish(v);
    height->publish(z);
    return output.has_value();
  }));
  ASSERT_EQ(output->particles.size(), 100U);
  EXPECT_EQ(output->header.frame_id, "map");
  double x = 0;
  for (const auto & q : output->particles) {
    EXPECT_TRUE(std::isfinite(q.pose.position.x));
    EXPECT_NEAR(q.pose.position.z, 2., 1e-5);
    EXPECT_GT(q.weight, 0);
    x += q.pose.position.x;
  }
  EXPECT_NEAR(x / 100., 10., .3);
  const auto stamp = output->header.stamp;
  ASSERT_TRUE(pump([&] {
    twist->publish(v);
    return output && rclcpp::Time(output->header.stamp) >
                       rclcpp::Time(stamp) + rclcpp::Duration::from_seconds(.4);
  }));
  double moved = 0;
  for (const auto & q : output->particles) moved += q.pose.position.x;
  EXPECT_GT(moved / 100., x / 100. + .2);
  EXPECT_TRUE(call(false));
  pump([] { return false; }, 100);
  output.reset();
  EXPECT_FALSE(pump([&] { return output.has_value(); }, 150));
}
TEST_F(ParticleNodes, GnssCorrectionAndRejection)
{
  std::vector<rclcpp::Parameter> ps{rclcpp::Parameter("mahalanobis_distance_threshold", 30.)};
  for (const std::string group : {"for_fixed/", "for_not_fixed/"}) {
    ps.emplace_back(group + "flat_radius", .1);
    ps.emplace_back(group + "max_radius", 10.);
    ps.emplace_back(group + "min_weight", .1);
    ps.emplace_back(group + "max_weight", 1.);
  }
  auto node = std::make_shared<mpf::GnssParticleCorrector>(corrector(ps));
  executor.add_node(node);
  auto a =
    peer->create_publisher<Particles>("/gnss_particle_corrector/input/predicted_particles", 10);
  auto g = peer->create_publisher<Pose>("/gnss_particle_corrector/input/pose_with_covariance", 10);
  std::optional<Particles> output;
  auto s = peer->create_subscription<Particles>(
    "/gnss_particle_corrector/output/weighted_particles", 10,
    [&](Particles::ConstSharedPtr p) { output = *p; });
  auto input = particles();
  Pose pose;
  pose.header = input.header;
  pose.pose.pose.position.x = 10;
  pose.pose.pose.orientation.w = 1;
  ASSERT_TRUE(pump([&] {
    a->publish(input);
    g->publish(pose);
    return output.has_value();
  }));
  EXPECT_EQ(output->id, input.id);
  EXPECT_EQ(output->header.stamp, input.header.stamp);
  ASSERT_EQ(output->particles.size(), 5U);
  EXPECT_GT(output->particles.front().weight, output->particles.back().weight);
  // A moved population would pass the travel gate; the distant GNSS fix must
  // reject it.
  for (auto & p : input.particles) p.pose.position.x += 3;
  input.header.stamp = peer->now();
  pose.header = input.header;
  pose.pose.pose.position.x = 1000;
  output.reset();
  EXPECT_FALSE(pump(
    [&] {
      a->publish(input);
      g->publish(pose);
      return output.has_value();
    },
    250));
}
TEST_F(ParticleNodes, CameraMapCorrectionAndSwitch)
{
  auto node = std::make_shared<mpf::CameraParticleCorrector>(corrector(
    {rclcpp::Parameter("min_prob", .1), rclcpp::Parameter("far_weight_gain", .001),
     rclcpp::Parameter("enabled_at_first", true), rclcpp::Parameter("max_range", 20.),
     rclcpp::Parameter("image_size", 200), rclcpp::Parameter("gamma", 2.)}));
  executor.add_node(node);
  auto a =
    peer->create_publisher<Particles>("/camera_particle_corrector/input/predicted_particles", 10);
  auto map = peer->create_publisher<Cloud>("/camera_particle_corrector/input/ll2_road_marking", 10);
  auto lines =
    peer->create_publisher<Cloud>("/camera_particle_corrector/input/line_segments_cloud", 10);
  auto self = peer->create_publisher<geometry_msgs::msg::PoseStamped>(
    "/camera_particle_corrector/input/pose", 10);
  geometry_msgs::msg::PoseStamped current;
  current.pose.orientation.w = 1.;
  current.pose.position.x = 10.;
  ASSERT_TRUE(pump([&] { return self->get_subscription_count() > 0; }));
  self->publish(current);
  pump([] { return false; }, 100);
  auto input = particles();
  pcl::PointCloud<pcl::PointXYZLNormal> local;
  pcl::PointXYZLNormal p;
  p.getVector3fMap() = Eigen::Vector3f(0, 0, 0);
  p.getNormalVector3fMap() = Eigen::Vector3f(2, 0, 0);
  p.label = 255;
  local.push_back(p);
  Cloud observed;
  pcl::toROSMsg(local, observed);
  observed.header = input.header;
  pcl::PointCloud<pcl::PointNormal> road;
  pcl::PointNormal r;
  r.getVector3fMap() = Eigen::Vector3f(8, 0, 0);
  r.getNormalVector3fMap() = Eigen::Vector3f(18, 0, 0);
  road.push_back(r);
  Cloud world;
  pcl::toROSMsg(road, world);
  world.header = input.header;
  std::optional<Particles> output;
  auto s = peer->create_subscription<Particles>(
    "/camera_particle_corrector/output/weighted_particles", 10,
    [&](Particles::ConstSharedPtr p) { output = *p; });
  ASSERT_TRUE(pump([&] {
    map->publish(world);
    a->publish(input);
    lines->publish(observed);
    return output.has_value();
  }));
  EXPECT_EQ(output->id, input.id);
  EXPECT_EQ(output->header.stamp, input.header.stamp);
  ASSERT_EQ(output->particles.size(), input.particles.size());
  for (const auto & q : output->particles) {
    EXPECT_TRUE(std::isfinite(q.weight));
    EXPECT_GT(q.weight, .5);
    EXPECT_LE(q.weight, 1.);
  }
  auto client =
    peer->create_client<std_srvs::srv::SetBool>("/camera_particle_corrector/switch_srv");
  ASSERT_TRUE(pump([&] { return client->service_is_ready(); }));
  auto req = std::make_shared<std_srvs::srv::SetBool::Request>();
  req->data = false;
  auto f = client->async_send_request(req);
  ASSERT_TRUE(
    pump([&] { return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; }));
  EXPECT_TRUE(f.get()->success);
  for (auto & q : input.particles) q.pose.position.x += 3;
  input.header.stamp = peer->now();
  observed.header = input.header;
  output.reset();
  EXPECT_FALSE(pump(
    [&] {
      a->publish(input);
      lines->publish(observed);
      return output.has_value();
    },
    250));
}
