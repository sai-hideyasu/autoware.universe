// Copyright 2024 TIER IV, Inc.
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

#ifndef NODE_HPP_
#define NODE_HPP_

#include "autoware/motion_utils/trajectory/interpolation.hpp"
#include "autoware/motion_utils/trajectory/trajectory.hpp"
#include "autoware/universe_utils/ros/polling_subscriber.hpp"
#include "autoware_frenet_planner/frenet_planner.hpp"
#include "autoware_path_sampler/prepare_inputs.hpp"
#include "autoware_path_sampler/utils/trajectory_utils.hpp"
#include "rosbag2_cpp/reader.hpp"
#include "type_alias.hpp"

#include <autoware/route_handler/route_handler.hpp>
#include <autoware_vehicle_info_utils/vehicle_info_utils.hpp>
#include <magic_enum.hpp>
#include <rclcpp/rclcpp.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace autoware::behavior_analyzer
{

namespace
{
Point vector2point(const geometry_msgs::msg::Vector3 & v)
{
  return autoware::universe_utils::createPoint(v.x, v.y, v.z);
}

tf2::Vector3 from_msg(const Point & p)
{
  return tf2::Vector3(p.x, p.y, p.z);
}

tf2::Vector3 get_velocity_in_world_coordinate(const PredictedObjectKinematics & kinematics)
{
  const auto pose = kinematics.initial_pose_with_covariance.pose;
  const auto v_local = kinematics.initial_twist_with_covariance.twist.linear;
  const auto v_world = autoware::universe_utils::transformPoint(vector2point(v_local), pose);

  return from_msg(v_world) - from_msg(pose.position);
}

tf2::Vector3 get_velocity_in_world_coordinate(const Odometry & odometry)
{
  const auto pose = odometry.pose.pose;
  const auto v_local = odometry.twist.twist.linear;
  const auto v_world = autoware::universe_utils::transformPoint(vector2point(v_local), pose);

  return from_msg(v_world) - from_msg(pose.position);
}

tf2::Vector3 get_velocity_in_world_coordinate(const TrajectoryPoint & point)
{
  const auto pose = point.pose;
  const auto v_local =
    geometry_msgs::build<Vector3>().x(point.longitudinal_velocity_mps).y(0.0).z(0.0);
  const auto v_world = autoware::universe_utils::transformPoint(vector2point(v_local), pose);

  return from_msg(v_world) - from_msg(pose.position);
}

double time_to_collision(
  const PredictedObjects & objects, const Pose & p_ego, const tf2::Vector3 & v_ego)
{
  if (objects.objects.empty()) {
    return std::numeric_limits<double>::max();
  }

  std::vector<double> time_to_collisions(objects.objects.size());

  for (const auto & object : objects.objects) {
    const auto p_object = object.kinematics.initial_pose_with_covariance.pose;
    const auto v_ego2object =
      autoware::universe_utils::point2tfVector(p_ego.position, p_object.position);

    const auto v_object = get_velocity_in_world_coordinate(object.kinematics);
    const auto v_relative = tf2::tf2Dot(v_ego2object.normalized(), v_ego) -
                            tf2::tf2Dot(v_ego2object.normalized(), v_object);

    time_to_collisions.push_back(v_ego2object.length() / v_relative);
  }

  const auto itr = std::remove_if(
    time_to_collisions.begin(), time_to_collisions.end(),
    [](const auto & value) { return value < 1e-3; });
  time_to_collisions.erase(itr, time_to_collisions.end());

  std::sort(time_to_collisions.begin(), time_to_collisions.end());

  return time_to_collisions.front();
}
}  // namespace

enum class METRIC {
  LATERAL_ACCEL = 0,
  LONGITUDINAL_ACCEL = 1,
  LONGITUDINAL_JERK = 2,
  TRAVEL_DISTANCE = 3,
  MINIMUM_TTC = 4,
  SIZE
};

enum class SCORE {
  LATERAL_COMFORTABILITY = 0,
  LONGITUDINAL_COMFORTABILITY = 1,
  EFFICIENCY = 2,
  SAFETY = 3,
  SIZE
};

struct FrenetPoint
{
  double length{0.0};    // longitudinal
  double distance{0.0};  // lateral
};

// data conversions
template <class T>
FrenetPoint convertToFrenetPoint(
  const T & points, const Point & search_point_geom, const size_t seg_idx)
{
  FrenetPoint frenet_point;

  const double longitudinal_length =
    autoware::motion_utils::calcLongitudinalOffsetToSegment(points, seg_idx, search_point_geom);
  frenet_point.length =
    autoware::motion_utils::calcSignedArcLength(points, 0, seg_idx) + longitudinal_length;
  frenet_point.distance =
    autoware::motion_utils::calcLateralOffset(points, search_point_geom, seg_idx);

  return frenet_point;
}

autoware::frenet_planner::SamplingParameters prepareSamplingParameters(
  const autoware::sampler_common::State & initial_state, const double base_length,
  const autoware::sampler_common::transform::Spline2D & path_spline, const double trajectory_length)
{
  autoware::frenet_planner::SamplingParameters sampling_parameters;

  // calculate target lateral positions
  std::vector<double> target_lateral_positions = {-4.5, -2.5, 0.0, 2.5, 4.5};
  sampling_parameters.resolution = 0.5;
  const auto max_s = path_spline.lastS();
  autoware::frenet_planner::SamplingParameter p;
  p.target_duration = 10.0;
  for (const auto target_length : {trajectory_length}) {
    p.target_state.position.s = std::min(
      max_s, path_spline.frenet(initial_state.pose).s + std::max(0.0, target_length - base_length));
    for (const auto target_longitudinal_velocity : {5.56, 11.1}) {
      p.target_state.longitudinal_velocity = target_longitudinal_velocity;
      for (const auto target_longitudinal_acceleration : {0.0}) {
        p.target_state.longitudinal_acceleration = target_longitudinal_acceleration;
        for (const auto target_lateral_position : target_lateral_positions) {
          p.target_state.position.d = target_lateral_position;
          for (const auto target_lat_vel : {0.0}) {
            p.target_state.lateral_velocity = target_lat_vel;
            for (const auto target_lat_acc : {-0.2, -0.1, 0.0, 0.1, 0.2}) {
              p.target_state.lateral_acceleration = target_lat_acc;
              sampling_parameters.parameters.push_back(p);
            }
          }
        }
      }
    }
    if (p.target_state.position.s == max_s) break;
  }
  return sampling_parameters;
}

template <class T>
struct Buffer
{
  std::vector<T> msgs;

  const double BUFFER_TIME = 20.0 * 1e9;

  bool is_ready() const
  {
    if (msgs.empty()) {
      return false;
    }

    return rclcpp::Time(msgs.back().header.stamp).nanoseconds() -
             rclcpp::Time(msgs.front().header.stamp).nanoseconds() >
           BUFFER_TIME;
  }

  void remove_old_data(const rcutils_time_point_value_t now)
  {
    const auto itr = std::remove_if(msgs.begin(), msgs.end(), [&now, this](const auto & msg) {
      return rclcpp::Time(msg.header.stamp).nanoseconds() < now;
    });
    msgs.erase(itr, msgs.end());
  }

  void append(const T & msg) { msgs.push_back(msg); }

  T get() const { return msgs.front(); }

  std::optional<T> get(const rcutils_time_point_value_t now) const
  {
    const auto itr = std::find_if(msgs.begin(), msgs.end(), [&now, this](const auto & msg) {
      return rclcpp::Time(msg.header.stamp).nanoseconds() > now;
    });

    if (itr == msgs.end()) {
      return std::nullopt;
    }

    return *itr;
  }

  std::vector<T> get_all_data() const { return msgs; }
};

template <>
bool Buffer<SteeringReport>::is_ready() const
{
  if (msgs.empty()) {
    return false;
  }

  return rclcpp::Time(msgs.back().stamp).nanoseconds() -
           rclcpp::Time(msgs.front().stamp).nanoseconds() >
         BUFFER_TIME;
}

template <>
void Buffer<SteeringReport>::remove_old_data(const rcutils_time_point_value_t now)
{
  if (msgs.empty()) {
    return;
  }

  const auto itr = std::remove_if(msgs.begin(), msgs.end(), [&now, this](const auto & msg) {
    return rclcpp::Time(msg.stamp).nanoseconds() < now;
  });
  msgs.erase(itr, msgs.end());
}

template <>
std::optional<SteeringReport> Buffer<SteeringReport>::get(
  const rcutils_time_point_value_t now) const
{
  const auto itr = std::find_if(msgs.begin(), msgs.end(), [&now, this](const auto & msg) {
    return rclcpp::Time(msg.stamp).nanoseconds() > now;
  });

  if (itr == msgs.end()) {
    return std::nullopt;
  }

  return *itr;
}

template <>
bool Buffer<TFMessage>::is_ready() const
{
  if (msgs.empty()) {
    return false;
  }

  if (msgs.front().transforms.empty()) {
    return false;
  }

  if (msgs.back().transforms.empty()) {
    return false;
  }

  return rclcpp::Time(msgs.back().transforms.front().header.stamp).nanoseconds() -
           rclcpp::Time(msgs.front().transforms.front().header.stamp).nanoseconds() >
         BUFFER_TIME;
}

template <>
void Buffer<TFMessage>::remove_old_data(const rcutils_time_point_value_t now)
{
  if (msgs.empty()) {
    return;
  }

  const auto itr = std::remove_if(msgs.begin(), msgs.end(), [&now, this](const auto & msg) {
    return rclcpp::Time(msg.transforms.front().header.stamp).nanoseconds() < now;
  });
  msgs.erase(itr, msgs.end());
}

template <>
std::optional<TFMessage> Buffer<TFMessage>::get(const rcutils_time_point_value_t now) const
{
  const auto itr = std::find_if(msgs.begin(), msgs.end(), [&now, this](const auto & msg) {
    return rclcpp::Time(msg.transforms.front().header.stamp).nanoseconds() > now;
  });

  if (itr == msgs.end()) {
    return std::nullopt;
  }

  return *itr;
}

struct TrimmedData
{
  explicit TrimmedData(const rcutils_time_point_value_t timestamp) : timestamp{timestamp} {}

  Buffer<TFMessage> buf_tf;
  Buffer<Odometry> buf_odometry;
  Buffer<PredictedObjects> buf_objects;
  Buffer<AccelWithCovarianceStamped> buf_accel;
  Buffer<SteeringReport> buf_steer;
  Buffer<Trajectory> buf_trajectory;

  rcutils_time_point_value_t timestamp;

  void update(const rcutils_time_point_value_t dt)
  {
    timestamp += dt;
    remove_old_data();
  }

  void remove_old_data()
  {
    buf_tf.remove_old_data(timestamp);
    buf_odometry.remove_old_data(timestamp);
    buf_objects.remove_old_data(timestamp);
    buf_accel.remove_old_data(timestamp);
    buf_steer.remove_old_data(timestamp);
    buf_trajectory.remove_old_data(timestamp);
  }

  bool is_ready() const
  {
    return buf_tf.is_ready() && buf_objects.is_ready() && buf_odometry.is_ready() &&
           buf_accel.is_ready() && buf_steer.is_ready() && buf_trajectory.is_ready();
  }
};

struct CommonData
{
  CommonData(
    const std::shared_ptr<TrimmedData> & trimmed_data,
    const vehicle_info_utils::VehicleInfo & vehicle_info, const size_t resample_num,
    const double time_resolution, const std::string & tag)
  : vehicle_info{vehicle_info}, resample_num(resample_num), tag{tag}
  {
    for (size_t i = 0; i < resample_num; i++) {
      const auto opt_objects =
        trimmed_data->buf_objects.get(trimmed_data->timestamp + 1e9 * time_resolution * i);
      if (!opt_objects.has_value()) {
        break;
      }
      objects_history.push_back(opt_objects.value());
    }
  }

  void calculate()
  {
    std::vector<double> lateral_accel_values;
    std::vector<double> minimum_ttc_values;
    std::vector<double> longitudinal_jerk_values;
    std::vector<double> travel_distance_values;

    for (size_t i = 0; i < resample_num - 1; i++) {
      lateral_accel_values.push_back(lateral_accel(i));
      longitudinal_jerk_values.push_back(longitudinal_jerk(i));
      minimum_ttc_values.push_back(minimum_ttc(i));
      travel_distance_values.push_back(travel_distance(i));
    }

    {
      lateral_accel_values.push_back(lateral_accel(resample_num - 1));
      longitudinal_jerk_values.push_back(0.0);
      minimum_ttc_values.push_back(minimum_ttc(resample_num - 1));
      travel_distance_values.push_back(travel_distance(resample_num - 1));
    }

    values.emplace(METRIC::LATERAL_ACCEL, lateral_accel_values);
    values.emplace(METRIC::LONGITUDINAL_JERK, longitudinal_jerk_values);
    values.emplace(METRIC::MINIMUM_TTC, minimum_ttc_values);
    values.emplace(METRIC::TRAVEL_DISTANCE, travel_distance_values);

    scores.emplace(SCORE::LONGITUDINAL_COMFORTABILITY, longitudinal_comfortability());
    scores.emplace(SCORE::LATERAL_COMFORTABILITY, lateral_comfortability());
    scores.emplace(SCORE::EFFICIENCY, efficiency());
    scores.emplace(SCORE::SAFETY, safety());
  }

  double longitudinal_comfortability() const
  {
    constexpr double TIME_FACTOR = 0.8;

    double score = 0.0;

    const auto min = 0.0;
    const auto max = 0.5;
    const auto normalize = [&min, &max](const double value) {
      return (max - std::clamp(value, min, max)) / (max - min);
    };

    for (size_t i = 0; i < resample_num; i++) {
      score +=
        normalize(std::pow(TIME_FACTOR, i) * std::abs(values.at(METRIC::LONGITUDINAL_JERK).at(i)));
    }

    return score / resample_num;
  }

  double lateral_comfortability() const
  {
    constexpr double TIME_FACTOR = 0.8;

    double score = 0.0;

    const auto min = 0.0;
    const auto max = 0.5;
    const auto normalize = [&min, &max](const double value) {
      return (max - std::clamp(value, min, max)) / (max - min);
    };

    for (size_t i = 0; i < resample_num; i++) {
      score +=
        normalize(std::pow(TIME_FACTOR, i) * std::abs(values.at(METRIC::LATERAL_ACCEL).at(i)));
    }

    return score / resample_num;
  }

  double efficiency() const
  {
    constexpr double TIME_FACTOR = 0.8;

    double score = 0.0;

    const auto min = 0.0;
    const auto max = 20.0;
    const auto normalize = [&min, &max](const double value) {
      return std::clamp(value, min, max) / (max - min);
    };

    for (size_t i = 0; i < resample_num; i++) {
      score += normalize(std::pow(TIME_FACTOR, i) * values.at(METRIC::TRAVEL_DISTANCE).at(i) / 0.5);
    }

    return score / resample_num;
  }

  double safety() const
  {
    constexpr double TIME_FACTOR = 0.8;

    double score = 0.0;

    const auto min = 0.0;
    const auto max = 5.0;
    const auto normalize = [&min, &max](const double value) {
      return std::clamp(value, min, max) / (max - min);
    };

    for (size_t i = 0; i < resample_num; i++) {
      score += normalize(std::pow(TIME_FACTOR, i) * values.at(METRIC::MINIMUM_TTC).at(i));
    }

    return score / resample_num;
  }

  double total() const
  {
    constexpr double w0 = 1.0;
    constexpr double w1 = 1.0;
    constexpr double w2 = 1.0;
    constexpr double w3 = 1.0;
    return w0 * scores.at(SCORE::LATERAL_COMFORTABILITY) +
           w1 * scores.at(SCORE::LONGITUDINAL_COMFORTABILITY) + w2 * scores.at(SCORE::EFFICIENCY) +
           w3 * scores.at(SCORE::SAFETY);
  }

  virtual double lateral_accel(const size_t idx) const = 0;

  virtual double longitudinal_jerk(const size_t idx) const = 0;

  virtual double minimum_ttc(const size_t idx) const = 0;

  virtual double travel_distance(const size_t idx) const = 0;

  std::vector<PredictedObjects> objects_history;

  std::unordered_map<METRIC, std::vector<double>> values;
  std::unordered_map<SCORE, double> scores;

  vehicle_info_utils::VehicleInfo vehicle_info;

  size_t resample_num;

  std::string tag{""};
};

struct ManualDrivingData : CommonData
{
  explicit ManualDrivingData(
    const std::shared_ptr<TrimmedData> & trimmed_data,
    const vehicle_info_utils::VehicleInfo & vehicle_info, const size_t resample_num,
    const double time_resolution)
  : CommonData(trimmed_data, vehicle_info, resample_num, time_resolution, "manual")
  {
    for (size_t i = 0; i < resample_num; i++) {
      const auto opt_odometry =
        trimmed_data->buf_odometry.get(trimmed_data->timestamp + 1e9 * time_resolution * i);
      if (!opt_odometry.has_value()) {
        break;
      }
      odometry_history.push_back(opt_odometry.value());

      const auto opt_accel =
        trimmed_data->buf_accel.get(trimmed_data->timestamp + 1e9 * time_resolution * i);
      if (!opt_accel.has_value()) {
        break;
      }
      accel_history.push_back(opt_accel.value());

      const auto opt_steer =
        trimmed_data->buf_steer.get(trimmed_data->timestamp + 1e9 * time_resolution * i);
      if (!opt_steer.has_value()) {
        break;
      }
      steer_history.push_back(opt_steer.value());
    }

    calculate();
  }

  double lateral_accel(const size_t idx) const
  {
    const auto radius =
      vehicle_info.wheel_base_m / std::tan(steer_history.at(idx).steering_tire_angle);
    const auto speed = odometry_history.at(idx).twist.twist.linear.x;
    return speed * speed / radius;
  }

  double longitudinal_jerk(const size_t idx) const
  {
    const double dt = rclcpp::Time(accel_history.at(idx + 1).header.stamp).nanoseconds() -
                      rclcpp::Time(accel_history.at(idx).header.stamp).nanoseconds();

    return 1e9 *
           (accel_history.at(idx + 1).accel.accel.linear.x -
            accel_history.at(idx).accel.accel.linear.x) /
           dt;
  }

  double minimum_ttc(const size_t idx) const
  {
    const auto p_ego = odometry_history.at(idx).pose.pose;
    const auto v_ego = get_velocity_in_world_coordinate(odometry_history.at(idx));

    return time_to_collision(objects_history.at(idx), p_ego, v_ego);
  }

  double travel_distance(const size_t idx) const
  {
    double distance = 0.0;
    for (size_t i = 0L; i < idx; i++) {
      distance += autoware::universe_utils::calcDistance3d(
        odometry_history.at(i + 1).pose.pose, odometry_history.at(i).pose.pose);
    }
    return distance;
  }

  std::vector<Odometry> odometry_history;
  std::vector<AccelWithCovarianceStamped> accel_history;
  std::vector<SteeringReport> steer_history;
};

struct TrajectoryData : CommonData
{
  TrajectoryData(
    const std::shared_ptr<TrimmedData> & trimmed_data,
    const vehicle_info_utils::VehicleInfo & vehicle_info, const size_t resample_num,
    const double time_resolution, const std::string & tag,
    const std::vector<TrajectoryPoint> & points)
  : CommonData(trimmed_data, vehicle_info, resample_num, time_resolution, tag), points{points}
  {
    calculate();
  }

  double lateral_accel(const size_t idx) const
  {
    const auto radius = vehicle_info.wheel_base_m / std::tan(points.at(idx).front_wheel_angle_rad);
    const auto speed = points.at(idx).longitudinal_velocity_mps;
    return speed * speed / radius;
  }

  double longitudinal_jerk(const size_t idx) const
  {
    return (points.at(idx + 1).acceleration_mps2 - points.at(idx).acceleration_mps2) / 0.5;
  }

  double minimum_ttc(const size_t idx) const
  {
    const auto p_ego = points.at(idx).pose;
    const auto v_ego = get_velocity_in_world_coordinate(points.at(idx));

    return time_to_collision(objects_history.at(idx), p_ego, v_ego);
  }

  double travel_distance(const size_t idx) const
  {
    return autoware::motion_utils::calcSignedArcLength(points, 0L, idx);
  }

  std::vector<TrajectoryPoint> points;
};

struct SamplingTrajectoryData
{
  SamplingTrajectoryData(
    const std::shared_ptr<TrimmedData> & trimmed_data,
    const vehicle_info_utils::VehicleInfo & vehicle_info, const double time_horizon,
    const double time_resolution)
  {
    const auto opt_odometry = trimmed_data->buf_odometry.get(trimmed_data->timestamp);
    if (!opt_odometry.has_value()) {
      throw std::logic_error("data is not enough.");
    }
    init_odometory = opt_odometry.value();

    const auto opt_accel = trimmed_data->buf_accel.get(trimmed_data->timestamp);
    if (!opt_accel.has_value()) {
      throw std::logic_error("data is not enough.");
    }
    init_accel = opt_accel.value();

    const auto opt_trajectory = trimmed_data->buf_trajectory.get(trimmed_data->timestamp);
    if (!opt_trajectory.has_value()) {
      throw std::logic_error("data is not enough.");
    }
    data.emplace_back(
      trimmed_data, vehicle_info, time_horizon, time_resolution, "autoware",
      resampling(opt_trajectory.value(), time_horizon, time_resolution));

    for (const auto & sample : sampling(opt_trajectory.value())) {
      data.emplace_back(
        trimmed_data, vehicle_info, time_horizon, time_resolution, "frenet", sample);
    }

    std::sort(data.begin(), data.end(), [](const auto & a, const auto & b) {
      return a.total() > b.total();
    });
  }

  std::vector<TrajectoryPoint> resampling(
    const Trajectory & trajectory, const double time_horizon, const double time_resolution)
  {
    const auto ego_seg_idx =
      autoware::motion_utils::findFirstNearestSegmentIndexWithSoftConstraints(
        trajectory.points, init_odometory.pose.pose, 10.0, M_PI_2);

    std::vector<TrajectoryPoint> output;
    const auto vehicle_pose_frenet =
      convertToFrenetPoint(trajectory.points, init_odometory.pose.pose.position, ego_seg_idx);

    double length = 0.0;
    for (double t = 0.0; t < time_horizon; t += time_resolution) {
      const auto pose = autoware::motion_utils::calcInterpolatedPose(
        trajectory.points, vehicle_pose_frenet.length + length);
      const auto p_trajectory = autoware::motion_utils::calcInterpolatedPoint(trajectory, pose);
      output.push_back(p_trajectory);

      const auto pred_accel = p_trajectory.acceleration_mps2;
      const auto pred_velocity = p_trajectory.longitudinal_velocity_mps;

      length +=
        pred_velocity * time_resolution + 0.5 * pred_accel * time_resolution * time_resolution;
    }

    return output;
  }

  std::vector<std::vector<TrajectoryPoint>> sampling(const Trajectory & trajectory)
  {
    const auto reference_trajectory =
      autoware::path_sampler::preparePathSpline(trajectory.points, true);

    autoware::sampler_common::State current_state;
    current_state.pose = {init_odometory.pose.pose.position.x, init_odometory.pose.pose.position.y};
    current_state.heading = tf2::getYaw(init_odometory.pose.pose.orientation);

    current_state.frenet = reference_trajectory.frenet(current_state.pose);
    // current_state.pose = reference_trajectory.cartesian(current_state.frenet.s);
    current_state.heading = reference_trajectory.yaw(current_state.frenet.s);
    current_state.curvature = reference_trajectory.curvature(current_state.frenet.s);

    const auto trajectory_length = autoware::motion_utils::calcArcLength(trajectory.points);
    const auto sampling_parameters =
      prepareSamplingParameters(current_state, 0.0, reference_trajectory, trajectory_length);

    autoware::frenet_planner::FrenetState initial_frenet_state;
    initial_frenet_state.position = reference_trajectory.frenet(current_state.pose);
    initial_frenet_state.longitudinal_velocity = init_odometory.twist.twist.linear.x;
    initial_frenet_state.longitudinal_acceleration = init_accel.accel.accel.linear.x;
    const auto s = initial_frenet_state.position.s;
    const auto d = initial_frenet_state.position.d;
    // Calculate Velocity and acceleration parametrized over arc length
    // From appendix I of Optimal Trajectory Generation for Dynamic Street Scenarios in a Frenet
    // Frame
    const auto frenet_yaw = current_state.heading - reference_trajectory.yaw(s);
    const auto path_curvature = reference_trajectory.curvature(s);
    const auto delta_s = 0.001;
    initial_frenet_state.lateral_velocity = (1 - path_curvature * d) * std::tan(frenet_yaw);
    const auto path_curvature_deriv =
      (reference_trajectory.curvature(s + delta_s) - path_curvature) / delta_s;
    const auto cos_yaw = std::cos(frenet_yaw);
    if (cos_yaw == 0.0) {
      initial_frenet_state.lateral_acceleration = 0.0;
    } else {
      initial_frenet_state.lateral_acceleration =
        -(path_curvature_deriv * d + path_curvature * initial_frenet_state.lateral_velocity) *
          std::tan(frenet_yaw) +
        ((1 - path_curvature * d) / (cos_yaw * cos_yaw)) *
          (current_state.curvature * ((1 - path_curvature * d) / cos_yaw) - path_curvature);
    }

    const auto sampling_frenet_trajectories = autoware::frenet_planner::generateTrajectories(
      reference_trajectory, initial_frenet_state, sampling_parameters);

    std::vector<std::vector<TrajectoryPoint>> output;
    output.reserve(sampling_frenet_trajectories.size());

    for (const auto & trajectory : sampling_frenet_trajectories) {
      output.push_back(autoware::path_sampler::trajectory_utils::convertToTrajectoryPoints(
        trajectory.resampleTimeFromZero(0.5)));
    }

    return output;
  }

  auto best() const -> TrajectoryData { return data.front(); }

  auto autoware() const -> TrajectoryData
  {
    const auto itr = std::find_if(data.begin(), data.end(), [](const auto & trajectory) {
      return trajectory.tag == "autoware";
    });
    return *itr;
  }

  Odometry init_odometory;
  AccelWithCovarianceStamped init_accel;
  std::vector<TrajectoryData> data;
};

struct DataSet
{
  DataSet(
    const std::shared_ptr<TrimmedData> & trimmed_data,
    const vehicle_info_utils::VehicleInfo & vehicle_info, const size_t resample_num,
    const double time_resolution)
  : manual{ManualDrivingData(trimmed_data, vehicle_info, resample_num, time_resolution)},
    sampling{SamplingTrajectoryData(trimmed_data, vehicle_info, resample_num, time_resolution)}
  {
  }

  ManualDrivingData manual;
  SamplingTrajectoryData sampling;
};

class BehaviorAnalyzerNode : public rclcpp::Node
{
public:
  explicit BehaviorAnalyzerNode(const rclcpp::NodeOptions & node_options);

private:
  void on_timer();

  void play(const SetBool::Request::SharedPtr req, SetBool::Response::SharedPtr res);

  void rewind(const Trigger::Request::SharedPtr req, Trigger::Response::SharedPtr res);

  void update(std::shared_ptr<TrimmedData> & trimmed_data) const;

  void process(const std::shared_ptr<TrimmedData> & trimmed_data) const;

  void metrics(const std::shared_ptr<DataSet> & data_set) const;

  void score(const std::shared_ptr<DataSet> & data_set) const;

  void visualize(const std::shared_ptr<DataSet> & data_set) const;

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<MarkerArray>::SharedPtr pub_marker_;
  rclcpp::Publisher<Odometry>::SharedPtr pub_odometry_;
  rclcpp::Publisher<PredictedObjects>::SharedPtr pub_objects_;
  rclcpp::Publisher<Trajectory>::SharedPtr pub_trajectory_;
  rclcpp::Publisher<TFMessage>::SharedPtr pub_tf_;
  rclcpp::Publisher<Float32MultiArrayStamped>::SharedPtr pub_manual_metrics_;
  rclcpp::Publisher<Float32MultiArrayStamped>::SharedPtr pub_system_metrics_;
  rclcpp::Publisher<Float32MultiArrayStamped>::SharedPtr pub_manual_score_;
  rclcpp::Publisher<Float32MultiArrayStamped>::SharedPtr pub_system_score_;
  rclcpp::Service<SetBool>::SharedPtr srv_play_;
  rclcpp::Service<Trigger>::SharedPtr srv_rewind_;

  vehicle_info_utils::VehicleInfo vehicle_info_;

  std::shared_ptr<TrimmedData> trimmed_data_;

  mutable rosbag2_cpp::Reader reader_;

  bool is_ready_{false};
};
}  // namespace autoware::behavior_analyzer

#endif  // NODE_HPP_
