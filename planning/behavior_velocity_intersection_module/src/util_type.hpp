// Copyright 2022 Tier IV, Inc.
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

#ifndef UTIL_TYPE_HPP_
#define UTIL_TYPE_HPP_

#include <autoware_auto_planning_msgs/msg/path_with_lane_id.hpp>

#include <lanelet2_core/primitives/CompoundPolygon.h>
#include <lanelet2_core/primitives/Lanelet.h>

#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace behavior_velocity_planner::util
{
struct IntersectionLanelets
{
  bool tl_arrow_solid_on;
  lanelet::ConstLanelets attention;
  lanelet::ConstLanelets conflicting;
  lanelet::ConstLanelets adjacent;
  lanelet::ConstLanelets occlusion_attention;  // for occlusion detection
  std::vector<lanelet::CompoundPolygon3d> attention_area;
  std::vector<lanelet::CompoundPolygon3d> conflicting_area;
  std::vector<lanelet::CompoundPolygon3d> adjacent_area;
  std::vector<lanelet::CompoundPolygon3d> occlusion_attention_area;
  // the first area intersecting with the path
  // even if lane change/re-routing happened on the intersection, these areas area are supposed to
  // be invariant under the 'associative' lanes.
  std::optional<lanelet::CompoundPolygon3d> first_conflicting_area;
  std::optional<lanelet::CompoundPolygon3d> first_detection_area;
};

struct DescritizedLane
{
  int lane_id;
  // discrete fine lines from left to right
  std::vector<lanelet::ConstLineString2d> divisions;
};

struct InterpolatedPathInfo
{
  autoware_auto_planning_msgs::msg::PathWithLaneId path;
  double ds;
  int lane_id;
  std::set<int> associative_lane_ids;
  std::optional<std::pair<size_t, size_t>> lane_id_interval;
};

struct IntersectionStopLines
{
  // NOTE: for baselink
  size_t default_stop_line;
  size_t closest_idx;
  size_t occlusion_peeking_stop_line;
  size_t pass_judge_line;
};

}  // namespace behavior_velocity_planner::util

#endif  // UTIL_TYPE_HPP_
