// Copyright 2023 Gustavo Rezende Silva
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

#include "suave_bt/action_change_alt.hpp"
#include <cmath>

namespace suave_bt
{
using namespace std::placeholders;
using namespace std::chrono_literals;

ChangeAlt::ChangeAlt(const std::string & name, const BT::NodeConfig & conf)
: BT::SyncActionNode(name, conf), battery_level_(-1.0), water_visibility_(-1.0),
  last_sent_altitude_(-1.0)  // set values to -1 to show they haven't been updated yet
{
  node_ = config().blackboard->get<std::shared_ptr<suave_bt::SuaveMission>>("node");
  previous_modes_ = config().blackboard->get<std::shared_ptr<std::map<std::string, std::string>>>(
    "previous_modes");

  // set ups program to call the callback function (diagnostics_cb)
  // when a change is detected in the diagnostics
  diagnostics_sub_ = node_->create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
    "/diagnostics", 10, std::bind(&ChangeAlt::diagnostics_cb, this, _1));
  // create the interface to use to set the altitude
  set_params_cli_ = node_->create_client<rcl_interfaces::srv::SetParameters>(
    "f_follow_pipeline_node/set_parameters");
}

void ChangeAlt::diagnostics_cb(const diagnostic_msgs::msg::DiagnosticArray & msg)
{
  // Callback function for if the diagnostics are updated
  for (const auto & status : msg.status) {   // read through each message
    if (status.message == "QA status") {     // if the message includes status
      // read through each value in the status update
      for (const auto & value : status.values) {
        // update our local copy of the battery level if that is what we are given
        if (value.key == "battery_level") {
          battery_level_ = std::stof(value.value);
          // update our local copy of the water visibility if that is what we are given
        } else if (value.key == "water_visibility") {
          water_visibility_ = std::stof(value.value);
        }
      }
    }
  }
}

BT::NodeStatus ChangeAlt::tick()
{
  // const auto & search_mode = previous_modes_->at("f_generate_search_path");
  const auto & follow_mode = previous_modes_->at("f_follow_pipeline");

  // if we don't have any information yet, send through a success and don't change the altitude
  if (battery_level_ == -1.0 || water_visibility_ == -1.0) {
    if (!(follow_mode == "fd_follow_pipeline")) {
      setOutput("search_mode", "fd_spiral_low");
    }
    return BT::NodeStatus::SUCCESS;
  }

  // calculate the new alt
  double new_alt;
  if (battery_level_ < 0.5) {
    new_alt = water_visibility_ - 0.15;
  } else {
    new_alt = water_visibility_ - 0.25;     // visibility/spiral alt difference that already existed
  }

  // cap the alt at 4m
  if (new_alt > 4.0) {
    new_alt = 4.0;
  }


  // determine execution based on the current mode
  if (follow_mode == "fd_follow_pipeline") {   // if in the follow pipeline mode
    // set altitude with SetParameters
    // deal with visibility too low
    if (new_alt < 1.0) {
      new_alt = 1.0;
    }

    // decide if a change is needed
    if (std::abs(new_alt - last_sent_altitude_) < 0.01) {
      return BT::NodeStatus::SUCCESS;
    }

    // make sure service is ready
    if (!set_params_cli_->service_is_ready()) {
      return BT::NodeStatus::FAILURE;
    }

    // build the param
    rcl_interfaces::msg::Parameter param;
    param.name = "altitude";
    param.value.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
    param.value.double_value = new_alt;

    // make request
    auto request = std::make_shared<rcl_interfaces::srv::SetParameters::Request>();
    request->parameters.push_back(param);

    // send request and wait 1s
    auto future = set_params_cli_->async_send_request(request);
    if (future.wait_for(1s) != std::future_status::ready) {
      return BT::NodeStatus::FAILURE;
    }

    // check if it failed
    auto response = future.get();
    if (response->results.empty() || !response->results[0].successful) {
      RCLCPP_WARN(
        node_->get_logger(), "Setting altitude failed: %s",
        response->results.empty() ? "no result" : response->results[0].reason.c_str());
      return BT::NodeStatus::FAILURE;
    }

    // if it gets here it was successful!
    last_sent_altitude_ = new_alt;
    RCLCPP_INFO(node_->get_logger(), "Follow altitude set to %.2f", new_alt);
    return BT::NodeStatus::SUCCESS;

  } else {    // if in the search mode
    // set altitude by picking a spiral mode
    std::string mode;

    if (new_alt >= 3.0) {
      mode = "fd_spiral_high";
    } else if (new_alt >= 2.0) {
      mode = "fd_spiral_medium";
    } else if (new_alt >= 1.0) {
      mode = "fd_spiral_low";
    } else {
      // visibility too low
      return BT::NodeStatus::FAILURE;
    }

    setOutput("search_mode", mode);
    return BT::NodeStatus::SUCCESS;
  }
}

}  // namespace suave_bt
