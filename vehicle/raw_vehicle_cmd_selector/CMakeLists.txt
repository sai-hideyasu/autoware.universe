cmake_minimum_required(VERSION 3.8)
project(raw_vehicle_cmd_selector)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(tier4_vehicle_msgs REQUIRED)
find_package(autoware_auto_vehicle_msgs REQUIRED)

add_executable(raw_vehicle_cmd_selector
  src/raw_vehicle_cmd_selector.cpp
)

ament_target_dependencies(raw_vehicle_cmd_selector
  rclcpp
  tier4_vehicle_msgs
  autoware_auto_vehicle_msgs
)

install(TARGETS
  TARGETS raw_vehicle_cmd_selector
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  launch
  DESTINATION share/${PROJECT_NAME}/
)

#install(DIRECTORY
#  config
#  DESTINATION share/${PROJECT_NAME}/
#)

ament_package()
