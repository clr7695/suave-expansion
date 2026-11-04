# Copyright 2026 KAS Lab
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Publish time-varying simulated ocean-current measurements."""

import math
import sys

from diagnostic_msgs.msg import DiagnosticArray
from diagnostic_msgs.msg import DiagnosticStatus
from diagnostic_msgs.msg import KeyValue
from mavros_msgs.msg import State

import rclpy

from rclpy.node import Node


class OceanCurrentMonitor(Node):
    """Generate sinusoidal strength and direction diagnostics."""

    def __init__(self):
        """Initialize ocean-current parameters, diagnostics, and state."""
        super().__init__('ocean_current_monitor')

        self.declare_parameter('qa_publishing_period', 1.0)
        self.declare_parameter('current_strength_period', 150)
        self.declare_parameter('current_strength_min', 0.0)
        self.declare_parameter('current_strength_max', 1.0)
        self.declare_parameter('current_strength_sec_shift', 0.0)
        self.declare_parameter('current_direction_period', 150)
        self.declare_parameter('current_direction_min', 0.0)
        self.declare_parameter('current_direction_max', 360.0)
        self.declare_parameter('current_direction_sec_shift', 0.0)

        self.qa_publishing_period = self.get_parameter(
            'qa_publishing_period').value

        self.diagnostics_publisher = self.create_publisher(
            DiagnosticArray, '/diagnostics', 10)
        
        self.mavros_state_sub = self.create_subscription(
            State, 'mavros/state', self.status_cb, 10)

        self.initial_time = None
        self.qa_publisher_timer = self.create_timer(
            self.qa_publishing_period, self.qa_publisher_cb)
        self.qa_publisher_cb()

    def status_cb(self, msg):
        """Start advancing current measurements at the first guided-mode observation."""
        if msg.mode == 'GUIDED' and self.initial_time is None:
            self.initial_time = self.get_clock().now().to_msg().sec
            self.destroy_subscription(self.mavros_state_sub)

# Helper function to make the publisher method a bit cleaner. 
    def calculate_sinusoidal_value(self, prefix, current_time):
        """Calculate a bounded sinusoidal parameter value."""
        period = self.get_parameter(prefix + '_period').value
        minimum = self.get_parameter(prefix + '_min').value
        maximum = self.get_parameter(prefix + '_max').value
        sec_shift = self.get_parameter(prefix + '_sec_shift').value

        amplitude = abs(maximum - minimum) / 2
        offset = amplitude + minimum
        elapsed = (current_time - self.initial_time if self.initial_time is not None else 0)
        value = amplitude * math.cos((2 * math.pi / period) * (elapsed + sec_shift)) + offset
        return value

    def qa_publisher_cb(self):
        """Calculate and publish the current ocean-current measurements."""
        current_time = self.get_clock().now().to_msg().sec
        strength = self.calculate_sinusoidal_value('current_strength', current_time)
        direction = self.calculate_sinusoidal_value('current_direction', current_time) % 360.0

        strength_value = KeyValue()
        strength_value.key = 'current_strength'
        strength_value.value = str(strength)

        strength_alias = KeyValue()
        strength_alias.key = 'ocean_current_strength'
        strength_alias.value = str(strength)

        direction_value = KeyValue()
        direction_value.key = 'current_direction'
        direction_value.value = str(direction)

        direction_alias = KeyValue()
        direction_alias.key = 'ocean_current_direction'
        direction_alias.value = str(direction)

        status_msg = DiagnosticStatus()
        status_msg.level = DiagnosticStatus.OK
        status_msg.name = 'ocean_current_monitor: Ocean current measurement'
        status_msg.message = 'QA status'
        status_msg.values.append(strength_value)
        status_msg.values.append(strength_alias)
        status_msg.values.append(direction_value)
        status_msg.values.append(direction_alias)

        diag_msg = DiagnosticArray()
        diag_msg.header.stamp = self.get_clock().now().to_msg()
        diag_msg.status.append(status_msg)

        self.diagnostics_publisher.publish(diag_msg)


def main():
    """Run the ocean-current monitor node."""
    print('Starting ocean current monitor node')

    rclpy.init(args=sys.argv)

    ocean_current_monitor = OceanCurrentMonitor()
    rclpy.spin(ocean_current_monitor)

    rclpy.shutdown()
