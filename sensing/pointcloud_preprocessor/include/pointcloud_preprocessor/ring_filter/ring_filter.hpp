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

#ifndef POINTCLOUD_PREPROCESSOR__RING_FILTER_HPP_
#define POINTCLOUD_PREPROCESSOR__RING_FILTER_HPP_

#include "pointcloud_preprocessor/filter.hpp"
#include "pointcloud_preprocessor/utility/utilities.hpp"
#include <sensor_msgs/point_cloud2_iterator.hpp>

namespace pointcloud_preprocessor
{
class RingFilterComponent : public pointcloud_preprocessor::Filter
{
private:
  void filter(
    const PointCloud2ConstPtr & input, const IndicesPtr & indices, PointCloud2 & output) override;

  // parameter
  uint32_t ring_interval_;
  bool remove_interval_ring_;

public:
  PCL_MAKE_ALIGNED_OPERATOR_NEW
  explicit RingFilterComponent(const rclcpp::NodeOptions & options);
};

}  // namespace pointcloud_preprocessor

#endif  // POINTCLOUD_PREPROCESSOR__RING_FILTER_HPP_
