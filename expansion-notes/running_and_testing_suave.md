# Running and testing SUAVE (extended BT) on Windows

How to run SUAVE with the extended behavior-tree manager (`suave_bt_extended`) in Docker on this Windows machine. It covers:

- running it with the GUI desktop so you can watch it;
- running it for numbers and printed output only;
- checking what the behavior tree is doing;
- adding code to `suave_bt` and testing it.

Everything here was checked on this machine (Docker Desktop + WSL2, container `suave`, image `ghcr.io/kas-lab/suave:main`) unless a step says otherwise.

---

## 1. What runs when "the program" runs

A SUAVE run is five groups of processes. All of them run inside the Docker container:

| Piece | Started by | What it does |
|---|---|---|
| **ArduSub SITL** (+ MAVProxy) | `sim_vehicle.py` | Simulated autopilot of the underwater vehicle. |
| **Gazebo simulation** + MAVROS + ROS–Gazebo bridges | `ros2 launch suave simulation.launch.py` | Physics, world, pipeline. MAVROS connects ROS to ArduSub. |
| **Managed system** (spiral search, follow pipeline, recharge, recover thrusters, monitors, system_modes) | `ros2 launch suave_bringup mission.launch.py ...` | The vehicle's mission functions and the monitors that publish `/diagnostics`. |
| **Managing system**: `suave_bt_extended` | same launch, `adaptation_manager:=bt bt_executable:=suave_bt_extended` | The behavior tree in [suave_extended.xml](../suave_managing/suave_bt/bts/suave_extended.xml). It switches function modes based on thrusters, battery and water visibility. |
| **Metrics** (`mission_metrics`) | same launch | Writes the results CSVs when the mission ends. |

You start them **in that order** and wait for each to settle before starting the next.

Diagrams of the code are in this folder: [suave_bt_class_diagram.png](suave_bt_class_diagram.png) (classes) and [suave_bt_extended_tree.png](suave_bt_extended_tree.png) (the tree).

---

## 2. The container (already set up)

| Item | Value |
|---|---|
| Container name | `suave` |
| Image | `ghcr.io/kas-lab/suave:main` (Kasm desktop image, user `kasm-user`) |
| Your repo, mounted at | `/home/kasm-user/suave_ws/src/suave` (live: edits on Windows appear immediately) |
| Results, mounted at | `/home/kasm-user/suave/results` = **`C:\Users\catie\Repos\suave-expansion\results`** on Windows (the [results/](../results/) folder in this repo) |
| Desktop | https://localhost:6901 (user `kasm_user`, password `password`) |
| Shared memory | 512 MB |

Two things were also changed in the repo checkout:
- `git config core.autocrlf input`: files are checked out with Linux (LF) line endings, which Linux scripts need. Keep it that way.
- `.config/host.ini` (gitignored) points the `suave` CLI at this container's paths (`/home/kasm-user/...`), sets `gpu = none`, and sets `host_results_dir` to the repo's `results/` folder.
- The repo's `results/` folder is **not ignored by git**, so new result files show up in `git status` as untracked. Commit the ones you want to keep. If you'd rather never commit them, add `results/` to `.gitignore`.

### Everyday container commands (PowerShell, from the repo folder)

```powershell
docker start suave                 # start it (e.g. after a reboot)
docker stop suave                  # stop it
docker restart suave               # quickest way to kill every SUAVE process and start clean
docker exec -it suave bash -l      # open a shell inside it   (-l matters, see below)
docker ps -a --filter name=suave   # is it running?
```

**`mavproxy.py` must be on the PATH.** It lives in `~/.local/bin`, which this image doesn't put on the PATH. Without it, ArduSub fails with `No such file or directory: 'mavproxy.py'`. Your branch's `docker/dockerfile-suave` already fixes this, but the downloaded `suave:main` image predates that fix. In this container, the fix is a line appended to `~/.bashrc`, so every new terminal (desktop or `docker exec ... bash`) finds it:

```bash
export PATH="$PATH:$HOME/.local/bin"
```

That line lives inside the container. **If you recreate the container, add it again:**

```powershell
docker exec suave bash -c 'echo ''export PATH="$PATH:$HOME/.local/bin"'' >> ~/.bashrc'
```

Already-open terminals don't pick it up: open a new one, or run `source ~/.bashrc`. Using `bash -l` for `docker exec` shells also works, because login shells add `~/.local/bin` themselves.

### If you ever need to recreate it

This deletes anything stored only inside the container. The repo and results folders are on Windows, so they're safe.

```powershell
docker rm -f suave
docker run -d --name suave --shm-size=512m -p 6901:6901 -e VNC_PW=password --security-opt seccomp=unconfined `
  -v "C:\Users\catie\Repos\suave-expansion:/home/kasm-user/suave_ws/src/suave" `
  -v "C:\Users\catie\Repos\suave-expansion\results:/home/kasm-user/suave/results" `
  ghcr.io/kas-lab/suave:main
```

Add `-p 1667:1667 -p 1668:1668` if you want to watch the tree live in Groot2 (section 5.4).

---

## 3. Run it and watch it in the GUI

### 3.1 Open the desktop

1. `docker start suave` (if it isn't running).
2. Open **https://localhost:6901** in a browser and accept the self-signed certificate warning.
3. Log in: user `kasm_user`, password `password`.

### 3.2 Start the three pieces

Open **three terminals on the desktop** (Applications menu → Terminal Emulator, or right-click the desktop). Run one command in each, in order:

**Terminal 1: ArduSub** (wait until MAVProxy prints `Detected vehicle` / parameters received, ~15 s)
```bash
cd /tmp && sim_vehicle.py -L RATBeach -v ArduSub --model=JSON --console --no-wsl2-network
```

**Terminal 2: Gazebo simulation** (wait until the Gazebo window shows the vehicle and pipeline, ~30 s)
```bash
cd ~/suave_ws/src/suave/runner && ./scripts/launch_sim.sh true
```

**Terminal 3: mission + extended BT manager**
```bash
cd ~/suave_ws/src/suave/runner && ./scripts/launch_mission.sh bt extended my_run_name
```

`my_run_name` becomes the results file name (`my_run_name.csv`, see 4.3).

> **`--no-wsl2-network` is required on this machine.** Docker Desktop runs containers inside WSL2, so `sim_vehicle.py` thinks it is in WSL2 and sends telemetry to the Windows host instead of `127.0.0.1`. MAVROS then never connects (`/mavros/state` shows `connected: false`) and the tree waits forever at `arm_thrusters`. `runner/scripts/start_ardusub.sh` and `runner/runner.sh` do **not** pass this flag yet, so don't use them on Windows until they do.

### 3.3 What you should see

- The vehicle arms, switches to GUIDED and starts a spiral search in Gazebo.
- Terminal 3 shows the managed nodes starting. The BT's own messages go to a log file, not the terminal (see 5.1).
- When the pipeline is found the vehicle follows it. When inspection finishes (or the time limit hits), the metrics node writes the CSVs.

### 3.4 Stopping

Press **Ctrl+C** in terminal 3, then 2, then 1. If anything is stuck, `docker restart suave` from PowerShell is a clean reset. It kills every process in the container; your files are safe.

The processes don't exit on their own when the mission ends. Stop them before the next run, or the old and new nodes will mix.

---

## 4. Run it for numbers and printed output only (no GUI)

You don't need the browser for this, and it uses less CPU because Gazebo runs without its GUI window.

### 4.1 One run, by hand

Open **three PowerShell windows** and run `docker exec -it suave bash -l` in each. Then:

```bash
# Shell 1: ArduSub (no --console)
cd /tmp && sim_vehicle.py -L RATBeach -v ArduSub --model=JSON --no-wsl2-network

# Shell 2: Gazebo server only (gui false)
cd ~/suave_ws/src/suave/runner && ./scripts/launch_sim.sh false

# Shell 3: mission + extended BT
cd ~/suave_ws/src/suave/runner && ./scripts/launch_mission.sh bt extended headless_run_1
```

The container's environment sets `DISPLAY=:1`, so ArduSub may still open a small xterm on the (unwatched) desktop. That's harmless.

*Verified:* these are the same commands as the GUI run, which completed a full mission on this machine. The `gui false` variant uses the same launch files with the Gazebo GUI turned off; I haven't run it separately end to end.

### 4.2 Many runs: experiment campaigns (`suave run`)

The experiment runner starts ArduSub, the simulation and the mission for every run, injects the configured disturbances, waits for the mission to finish, and restarts everything for the next run. Run it **inside the container** (`docker exec -it suave bash -l`):

```bash
suave run \
  --config ~/suave_ws/src/suave/suave_runner/config/runner/extended_exp1_runner_config.yml \
  -p "ardupilot_executable:=sim_vehicle.py -L RATBeach -v ArduSub --model=JSON --no-wsl2-network"
```

- The `-p ardupilot_executable:=...` override adds the Windows fix. Without it every run hangs at arming.
- `extended_exp1_runner_config.yml` does **30 runs** with failures on thrusters 1 and 3. For a quick check, copy it to e.g. `suave_runner/config/runner/dev_runner_config.yml`, set `"num_runs": 1` (and maybe `run_duration`), and pass that file to `--config` instead.
- Add `--gui` to watch the runs on the desktop.
- Useful options: `--seed N` (randomisation), `--result-path PATH`, `--run-duration SEC`, more `-p KEY:=VALUE` overrides. `suave run --help` lists them.
- Per-run logs go to `<result_path>/logs/run_<exp>_<run>/` (`ardupilot.log`, `simulation/`, `experiment/`).

*Not yet tried on this machine.* I confirmed (with `--dry-run`) that the command is built correctly. Check that the first run arms before leaving a long campaign running.

### 4.3 The numbers: result files

Results are written to `~/suave/results` in the container, which is **`C:\Users\catie\Repos\suave-expansion\results`** on Windows (the [results/](../results/) folder at the top of this repo). That holds for every way of running: the launch files, the runner and `mission_config.yaml` all default to `~/suave/results`, and the container mounts the repo's `results/` folder there. You don't need to pass any path. If you ever set `result_path` / `--result-path` yourself, keep it under `~/suave/results` (e.g. `~/suave/results/campaign_1`) so the files still land in the repo. For a run named `NAME`:

| File | One row per | Columns |
|---|---|---|
| `NAME.csv` | mission | `mission name`, `datetime`, `initial pos (x,y)`, `mission duration (s)`, `pipeline found`, `time searching pipeline (s)`, `distance inspected (m)`, `mean reaction time (s)` |
| `NAME_component_recovery_time.csv` | thruster recovery | `mission_name`, `datetime`, `reaction time (s)` |
| `NAME_wv_reaction_time.csv` | water-visibility reaction | same three columns |
| `NAME_battery_reaction_time.csv` | battery reaction | same three columns |

Rows are **appended**, so reusing a name adds rows to the same files. Example from the first run on this machine:

```
mission name,datetime,"initial pos (x,y)",mission duration (s),pipeline found,time searching pipeline (s),distance inspected (m),mean reaction time (s)
suave_extended bt,22-Oct-2026-23-26-38,"(-17.0, 2.0)",1019,True,80,1.5,0.1751657024
```

For statistics across campaigns, the repo has skills and scripts for this: `suave analyze wilcoxon|mann-whitney|summarize`, plus `sort_results.py` for resumed or out-of-order CSVs. See [suave_runner/README.md](../suave_runner/README.md).

---

## 5. Checking what the behavior tree is doing

These work for both GUI and headless runs. Use a desktop terminal or `docker exec -it suave bash -l`. ROS is sourced automatically in these shells.

### 5.1 The BT's own log (the most useful)

The BT node logs as `mission_node`. Its output goes to a file, not the launch terminal:

```bash
tail -f $(ls -t ~/.ros/log/suave_bt_extended_*.log | head -1)
```

Each mode change it makes shows up, e.g.:

```
[mission_node]: Node f_generate_search_path mode changed to fd_spiral_high
[mission_node]: Node f_generate_search_path mode changed to fd_spiral_medium
[mission_node]: Node f_maintain_motion mode changed to fd_recover_thrusters
[mission_node]: Node f_maintain_motion mode changed to fd_all_thrusters
```

Lines like `change_mode(X) acknowledged but X_node has not reached it yet` are normal. They mean `ChangeMode` is waiting for the lifecycle transition to actually happen (see [action_change_mode.cpp](../suave_managing/suave_bt/src/suave_bt/action_change_mode.cpp)).

### 5.2 Live ROS state

```bash
ros2 topic echo /mavros/state                 # connected / armed / mode (GUIDED once the BT has started)
ros2 topic echo /diagnostics                  # monitor outputs the BT conditions read:
                                              #   "QA status": water_visibility, battery_level
                                              #   "Component status": c_thruster_1..6 (FAILURE / RECOVERED)
ros2 topic echo /pipeline/detected            # search_pipeline / is_pipeline_found
ros2 topic echo /pipeline/inspected           # inspect_pipeline / is_pipeline_inspected
ros2 topic echo /pipeline/distance_inspected  # metres inspected so far

ros2 lifecycle get /f_generate_search_path_node      # active / inactive (what ChangeMode checks)
ros2 lifecycle get /f_follow_pipeline_node
ros2 lifecycle get /f_maintain_motion_node
ros2 param get /f_generate_search_path_node spiral_altitude   # 3.0 / 2.0 / 1.0 = high / medium / low

ros2 node list                                # everything that's running
```

Quick health check: if the tree seems stuck at the start, `ros2 topic echo --once /mavros/state` showing `connected: false` means the ArduSub link is broken. You probably left out `--no-wsl2-network`, or ArduSub isn't running.

### 5.3 Making it adapt on purpose

Disturbances come from the mission config. The default is [suave_missions/config/mission_config.yaml](../suave_missions/config/mission_config.yaml):

| Setting | Where | Default | Triggers in the tree |
|---|---|---|---|
| `thruster_events` | `/thruster_monitor` | `(1,failure,35)`: thruster 1 fails 35 s after GUIDED | `thrusters_ok` fails → `fd_recover_thrusters` → `recover_thrusters` |
| `water_visibility_period`, `_min`, `_max` | `/water_visibility_observer_node` | 80 s, 1.25, 3.75 | `water_visibility` thresholds → `fd_spiral_high/medium/low` |
| `time_limit` | `/mission_node` | 300 s (counted from the start of the search) | `is_mission_aborted` → everything to `fd_unground` |
| `discharge_time` | `/battery_monitor` (node default, not in the file) | 200 s | `battery_level < 0.25` → recharge branch |

`thruster_events` format: `(thruster 1-6, failure|recovery, seconds since the previous event)`, one entry per event.

To try different conditions without touching the default, copy the file (e.g. to `suave_missions/config/my_test_config.yaml`), edit it, and pass it to the mission launch:

```bash
ros2 launch suave_bringup mission.launch.py adaptation_manager:=bt bt_executable:=suave_bt_extended \
  mission_type:=suave_extended result_filename:=my_test \
  mission_config:=/home/kasm-user/suave_ws/src/suave/suave_missions/config/my_test_config.yaml
```

That is what `launch_mission.sh bt extended NAME` runs, plus the `mission_config` argument.

### 5.4 Optional: watch the tree live in Groot2

Both BT executables publish the tree to **Groot2** on port 1667 (and 1668). The container doesn't publish those ports yet: recreate it with `-p 1667:1667 -p 1668:1668` (section 2), install Groot2 on Windows, and connect to `localhost:1667`. You then see every node's status on every tick. *Not set up or tested yet.*

---

## 6. Adding code to `suave_bt` and testing it

### 6.1 Where things live

| What | Where |
|---|---|
| BT node headers | [suave_managing/suave_bt/include/suave_bt/](../suave_managing/suave_bt/include/suave_bt/) |
| BT node implementations | [suave_managing/suave_bt/src/suave_bt/](../suave_managing/suave_bt/src/suave_bt/) |
| Node registration + tick loop | [src/suave_bt.cpp](../suave_managing/suave_bt/src/suave_bt.cpp) and [src/suave_bt_extended.cpp](../suave_managing/suave_bt/src/suave_bt_extended.cpp) |
| Trees (XML) | [bts/suave.xml](../suave_managing/suave_bt/bts/suave.xml), [bts/suave_extended.xml](../suave_managing/suave_bt/bts/suave_extended.xml) |
| Build | [CMakeLists.txt](../suave_managing/suave_bt/CMakeLists.txt) |
| Unit tests (gtest) | [test/test_action_clients.cpp](../suave_managing/suave_bt/test/test_action_clients.cpp) |

Base classes to choose from (see the class diagram):
- `BT::ConditionNode`: a yes/no check, e.g. `BatteryLevel`, `IsPipelineFound`.
- `BT::SyncActionNode`: finishes within one tick, e.g. `ChangeMode`.
- `BT::StatefulActionNode`: runs over several ticks, e.g. `ArmThrusters`.
- `BtActionClient<ActionT>`: wraps a ROS 2 action server with cancel/halt handling, e.g. `SearchPipeline`. You implement `makeGoal()` and `evaluateResult()`, plus `onLegacy*()` if the node must also work with `use_action_server:=false`.

Every node gets the shared ROS node from the blackboard: `config().blackboard->get<std::shared_ptr<suave_bt::SuaveMission>>("node")`. Create subscriptions and clients on it. Don't create your own `rclcpp::Node`.

### 6.2 Example: adding a condition node

A condition that succeeds once the vehicle has inspected at least `threshold` metres of pipeline. It's modelled on [is_pipeline_found.cpp](../suave_managing/suave_bt/src/suave_bt/is_pipeline_found.cpp) and [condition_battery_level.cpp](../suave_managing/suave_bt/src/suave_bt/condition_battery_level.cpp). It's an illustrative template that hasn't been compiled; adjust the names to your node.

**1. Header**: `include/suave_bt/condition_distance_inspected.hpp` (start with the Apache-2.0 header block the other files use)

```cpp
#ifndef SUAVE_BT__CONDITION_DISTANCE_INSPECTED_HPP_
#define SUAVE_BT__CONDITION_DISTANCE_INSPECTED_HPP_

#include <memory>
#include <string>

#include "behaviortree_cpp/behavior_tree.h"
#include "behaviortree_cpp/bt_factory.h"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "suave_bt/suave_mission.hpp"

namespace suave_bt
{

class DistanceInspected : public BT::ConditionNode
{
public:
  DistanceInspected(const std::string & xml_tag_name, const BT::NodeConfig & conf);

  BT::NodeStatus tick() override;

  static BT::PortsList providedPorts()
  {
    return BT::PortsList({BT::InputPort<double>("threshold")});
  }

private:
  std::shared_ptr<suave_bt::SuaveMission> node_;
  double distance_inspected_ = 0.0;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr distance_sub_;
};

}  // namespace suave_bt

#endif  // SUAVE_BT__CONDITION_DISTANCE_INSPECTED_HPP_
```

**2. Implementation**: `src/suave_bt/condition_distance_inspected.cpp`

```cpp
#include "suave_bt/condition_distance_inspected.hpp"

namespace suave_bt
{

DistanceInspected::DistanceInspected(
  const std::string & xml_tag_name, const BT::NodeConfig & conf)
: BT::ConditionNode(xml_tag_name, conf)
{
  node_ = config().blackboard->get<std::shared_ptr<suave_bt::SuaveMission>>("node");
  distance_sub_ = node_->create_subscription<std_msgs::msg::Float32>(
    "/pipeline/distance_inspected", 10,
    [this](const std_msgs::msg::Float32 & msg) {distance_inspected_ = msg.data;});
}

BT::NodeStatus DistanceInspected::tick()
{
  double threshold = 0.0;
  if (!getInput("threshold", threshold)) {
    throw BT::RuntimeError("distance_inspected: missing required input [threshold]");
  }
  return distance_inspected_ >= threshold ?
         BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
}

}  // namespace suave_bt
```

**3. CMakeLists.txt**: add a library and append it to `plugin_libs` (both executables link everything in that list):

```cmake
add_library(condition_distance_inspected SHARED src/suave_bt/condition_distance_inspected.cpp)

list(APPEND plugin_libs
  ...
  condition_distance_inspected
)
```

If a test uses it, also add it to `target_link_libraries(test_action_clients ...)`. If you use a message package that isn't already in the `dependencies` list, add it there and as a `<depend>` in `package.xml`. (`std_msgs` is already used.)

**4. Register the XML tag name** in [suave_bt_extended.cpp](../suave_managing/suave_bt/src/suave_bt_extended.cpp), and in [suave_bt.cpp](../suave_managing/suave_bt/src/suave_bt.cpp) too if the base tree should be able to use it:

```cpp
#include "suave_bt/condition_distance_inspected.hpp"
...
factory.registerNodeType<suave_bt::DistanceInspected>("distance_inspected");
```

**5. Use it in the tree**: [bts/suave_extended.xml](../suave_managing/suave_bt/bts/suave_extended.xml). Put it in the tree, and add a model entry in `<TreeNodesModel>` so Groot2 knows it:

```xml
<distance_inspected threshold="10.0"/>
...
<Condition ID="distance_inspected" editable="true">
  <input_port name="threshold"/>
</Condition>
```

How you place it matters. Under a `ReactiveSequence` or `ReactiveFallback` it is re-checked on **every tick**, so it can interrupt (halt) running actions. Under a `Sequence` / `SequenceWithMemory` it is checked once, when reached. See the tree diagram.

### 6.3 What needs a rebuild

| You changed | Rebuild needed? |
|---|---|
| Any `.cpp` / `.hpp` in `suave_bt` | **Yes**: `colcon build ... suave_bt` |
| `CMakeLists.txt`, `package.xml` | **Yes** |
| An **existing** XML tree in `bts/` | **No**: the install folder links back to your source file (`--symlink-install`) |
| A **new** file in `bts/` or `launch/` | **Yes** (so it gets installed/linked) |
| Python nodes / launch files / YAML configs in other packages (existing files) | **No** |
| New Python files, entry points, `.msg`/`.action` definitions | **Yes**, for that package |

### 6.4 Build and unit-test

**Option A: suave CLI from PowerShell** (it runs `docker exec -it`, so use **PowerShell or Windows Terminal**. Git Bash/mintty fails with "the input device is not a TTY" unless you prefix `winpty`):

```powershell
python suave_cli/bin/suave build suave_bt          # build only
python suave_cli/bin/suave test suave_bt           # build + run tests; non-zero exit if anything fails
python suave_cli/bin/suave test suave_bt --lint    # also run the linters (uncrustify, cpplint, ...)
python suave_cli/bin/suave --dry-run test suave_bt # just print what it would run
```

**Option B: inside the container** (`docker exec -it suave bash -l`):

```bash
cd ~/suave_ws
colcon build --symlink-install --packages-select suave_bt
colcon test --packages-select suave_bt --event-handlers console_direct+
colcon test-result --verbose --test-result-base build/suave_bt
```

Baseline on this machine (before any changes): **166 tests, 0 errors, 0 failures, 31 skipped**. Compare against that after your change.

If the C++ style check fails, auto-format from inside the package directory in the container:

```bash
cd ~/suave_ws/src/suave/suave_managing/suave_bt && ament_uncrustify --reformat
```

### 6.5 Writing a unit test for your node

Add a `TEST_F(ActionClientTest, ...)` to [test/test_action_clients.cpp](../suave_managing/suave_bt/test/test_action_clients.cpp) (the fixture there already calls `rclcpp::init`/`shutdown`), or create a new test file with its own `ament_add_gtest(...)` in `CMakeLists.txt`. Pattern: build a `SuaveMission` + blackboard, load a one-node tree from text, feed it ROS messages, tick it. Untested sketch for the example node:

```cpp
#include "std_msgs/msg/float32.hpp"
#include "suave_bt/condition_distance_inspected.hpp"

TEST_F(ActionClientTest, distance_inspected_succeeds_after_threshold)
{
  auto mission = std::make_shared<suave_bt::SuaveMission>("distance_inspected_test");
  auto blackboard = BT::Blackboard::create();
  blackboard->set<std::shared_ptr<suave_bt::SuaveMission>>("node", mission);

  BT::BehaviorTreeFactory factory;
  factory.registerNodeType<suave_bt::DistanceInspected>("distance_inspected");
  auto tree = factory.createTreeFromText(R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="T"><distance_inspected threshold="5.0"/></BehaviorTree>
    </root>)", blackboard);

  EXPECT_EQ(tree.tickOnce(), BT::NodeStatus::FAILURE);  // nothing inspected yet

  auto pub = mission->create_publisher<std_msgs::msg::Float32>(
    "/pipeline/distance_inspected", 10);
  std_msgs::msg::Float32 msg;
  msg.data = 6.0;
  auto status = BT::NodeStatus::FAILURE;
  for (int i = 0; i < 40 && status != BT::NodeStatus::SUCCESS; ++i) {
    pub->publish(msg);
    rclcpp::spin_some(mission);  // deliver the message to the node's subscription
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    status = tree.tickOnce();
  }
  EXPECT_EQ(status, BT::NodeStatus::SUCCESS);
}
```

For nodes built on `BtActionClient`, follow the existing tests: `evaluate_result<...>` checks how results map to SUCCESS/FAILURE, and the `halt_cancels_an_accepted_goal` / `time_limit_*` tests run a small fake action server.

### 6.6 Test it in the simulation

Unit tests pass → run it for real:

1. `docker restart suave` (clean slate), then start ArduSub, the simulation and the mission as in section 3 or 4.
2. Follow the BT log (5.1) and look for your node's effect: mode changes, or `RCLCPP_INFO` lines you add.
3. If your node reacts to a condition, force that condition with a custom mission config (5.3).
4. Check the CSVs in the repo's [results/](../results/) folder (4.3).
5. When it behaves, run a small campaign (4.2, `num_runs: 1–3`) before a full one.

### 6.7 The loop, summarised

```
edit on Windows (VS Code)
   │
   ├─ C++ / CMake / new files? ──► python suave_cli/bin/suave build suave_bt
   │
   ├─► python suave_cli/bin/suave test suave_bt [--lint]   (expect 0 failures)
   │
   ├─► docker restart suave → run ArduSub / sim / mission (GUI §3 or headless §4)
   │      └─ watch: BT log, ros2 topic echo, Gazebo
   │
   └─► check results CSVs → small campaign → commit
```

---

## 7. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Tree stuck at the start; `/mavros/state` shows `connected: false` | ArduSub started without `--no-wsl2-network`, or ArduSub isn't running. Restart ArduSub with the flag. |
| `No such file or directory: 'mavproxy.py'` | `~/.local/bin` isn't on the PATH. Open a new terminal or run `source ~/.bashrc`. If the container was recreated, re-add the PATH line (section 2). |
| `ros2: command not found` | Same: use `bash -l`. As a fallback: `source /opt/ros/humble/setup.bash && source ~/suave_ws/install/setup.bash`. |
| `/usr/bin/env: 'python3\r'` or `bash\r` errors | Windows line endings got back in. Check `git config core.autocrlf` is `input`. Re-checkout with `git rm -r --cached . ; git reset --hard` (commit or stash changes first). |
| `the input device is not a TTY` | Running the `suave` CLI from Git Bash. Use PowerShell / Windows Terminal, or prefix `winpty`. |
| Old nodes still running / weird duplicate behaviour | You started a new run without stopping the old one. `docker restart suave`. |
| **The container stops by itself** (`docker ps -a` shows `Exited (0)`) | **Not solved yet.** It happened 3 times while setting this up: twice ~20–60 s after the simulation started, once while idle long after a mission had finished. The desktop's start script received a stop signal, which shuts the container down cleanly. The sender wasn't found. Results already written are kept (they're on Windows). Fix: `docker start suave` and start the run again. It has been less frequent since each terminal runs as its own window (desktop terminals or `docker exec`). |
| Killing processes with `pkill -f <pattern>` closes the wrong things | `pkill -f` matches full command lines, including the shell running the `pkill` and `xfce4-terminal` processes that host several windows. Prefer Ctrl+C in each terminal, or `docker restart suave`. |
