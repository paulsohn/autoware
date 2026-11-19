// Copyright 2026 TIER IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// What each backend does with a delivered message is covered by that backend's own tests, so what
// is left here is the wrapper's own: which of the two callback shapes a registration resolves to,
// which backend a Subscriber holds, and how it forwards subscription QoS.
//
// The disabled build uses the upstream Synchronizer and still forwards subscription QoS.

#include "autoware/agnocast_wrapper/message_filters.hpp"

#include "autoware/agnocast_wrapper/runtime.hpp"
#include "heaphook_probe.hpp"

#include <geometry_msgs/msg/point_stamped.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>

#include <gtest/gtest.h>

#ifdef USE_AGNOCAST_ENABLED

#include <type_traits>
#include <variant>

namespace
{

namespace mf = autoware::agnocast_wrapper::message_filters;
using geometry_msgs::msg::PointStamped;
using geometry_msgs::msg::PoseStamped;
using Policy = mf::sync_policies::ExactTime<PoseStamped, PointStamped>;
using MessagePtrPose = AUTOWARE_MESSAGE_CONST_SHARED_PTR(PoseStamped);
using MessagePtrPoint = AUTOWARE_MESSAGE_CONST_SHARED_PTR(PointStamped);

/// Receiver for the member-function-pointer registerCallback() overloads, which take the callback
/// and the instance separately.
class Receiver
{
public:
  void on_message_ptr(const MessagePtrPose &, const MessagePtrPoint &) {}
  void on_const_shared_ptr(
    const PoseStamped::ConstSharedPtr &, const PointStamped::ConstSharedPtr &)
  {
  }
};

/// Invocable with either shape, the case the precedence rule is about. Resolving it to the
/// ConstSharedPtr form instantiates that body, and the build stops with the message below.
class BothShapesCallable
{
public:
  void operator()(const MessagePtrPose &, const MessagePtrPoint &) const {}

  template <typename Pose = PoseStamped>
  void operator()(const typename Pose::ConstSharedPtr &, const PointStamped::ConstSharedPtr &) const
  {
    static_assert(
      !std::is_same_v<Pose, PoseStamped>,
      "a callable taking both shapes resolved to the ConstSharedPtr form; the "
      "AUTOWARE_MESSAGE_CONST_SHARED_PTR form must be probed first");
  }
};

/// The operator() qualifiers registerCallback() has to accept, since CallbackAdapter stores the
/// callable and invokes it as a non-const lvalue.
struct NonConstCallable
{
  void operator()(const MessagePtrPose &, const MessagePtrPoint &) {}
};

struct LvalueRefQualifiedCallable
{
  void operator()(const MessagePtrPose &, const MessagePtrPoint &) & {}
};

/// Every registerCallback() overload in both shapes, plus the callable qualifiers above. Never
/// called: what it pins is that the bodies compile, which is where the shape is decided. It also
/// covers the agnocast half of the adapter, because the std::visit in registerCallbackInternal()
/// instantiates agnocastInvoke() whichever backend ends up running.
[[maybe_unused]] void instantiate_every_registration_shape(
  mf::Synchronizer<Policy> & sync, Receiver & receiver)
{
  auto message_ptr_callable = [](const MessagePtrPose &, const MessagePtrPoint &) {};
  const auto const_shared_ptr_callable =
    [](const PoseStamped::ConstSharedPtr &, const PointStamped::ConstSharedPtr &) {};
  auto counting_callable = [count = 0](
                             const PoseStamped::ConstSharedPtr &,
                             const PointStamped::ConstSharedPtr &) mutable { ++count; };
  auto message_ptr_method = &Receiver::on_message_ptr;
  NonConstCallable non_const_callable;
  LvalueRefQualifiedCallable lvalue_ref_qualified_callable;
  BothShapesCallable both_shapes_callable;

  sync.registerCallback(message_ptr_callable);           // C &
  sync.registerCallback(const_shared_ptr_callable);      // const C &
  sync.registerCallback(counting_callable);              // C &, mutable lambda
  sync.registerCallback(non_const_callable);             // C &, non-const operator()
  sync.registerCallback(lvalue_ref_qualified_callable);  // C &, operator() &
  sync.registerCallback(both_shapes_callable);           // C &, both shapes
  sync.registerCallback(
    [](const PoseStamped::ConstSharedPtr &, const PointStamped::ConstSharedPtr &) {});  // const C &
  sync.registerCallback(message_ptr_method, &receiver);                                 // C &, T *
  sync.registerCallback(&Receiver::on_const_shared_ptr, &receiver);  // const C &, T *
}

/// The backend is picked from use_agnocast() at construction and fixed for the Subscriber's
/// lifetime. Needs no node and no message flow, so it runs the same way in every configuration.
TEST(SynchronizerTest, SubscriberHoldsTheBackendSelectedAtConstruction)
{
  // Arrange
  mf::Subscriber<PoseStamped> subscriber;

  // Assert
  if (autoware::agnocast_wrapper::use_agnocast()) {
    EXPECT_NO_THROW(subscriber.agnocast_subscriber());
    EXPECT_THROW(subscriber.rclcpp_subscriber(), std::bad_variant_access);
  } else {
    EXPECT_NO_THROW(subscriber.rclcpp_subscriber());
    EXPECT_THROW(subscriber.agnocast_subscriber(), std::bad_variant_access);
  }
}

#else

namespace
{

namespace mf = autoware::agnocast_wrapper::message_filters;
using geometry_msgs::msg::PoseStamped;

#endif

TEST(SynchronizerTest, SubscriberForwardsRmwQosOnConstructionAndResubscription)
{
#ifdef USE_AGNOCAST_ENABLED
  ASSERT_TRUE(
    !autoware::agnocast_wrapper::use_agnocast() ||
    autoware::agnocast_wrapper::test::agnocast_heaphook_loaded());
#endif
  auto node = std::make_shared<autoware::agnocast_wrapper::Node>("filter_qos_test");
  auto qos = rmw_qos_profile_sensor_data;
  qos.depth = 7;
  mf::Subscriber<PoseStamped> subscriber(node.get(), "/filter_qos_first", qos);

  auto check = [&](const std::string & topic) {
    auto inspect = [&](auto & backend) {
      auto subscription = backend.getSubscriber();
      ASSERT_NE(subscription, nullptr);
      EXPECT_EQ(backend.getTopic(), topic);
      const auto actual = subscription->get_actual_qos().get_rmw_qos_profile();
      EXPECT_EQ(actual.history, qos.history);
      EXPECT_EQ(actual.depth, qos.depth);
      EXPECT_EQ(actual.reliability, qos.reliability);
      EXPECT_EQ(actual.durability, qos.durability);
    };
#ifdef USE_AGNOCAST_ENABLED
    if (autoware::agnocast_wrapper::use_agnocast()) {
      inspect(subscriber.agnocast_subscriber());
    } else {
      inspect(subscriber.rclcpp_subscriber());
    }
#else
    inspect(subscriber);
#endif
  };
  check("/filter_qos_first");
  qos.depth = 3;
  subscriber.subscribe(node.get(), "/filter_qos_second", qos);
  check("/filter_qos_second");
  subscriber.unsubscribe();
#ifdef USE_AGNOCAST_ENABLED
  if (autoware::agnocast_wrapper::use_agnocast()) {
    EXPECT_EQ(subscriber.agnocast_subscriber().getSubscriber(), nullptr);
  } else {
    EXPECT_EQ(subscriber.rclcpp_subscriber().getSubscriber(), nullptr);
  }
#else
  EXPECT_EQ(subscriber.getSubscriber(), nullptr);
#endif
}

}  // namespace
