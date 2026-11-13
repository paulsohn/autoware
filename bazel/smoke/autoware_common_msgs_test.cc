// Copyright 2026 Taeseung Sohn
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

#include "autoware_common_msgs/msg/response_status.hpp"

#include <cstdlib>
#include <string>

int main()
{
  autoware_common_msgs::msg::ResponseStatus status;
  status.success = false;
  status.code = autoware_common_msgs::msg::ResponseStatus::SERVICE_TIMEOUT;
  status.message = "service timed out";

  const bool valid = !status.success &&
                     status.code == autoware_common_msgs::msg::ResponseStatus::SERVICE_TIMEOUT &&
                     status.message == "service timed out";
  return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
