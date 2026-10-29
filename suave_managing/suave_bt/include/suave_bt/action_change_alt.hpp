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


#ifndef SUAVE_BT__ACTION_CHANGE_ALT_HPP_
#define SUAVE_BT__ACTION_CHANGE_ALT_HPP_

#include <map>
#include <memory>
#include <string>

#include "behaviortree_cpp/behavior_tree.h"
#include "behaviortree_cpp/bt_factory.h"
#include "rclcpp/rclcpp.hpp"
#include "suave_bt/suave_mission.hpp"

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rcl_interfaces/msg/parameter.hpp"
#include "rcl_interfaces/msg/parameter_type.hpp"
#include "rcl_interfaces/srv/set_parameters.hpp"


namespace suave_bt
{

class ChangeAlt : public BT::SyncActionNode
{
public:
  ChangeAlt(const std::string & name, const BT::NodeConfig & conf);

  BT::NodeStatus tick() override;

  static BT::PortsList providedPorts()
  {
    return BT::PortsList(
      {
        BT::InputPort<std::string>("phase"),
        BT::OutputPort<std::string>("search_mode"),
      });
  }

private:
  std::shared_ptr<suave_bt::SuaveMission> node_;
  std::shared_ptr<std::map<std::string, std::string>> previous_modes_;

  float battery_level_;
  float water_visibility_;
  rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_sub_;
  rclcpp::Client<rcl_interfaces::srv::SetParameters>::SharedPtr set_params_cli_;
  double last_sent_altitude_;
  void diagnostics_cb(const diagnostic_msgs::msg::DiagnosticArray & msg);
};

}  // namespace suave_bt

#endif  // SUAVE_BT__ACTION_CHANGE_ALT_HPP_
