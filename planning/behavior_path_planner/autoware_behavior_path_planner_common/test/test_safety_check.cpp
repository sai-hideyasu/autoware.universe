// Copyright 2023 TIER IV, Inc.
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

#include "autoware/behavior_path_planner_common/marker_utils/utils.hpp"
#include "autoware/behavior_path_planner_common/utils/path_safety_checker/path_safety_checker_parameters.hpp"
#include "autoware/behavior_path_planner_common/utils/path_safety_checker/safety_check.hpp"

#include <autoware/universe_utils/math/unit_conversion.hpp>

#include <geometry_msgs/msg/pose.hpp>

#include <boost/geometry.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

constexpr double epsilon = 1e-6;

using autoware::behavior_path_planner::utils::path_safety_checker::calcInterpolatedPoseWithVelocity;
using autoware::behavior_path_planner::utils::path_safety_checker::CollisionCheckDebug;
using autoware::behavior_path_planner::utils::path_safety_checker::PoseWithVelocityStamped;
using autoware::universe_utils::createPoint;
using autoware::universe_utils::createQuaternionFromRPY;
using autoware::universe_utils::Point2d;
using autoware::universe_utils::Polygon2d;
using autoware_perception_msgs::msg::Shape;
using geometry_msgs::msg::Point;
using geometry_msgs::msg::Pose;
using geometry_msgs::msg::Twist;

geometry_msgs::msg::Pose createPose(
  double x, double y, double z, double roll, double pitch, double yaw)
{
  geometry_msgs::msg::Pose p;
  p.position = createPoint(x, y, z);
  p.orientation = createQuaternionFromRPY(roll, pitch, yaw);
  return p;
}

std::vector<PoseWithVelocityStamped> createTestPath()
{
  std::vector<PoseWithVelocityStamped> path;
  path.emplace_back(0.0, createPose(0.0, 0.0, 0.0, 0.0, 0.0, 0.0), 1.0);
  path.emplace_back(1.0, createPose(1.0, 0.0, 0.0, 0.0, 0.0, 0.0), 2.0);
  path.emplace_back(2.0, createPose(2.0, 0.0, 0.0, 0.0, 0.0, 0.0), 3.0);
  return path;
}

TEST(BehaviorPathPlanningSafetyUtilsTest, createExtendedEgoPolygon)
{
  using autoware::behavior_path_planner::utils::path_safety_checker::createExtendedPolygon;

  autoware::vehicle_info_utils::VehicleInfo vehicle_info;
  vehicle_info.max_longitudinal_offset_m = 4.0;
  vehicle_info.vehicle_width_m = 2.0;
  vehicle_info.rear_overhang_m = 1.0;
  CollisionCheckDebug debug;

  {
    Pose ego_pose;
    ego_pose.position = autoware::universe_utils::createPoint(0.0, 0.0, 0.0);
    ego_pose.orientation = autoware::universe_utils::createQuaternionFromYaw(0.0);

    const double lon_length = 10.0;
    const double lat_margin = 2.0;
    const bool is_stopped_object = false;

    const auto polygon = createExtendedPolygon(
      ego_pose, vehicle_info, lon_length, lat_margin, is_stopped_object, debug);

    EXPECT_EQ(polygon.outer().size(), static_cast<unsigned int>(5));

    const auto & p1 = polygon.outer().at(0);
    const auto & p2 = polygon.outer().at(1);
    const auto & p3 = polygon.outer().at(2);
    const auto & p4 = polygon.outer().at(3);
    EXPECT_NEAR(p1.x(), 14.0, epsilon);
    EXPECT_NEAR(p1.y(), 3.0, epsilon);
    EXPECT_NEAR(p2.x(), 14.0, epsilon);
    EXPECT_NEAR(p2.y(), -3.0, epsilon);
    EXPECT_NEAR(p3.x(), -1.0, epsilon);
    EXPECT_NEAR(p3.y(), -3.0, epsilon);
    EXPECT_NEAR(p4.x(), -1.0, epsilon);
    EXPECT_NEAR(p4.y(), 3.0, epsilon);
  }

  {
    Pose ego_pose;
    ego_pose.position = autoware::universe_utils::createPoint(3.0, 4.0, 0.0);
    ego_pose.orientation = autoware::universe_utils::createQuaternionFromYaw(0.0);

    const double lon_length = 10.0;
    const double lat_margin = 2.0;
    const bool is_stopped_object = false;

    const auto polygon = createExtendedPolygon(
      ego_pose, vehicle_info, lon_length, lat_margin, is_stopped_object, debug);

    EXPECT_EQ(polygon.outer().size(), static_cast<unsigned int>(5));

    const auto & p1 = polygon.outer().at(0);
    const auto & p2 = polygon.outer().at(1);
    const auto & p3 = polygon.outer().at(2);
    const auto & p4 = polygon.outer().at(3);
    EXPECT_NEAR(p1.x(), 17.0, epsilon);
    EXPECT_NEAR(p1.y(), 7.0, epsilon);
    EXPECT_NEAR(p2.x(), 17.0, epsilon);
    EXPECT_NEAR(p2.y(), 1.0, epsilon);
    EXPECT_NEAR(p3.x(), 2.0, epsilon);
    EXPECT_NEAR(p3.y(), 1.0, epsilon);
    EXPECT_NEAR(p4.x(), 2.0, epsilon);
    EXPECT_NEAR(p4.y(), 7.0, epsilon);
  }

  {
    Pose ego_pose;
    ego_pose.position = autoware::universe_utils::createPoint(0.0, 0.0, 0.0);
    ego_pose.orientation =
      autoware::universe_utils::createQuaternionFromYaw(autoware::universe_utils::deg2rad(60));

    const double lon_length = 10.0;
    const double lat_margin = 2.0;
    const bool is_stopped_object = false;

    const auto polygon = createExtendedPolygon(
      ego_pose, vehicle_info, lon_length, lat_margin, is_stopped_object, debug);

    EXPECT_EQ(polygon.outer().size(), static_cast<unsigned int>(5));

    const auto & p1 = polygon.outer().at(0);
    const auto & p2 = polygon.outer().at(1);
    const auto & p3 = polygon.outer().at(2);
    const auto & p4 = polygon.outer().at(3);
    EXPECT_NEAR(p1.x(), 7.0 - 1.5 * std::sqrt(3), epsilon);
    EXPECT_NEAR(p1.y(), 7.0 * std::sqrt(3) + 1.5, epsilon);
    EXPECT_NEAR(p2.x(), 7.0 + 1.5 * std::sqrt(3), epsilon);
    EXPECT_NEAR(p2.y(), 7.0 * std::sqrt(3) - 1.5, epsilon);
    EXPECT_NEAR(p3.x(), 1.5 * std::sqrt(3) - 0.5, epsilon);
    EXPECT_NEAR(p3.y(), -1.5 - std::sqrt(3) / 2.0, epsilon);
    EXPECT_NEAR(p4.x(), -1.5 * std::sqrt(3) - 0.5, epsilon);
    EXPECT_NEAR(p4.y(), 1.5 - std::sqrt(3) / 2.0, epsilon);
  }
}

TEST(BehaviorPathPlanningSafetyUtilsTest, createExtendedObjPolygon)
{
  using autoware::behavior_path_planner::utils::path_safety_checker::createExtendedPolygon;
  using autoware::universe_utils::createPoint;
  using autoware::universe_utils::createQuaternionFromYaw;

  {
    Pose obj_pose;
    obj_pose.position = createPoint(0.0, 0.0, 0.0);
    obj_pose.orientation = autoware::universe_utils::createQuaternionFromYaw(0.0);

    Shape shape;
    shape.type = autoware_perception_msgs::msg::Shape::POLYGON;
    shape.footprint.points.resize(5);
    shape.footprint.points.at(0).x = 3.0;
    shape.footprint.points.at(0).y = 0.0;
    shape.footprint.points.at(1).x = 0.0;
    shape.footprint.points.at(1).y = -2.0;
    shape.footprint.points.at(2).x = -2.0;
    shape.footprint.points.at(2).y = 0.0;
    shape.footprint.points.at(3).x = -1.0;
    shape.footprint.points.at(3).y = 0.5;
    shape.footprint.points.at(4).x = 2.0;
    shape.footprint.points.at(4).y = 1.0;

    const double lon_length = 10.0;
    const double lat_margin = 2.0;
    const bool is_stopped_object = false;

    CollisionCheckDebug debug;

    using autoware::behavior_path_planner::utils::path_safety_checker::
      PoseWithVelocityAndPolygonStamped;

    PoseWithVelocityAndPolygonStamped obj_pose_with_poly(
      0.0, obj_pose, 0.0, autoware::universe_utils::toPolygon2d(obj_pose, shape));
    const auto polygon =
      createExtendedPolygon(obj_pose_with_poly, lon_length, lat_margin, is_stopped_object, debug);

    EXPECT_EQ(polygon.outer().size(), static_cast<unsigned int>(5));

    const auto & p1 = polygon.outer().at(0);
    const auto & p2 = polygon.outer().at(1);
    const auto & p3 = polygon.outer().at(2);
    const auto & p4 = polygon.outer().at(3);
    EXPECT_NEAR(p1.x(), 13.0, epsilon);
    EXPECT_NEAR(p1.y(), 3.0, epsilon);
    EXPECT_NEAR(p2.x(), 13.0, epsilon);
    EXPECT_NEAR(p2.y(), -4.0, epsilon);
    EXPECT_NEAR(p3.x(), -2.0, epsilon);
    EXPECT_NEAR(p3.y(), -4.0, epsilon);
    EXPECT_NEAR(p4.x(), -2.0, epsilon);
    EXPECT_NEAR(p4.y(), 3.0, epsilon);
  }
}

TEST(BehaviorPathPlanningSafetyUtilsTest, calcRssDistance)
{
  using autoware::behavior_path_planner::utils::path_safety_checker::calcRssDistance;
  using autoware::behavior_path_planner::utils::path_safety_checker::RSSparams;

  {
    const double front_vel = 5.0;
    const double front_decel = -2.0;
    const double rear_vel = 10.0;
    const double rear_decel = -1.0;
    RSSparams params;
    params.rear_vehicle_reaction_time = 1.0;
    params.rear_vehicle_safety_time_margin = 1.0;
    params.longitudinal_distance_min_threshold = 3.0;
    params.rear_vehicle_deceleration = rear_decel;
    params.front_vehicle_deceleration = front_decel;

    EXPECT_NEAR(calcRssDistance(front_vel, rear_vel, params), 63.75, epsilon);
  }
}

// Basic interpolation test
TEST(CalcInterpolatedPoseWithVelocityTest, BasicInterpolation)
{
  auto path = createTestPath();
  auto result = calcInterpolatedPoseWithVelocity(path, 0.5);

  ASSERT_TRUE(result.has_value());
  EXPECT_NEAR(result->time, 0.5, 1e-6);
  EXPECT_NEAR(result->pose.position.x, 0.5, 1e-6);
  EXPECT_NEAR(result->velocity, 1.5, 1e-6);
}

// Boundary conditions test
TEST(CalcInterpolatedPoseWithVelocityTest, BoundaryConditions)
{
  auto path = createTestPath();

  // First point of the path
  auto start_result = calcInterpolatedPoseWithVelocity(path, 0.0);
  ASSERT_TRUE(start_result.has_value());
  EXPECT_NEAR(start_result->time, 0.0, 1e-6);
  EXPECT_NEAR(start_result->pose.position.x, 0.0, 1e-6);
  EXPECT_NEAR(start_result->velocity, 1.0, 1e-6);

  // Last point of the path
  auto end_result = calcInterpolatedPoseWithVelocity(path, 2.0);
  ASSERT_TRUE(end_result.has_value());
  EXPECT_NEAR(end_result->time, 2.0, 1e-6);
  EXPECT_NEAR(end_result->pose.position.x, 2.0, 1e-6);
  EXPECT_NEAR(end_result->velocity, 3.0, 1e-6);
}

// Invalid input test
TEST(CalcInterpolatedPoseWithVelocityTest, InvalidInput)
{
  auto path = createTestPath();

  // Empty path
  EXPECT_FALSE(calcInterpolatedPoseWithVelocity({}, 1.0).has_value());

  // Negative relative time
  EXPECT_FALSE(calcInterpolatedPoseWithVelocity(path, -1.0).has_value());

  // Relative time greater than the last time in the path
  EXPECT_FALSE(calcInterpolatedPoseWithVelocity(path, 3.0).has_value());
}

// Special cases test
TEST(CalcInterpolatedPoseWithVelocityTest, DISABLED_SpecialCases)
{
  // Case with consecutive points at the same time
  std::vector<PoseWithVelocityStamped> same_time_path;
  same_time_path.emplace_back(0.0, createPose(0.0, 0.0, 0.0, 0.0, 0.0, 0.0), 1.0);
  same_time_path.emplace_back(0.0, createPose(1.0, 0.0, 0.0, 0.0, 0.0, 0.0), 2.0);
  same_time_path.emplace_back(1.0, createPose(2.0, 0.0, 0.0, 0.0, 0.0, 0.0), 3.0);

  auto same_time_result = calcInterpolatedPoseWithVelocity(same_time_path, 0.0);
  ASSERT_TRUE(same_time_result.has_value());
  EXPECT_NEAR(same_time_result->pose.position.x, 0.0, 1e-6);
  EXPECT_NEAR(same_time_result->velocity, 1.0, 1e-6);

  // Case with reversed time order
  std::vector<PoseWithVelocityStamped> reverse_time_path;
  reverse_time_path.emplace_back(2.0, createPose(0.0, 0.0, 0.0, 0.0, 0.0, 0.0), 1.0);
  reverse_time_path.emplace_back(1.0, createPose(1.0, 0.0, 0.0, 0.0, 0.0, 0.0), 2.0);
  reverse_time_path.emplace_back(0.0, createPose(2.0, 0.0, 0.0, 0.0, 0.0, 0.0), 3.0);

  auto reverse_time_result = calcInterpolatedPoseWithVelocity(reverse_time_path, 1.5);
  ASSERT_FALSE(reverse_time_result.has_value());
}
