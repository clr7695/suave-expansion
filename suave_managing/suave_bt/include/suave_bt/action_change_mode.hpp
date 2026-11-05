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

#ifndef SUAVE_BT__ACTION_CHANGE_MODE_HPP_
#define SUAVE_BT__ACTION_CHANGE_MODE_HPP_

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "behaviortree_cpp/behavior_tree.h"
#include "behaviortree_cpp/bt_factory.h"
#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"
#include "rcl_interfaces/msg/parameter_type.hpp"
#include "rcl_interfaces/srv/get_parameters.hpp"
#include "rclcpp/rclcpp.hpp"
#include "suave_bt/suave_mission.hpp"
#include "system_modes_msgs/srv/change_mode.hpp"

namespace suave_bt
{

// Request a system_modes mode change and wait for the managed part node to
// actually reach it. While the transition is in progress the node returns
// RUNNING (not FAILURE), so an unfinished transition is not mistaken for a
// failed one. FAILURE is reserved for a missing/rejecting change_mode service
// or a transition that does not complete within `timeout` seconds.
//
// Set wait_for_transition="false" to get the old single-shot behavior (FAILURE
// if the part has not settled on the first check). Use it where a RUNNING
// result would halt a sibling action, e.g. switching spiral altitude in a
// ReactiveSequence in front of search_pipeline.
class ChangeMode : public BT::StatefulActionNode
{
public:
  ChangeMode(const std::string & name, const BT::NodeConfig & conf);

  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override;

  static BT::PortsList providedPorts()
  {
    return BT::PortsList(
      {
        BT::InputPort<std::string>("node_name"),
        BT::InputPort<std::string>("mode_name"),
        BT::InputPort<bool>(
          "wait_for_transition", true,
          "Return RUNNING until the part reaches the mode (false: FAILURE instead)"),
        BT::InputPort<double>(
          "timeout", 10.0,
          "Seconds to wait for the transition before returning FAILURE"),
      });
  }

protected:
  // Send the change_mode request to system_modes. Returns false if the service
  // is unavailable, does not answer, or rejects the request.
  bool send_change_mode_request();

  // Check whether the requested transition has completed and map that to a
  // node status, re-sending the request periodically while waiting.
  BT::NodeStatus check_transition();

  // Whether `mode_name` maps the managed part node to lifecycle active
  // (fd_unground / fd_all_thrusters / inactive map it to inactive).
  static bool mode_implies_active(const std::string & mode_name);

  // Return whether the managed part node has actually reached the state the
  // requested mode implies: the right lifecycle state (queried from the part
  // node's own get_state service, not system_modes inference) and, for the
  // f_generate_search_path spiral modes, the matching spiral_altitude
  // parameter value (queried from the part node's get_parameters service).
  // Both bypass system_modes mode inference, which can report a mode from a
  // stale parameter default even when the transition never happened.
  bool part_in_expected_state(const std::string & mode_name);

  suave_bt::SuaveMission::SharedPtr node_;
  std::string node_name_;
  std::shared_ptr<std::map<std::string, std::string>> previous_modes_;
  rclcpp::Client<system_modes_msgs::srv::ChangeMode>::SharedPtr change_mode_cli_;
  rclcpp::Client<lifecycle_msgs::srv::GetState>::SharedPtr get_state_cli_;
  rclcpp::Client<rcl_interfaces::srv::GetParameters>::SharedPtr get_params_cli_;

  // State of the transition currently being driven.
  std::string requested_mode_;
  bool wait_for_transition_{true};
  std::chrono::duration<double> timeout_{10.0};
  std::chrono::steady_clock::time_point start_time_;
  std::chrono::steady_clock::time_point last_request_time_;
};

}  // namespace suave_bt

#endif  // SUAVE_BT__ACTION_CHANGE_MODE_HPP_
