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

import math
from unittest.mock import Mock

from builtin_interfaces.msg import Time
from mavros_msgs.msg import State
import pytest
import rclpy
from rclpy.node import Node
from rclpy.parameter import Parameter

from suave_monitor.ocean_current_monitor import OceanCurrentMonitor


@pytest.fixture
def monitor(monkeypatch):
    rclpy.init()
    publisher = Mock()
    create_publisher = Node.create_publisher

    def make_publisher(node, msg_type, topic, *args, **kwargs):
        if topic == '/diagnostics':
            return publisher
        return create_publisher(node, msg_type, topic, *args, **kwargs)

    monkeypatch.setattr(Node, 'create_publisher', make_publisher)
    node = OceanCurrentMonitor()
    clock = Mock()
    clock.now.return_value.to_msg.return_value = Time(sec=100)
    monkeypatch.setattr(node, 'get_clock', lambda: clock)
    try:
        yield node, publisher, clock
    finally:
        node.destroy_node()
        rclpy.shutdown()


def current_values(publisher):
    values = {
        value.key: float(value.value)
        for value in publisher.publish.call_args.args[0].status[0].values
    }
    return values['current_strength'], values['current_direction']


def set_time(clock, seconds):
    clock.now.return_value.to_msg.return_value = Time(sec=seconds)


def test_publishes_strength_and_direction_on_construction(monitor):
    node, publisher, _ = monitor
    publisher.publish.assert_called_once()
    strength, direction = current_values(publisher)
    assert strength == pytest.approx(1.0)
    assert direction == pytest.approx(0.0)
    assert not node.qa_publisher_timer.is_canceled()


def test_current_schedule_starts_at_guided(monitor):
    node, publisher, clock = monitor
    node.set_parameters([
        Parameter('current_strength_period', value=100),
        Parameter('current_strength_min', value=0.0),
        Parameter('current_strength_max', value=2.0),
        Parameter('current_direction_period', value=120),
        Parameter('current_direction_min', value=0.0),
        Parameter('current_direction_max', value=180.0),
    ])

    node.status_cb(State(mode='GUIDED'))
    node.qa_publisher_cb()
    assert current_values(publisher) == pytest.approx((2.0, 180.0))

    set_time(clock, 125)
    node.qa_publisher_cb()
    strength, direction = current_values(publisher)
    expected_strength = math.cos(2 * math.pi * 25 / 100)
    expected_direction = 90 + 90 * math.cos(
        2 * math.pi * 25 / 120)
    assert strength == pytest.approx(expected_strength + 1)
    assert direction == pytest.approx(expected_direction)
