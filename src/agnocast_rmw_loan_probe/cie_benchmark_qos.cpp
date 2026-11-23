// The standalone RMW supports KeepLast. Limit this diagnostic override to CIE
// thread announcements. Path retains the same QoS as the ordinary reader.
#include <rcl/publisher.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>

extern "C" rcl_ret_t rcl_publisher_init(
  rcl_publisher_t * pub, const rcl_node_t * node, const rosidl_message_type_support_t * ts,
  const char * topic, const rcl_publisher_options_t * options)
{
  static const auto next =
    reinterpret_cast<decltype(&rcl_publisher_init)>(dlsym(RTLD_NEXT, "rcl_publisher_init"));
  if (!next) return RCL_RET_ERROR;
  if (topic && options && options->qos.history == RMW_QOS_POLICY_HISTORY_KEEP_ALL &&
      (std::strcmp(topic, "/cie_thread_configurator/callback_group_info") == 0 ||
       std::strcmp(topic, "/agnocast_cie_thread_configurator/callback_group_info") == 0)) {
    auto bounded = *options;
    bounded.qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
    bounded.qos.depth = 16;
    std::fprintf(stderr, "CIE benchmark announcement KeepLast(16): %s\n", topic);
    return next(pub, node, ts, topic, &bounded);
  }
  return next(pub, node, ts, topic, options);
}
