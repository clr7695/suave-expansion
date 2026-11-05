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

#include <cmath>

#include "suave_bt/action_change_mode.hpp"

namespace suave_bt
{
using namespace std::placeholders;
using namespace std::chrono_literals;

ChangeMode::ChangeMode(const std::string & name, const BT::NodeConfig & conf)
: BT::StatefulActionNode(name, conf)
{
  getInput("node_name", node_name_);

  node_ = config().blackboard->get<std::shared_ptr<suave_bt::SuaveMission>>("node");
  previous_modes_ =
    config().blackboard->get<std::shared_ptr<std::map<std::string, std::string>>>("previous_modes");

  change_mode_cli_ =
    node_->create_client<system_modes_msgs::srv::ChangeMode>(node_name_ + "/change_mode");
  get_state_cli_ =
    node_->create_client<lifecycle_msgs::srv::GetState>(node_name_ + "_node/get_state");
  get_params_cli_ =
    node_->create_client<rcl_interfaces::srv::GetParameters>(
    node_name_ + "_node/get_parameters");
}

bool ChangeMode::mode_implies_active(const std::string & mode_name)
{
  // See suave_modes.yaml: fd_unground and inactive map every system's part to
  // inactive, fd_all_thrusters maps f_maintain_motion's part to inactive.
  return mode_name != "fd_unground" &&
         mode_name != "fd_all_thrusters" &&
         mode_name != "inactive";
}

bool ChangeMode::part_in_expected_state(const std::string & mode_name)
{
  const bool want_active = mode_implies_active(mode_name);

  if (!get_state_cli_->service_is_ready()) {
    return false;
  }
  auto state_future = get_state_cli_->async_send_request(
    std::make_shared<lifecycle_msgs::srv::GetState::Request>());
  if (state_future.wait_for(1s) != std::future_status::ready) {
    return false;
  }
  auto state_id = state_future.get()->current_state.id;
  const bool state_ok = want_active ?
    state_id == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE :
    state_id == lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE;
  if (!state_ok) {
    return false;
  }

  // Sub-mode check: only f_generate_search_path selects a parameter
  // (spiral_altitude) per mode. For every other system/mode the lifecycle
  // state is the whole story.
  double expected_altitude = 0.0;
  if (mode_name == "fd_spiral_high") {
    expected_altitude = 3.0;
  } else if (mode_name == "fd_spiral_medium") {
    expected_altitude = 2.0;
  } else if (mode_name == "fd_spiral_low") {
    expected_altitude = 1.0;
  } else {
    return true;
  }

  if (!get_params_cli_->service_is_ready()) {
    return false;
  }
  auto params_request = std::make_shared<rcl_interfaces::srv::GetParameters::Request>();
  params_request->names.push_back("spiral_altitude");
  auto params_future = get_params_cli_->async_send_request(params_request);
  if (params_future.wait_for(1s) != std::future_status::ready) {
    return false;
  }
  auto params_response = params_future.get();
  const auto & values = params_response->values;
  if (values.empty() ||
    values[0].type != rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE)
  {
    return false;
  }
  return std::abs(values[0].double_value - expected_altitude) < 0.01;
}

bool ChangeMode::send_change_mode_request()
{
  last_request_time_ = std::chrono::steady_clock::now();

  if (!change_mode_cli_->service_is_ready()) {
    RCLCPP_INFO(
      node_->get_logger(), "%s/change_mode service not available...", node_name_.c_str());
    return false;
  }

  auto request = std::make_shared<system_modes_msgs::srv::ChangeMode::Request>();
  request->mode_name = requested_mode_;
  auto change_mode_future = change_mode_cli_->async_send_request(request);
  return change_mode_future.wait_for(1s) == std::future_status::ready &&
         change_mode_future.get()->success;
}

BT::NodeStatus ChangeMode::check_transition()
{
  // system_modes acknowledges the mode change before the underlying lifecycle
  // transition and parameter update are applied (and can drop them entirely).
  // Only report success once the part node has actually reached the required
  // lifecycle state and spiral_altitude.
  if (part_in_expected_state(requested_mode_)) {
    RCLCPP_INFO(
      node_->get_logger(), "Node %s mode changed to %s", node_name_.c_str(),
      requested_mode_.c_str());
    previous_modes_->at(node_name_) = requested_mode_;
    return BT::NodeStatus::SUCCESS;
  }

  if (!wait_for_transition_) {
    RCLCPP_INFO(
      node_->get_logger(),
      "%s: change_mode(%s) acknowledged but %s_node has not reached it yet",
      node_name_.c_str(), requested_mode_.c_str(), node_name_.c_str());
    return BT::NodeStatus::FAILURE;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now - start_time_ >= timeout_) {
    RCLCPP_WARN(
      node_->get_logger(),
      "%s: %s_node did not reach mode %s within %.1f s",
      node_name_.c_str(), node_name_.c_str(), requested_mode_.c_str(), timeout_.count());
    return BT::NodeStatus::FAILURE;
  }

  // system_modes can drop a request; re-send it periodically while waiting.
  if (now - last_request_time_ >= 2s) {
    send_change_mode_request();
  }

  return BT::NodeStatus::RUNNING;
}

BT::NodeStatus ChangeMode::onStart()
{
  getInput("mode_name", requested_mode_);
  wait_for_transition_ = true;
  getInput("wait_for_transition", wait_for_transition_);
  double timeout_s = 10.0;
  getInput("timeout", timeout_s);
  timeout_ = std::chrono::duration<double>(timeout_s);

  // Already driven to this mode and the part node is where the mode requires
  // it to be: nothing to do.
  if (previous_modes_->at(node_name_) == requested_mode_ &&
    part_in_expected_state(requested_mode_))
  {
    return BT::NodeStatus::SUCCESS;
  }

  start_time_ = std::chrono::steady_clock::now();
  if (!send_change_mode_request()) {
    return BT::NodeStatus::FAILURE;
  }

  return check_transition();
}

BT::NodeStatus ChangeMode::onRunning()
{
  // The target can change while waiting (e.g. mode_name="{search_mode}"):
  // start over and drive the new mode instead.
  std::string mode_name;
  getInput("mode_name", mode_name);
  if (mode_name != requested_mode_) {
    return onStart();
  }

  return check_transition();
}

void ChangeMode::onHalted()
{
  RCLCPP_INFO(
    node_->get_logger(), "%s: change_mode(%s) halted before the transition completed",
    node_name_.c_str(), requested_mode_.c_str());
}
}  // namespace suave_bt
