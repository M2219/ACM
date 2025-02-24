
/*
 * hunter_messenger.cpp
 *
 * Created on: Jun 01, 2020 15:25
 * Description:
 *
 * Test commands:
 * rostopic pub /cmd_vel geometry_msgs/Twist -r 3 -- '[0.5,0.0,0.0]' '[0.0, 0.0,
 * 0.3872]'
 *
 * rostopic pub /reset_odom_integrator std_msgs/Bool true
 *
 * Copyright (c) 2019 Ruixiang Du (rdu)
 */

#include "hunter_base/hunter_messenger.hpp"
#include <tf/transform_broadcaster.h>
#include <cmath>

#include "hunter_base/hunter_messenger.hpp"
#include "hunter_msgs/HunterStatus.h"
#include "hunter_msgs/HunterMotorState.h"
#include "hunter_msgs/HunterBmsStatus.h"

namespace westonrobot {
HunterROSMessenger::HunterROSMessenger(ros::NodeHandle *nh)
    : hunter_(nullptr), nh_(nh) {}

HunterROSMessenger::HunterROSMessenger(HunterRobot *hunter, ros::NodeHandle *nh)
    : hunter_(hunter), nh_(nh) {}

void HunterROSMessenger::SetupSubscription() {
  // odometry publisher
  odom_publisher_ = nh_->advertise<nav_msgs::Odometry>(odom_frame_, 50); // 50Hz
  status_publisher_ =
      nh_->advertise<hunter_msgs::HunterStatus>("/hunter_status", 10);
  BMS_status_publisher_ = nh_->advertise<hunter_msgs::HunterBmsStatus>("/BMS_status", 10);

  // cmd subscriber
  motion_cmd_subscriber_ = nh_->subscribe<geometry_msgs::Twist>(
      "/cmd_vel", 5, &HunterROSMessenger::TwistCmdCallback, this); //5

  integrator_reset_subscriber_ = nh_->subscribe<std_msgs::Bool>(
      "/reset_odom_integrator", 5,
      &HunterROSMessenger::ResetOdomIntegratorCallback, this); //5
}

void HunterROSMessenger::ResetOdomIntegratorCallback(
    const std_msgs::Bool::ConstPtr &msg) {
  if (msg->data) ResetOdometry();
}

void HunterROSMessenger::TwistCmdCallback(
    const geometry_msgs::Twist::ConstPtr &msg) {
  // double steer_cmd = msg->angular.z;
  // if(steer_cmd > max_steer_angle_central)
  //   steer_cmd = max_steer_angle_central;
  // if(steer_cmd < - max_steer_angle_central)
  //     steer_cmd = - max_steer_angle_central;
  // ROS_DEBUG("max_steer_angle_central:%f",max_steer_angle_central);
  // TODO add cmd limits here

  // Set constant velocity and angular velocity
  //double constant_linear_velocity = 3.0;  // Set desired linear velocity (m/s)
  //double constant_angular_velocity = 0.3; // Set desired angular velocity (rad/s)

  if (!simulated_robot_) {
    // double phi_i = ConvertCentralAngleToInner(steer_cmd);
//    double phi_i = steer_cmd;

    double radian = 0;
    double phi_i = AngelVelocity2Angel(*msg, radian);

    ROS_DEBUG("set steering angle:%f",phi_i);
    hunter_->ReleaseBrake();
    hunter_->SetMotionCommand(msg->linear.x,phi_i);
  } else {
    std::lock_guard<std::mutex> guard(twist_mutex_);
    current_twist_ = *msg.get();
  }
  ROS_INFO("cmd received:%f, %f", msg->linear.x, msg->angular.z);
}

void HunterROSMessenger::GetCurrentMotionCmdForSim(double &linear,
                                                   double &angular) {
  std::lock_guard<std::mutex> guard(twist_mutex_);
  linear = current_twist_.linear.x;
  angular = current_twist_.angular.z;
}

double HunterROSMessenger::ConvertInnerAngleToCentral(double angle) {
  double phi = 0;
  double phi_i = angle;
  if (phi_i > steer_angle_tolerance) {
    // left turn
    double r = l / std::tan(phi_i) + w / 2;
    phi = std::atan(l / r);
  } else if (phi_i < -steer_angle_tolerance) {
    // right turn
    double r = l / std::tan(-phi_i) + w / 2;
    phi = std::atan(l / r);
    phi = -phi;
  }
  return phi;
}

double HunterROSMessenger::ConvertCentralAngleToInner(double angle) {
  double phi = angle;
  double phi_i = 0;
  if (phi > steer_angle_tolerance) {
    // left turn
    phi_i = std::atan(2 * l * std::sin(phi) /
                      (2 * l * std::cos(phi) - w * std::sin(phi)));
  } else if (phi < -steer_angle_tolerance) {
    // right turn
    phi = -phi;
    phi_i = std::atan(2 * l * std::sin(phi) /
                      (2 * l * std::cos(phi) - w * std::sin(phi)));
    phi_i = -phi_i;
  }
  return phi_i;
}

void HunterROSMessenger::PublishStateToROS() {
  current_time_ = ros::Time::now();
  double dt = (current_time_ - last_time_).toSec();

  static bool init_run = true;
  if (init_run) {
    last_time_ = current_time_;
    init_run = false;
    return;
  }

  auto robot_state = hunter_->GetRobotState();
  auto actuator_state = hunter_->GetActuatorState();
  auto bms_state = hunter_->GetCommonSensorState();
  // publish hunter state message
  hunter_msgs::HunterStatus status_msg;
  hunter_msgs::HunterBmsStatus bms_msg;

//  double left_vel = -actuator_state.actuator_state[1].rpm / 60.0 * 2 * M_PI /
//          HunterParams::transmission_reduction_rate *
//          HunterParams::wheel_radius;
//  double right_vel = actuator_state.actuator_state[2].rpm / 60.0 * 2 * M_PI /
//          HunterParams::transmission_reduction_rate *
//          HunterParams::wheel_radius;
  status_msg.linear_velocity = robot_state.motion_state.linear_velocity;

  // TODO SHOULD NOT use this correction when new Firmware with right
  //     cmd/feedback of steering angle is updated
  //double corrected_angle = robot_state.motion_state.steering_angle * 26.5 / 40.0;
  double phi = ConvertInnerAngleToCentral(robot_state.motion_state.steering_angle);
  ROS_DEBUG("feedback steering angle:%f",phi);
  std::cout << "feedback steering angle: " << phi << std::endl;
//  double phi = robot_state.motion_state.steering_angle;
  //   double phi = ConvertInnerAngleToCentral(state.steering_angle);
  status_msg.steering_angle = phi;

  status_msg.header.stamp = current_time_;
  status_msg.base_state = robot_state.system_state.vehicle_state;
  status_msg.control_mode = robot_state.system_state.control_mode;
//  status_msg.park_mode = robot_state;
  status_msg.fault_code = robot_state.system_state.error_code;
  status_msg.battery_voltage = robot_state.system_state.battery_voltage;
  if(hunter_->GetParserProtocolVersion() == ProtocolVersion::AGX_V1)
  {
      for (int i = 0; i < 3; ++i)
      {
          status_msg.motor_states[i].current = actuator_state.actuator_state[i].current;
          status_msg.motor_states[i].rpm = actuator_state.actuator_state[i].rpm;
          status_msg.motor_states[i].temperature = actuator_state.actuator_state[i].motor_temp;
          status_msg.driver_states[i].driver_temperature = actuator_state.actuator_state[i].driver_temp;
      }
  }
  else
  {
      for (int i = 0; i < 3; ++i) {
        status_msg.motor_states[i].current = actuator_state.actuator_hs_state[i].current;
        status_msg.motor_states[i].rpm = actuator_state.actuator_hs_state[i].rpm;
        status_msg.motor_states[i].motor_pose = actuator_state.actuator_hs_state[i].pulse_count;
        status_msg.motor_states[i].temperature = actuator_state.actuator_ls_state[i].motor_temp;
        status_msg.driver_states[i].driver_state = actuator_state.actuator_ls_state[i].driver_state;
        status_msg.driver_states[i].driver_voltage = actuator_state.actuator_ls_state[i].driver_voltage;
        status_msg.driver_states[i].driver_temperature = actuator_state.actuator_ls_state[i].driver_temp;
      }
  }

  bms_msg.SOC                   = bms_state.bms_basic_state.battery_soc;
  bms_msg.SOH                   = bms_state.bms_basic_state.battery_soh;
  bms_msg.battery_voltage       = bms_state.bms_basic_state.voltage;
  bms_msg.battery_current       = bms_state.bms_basic_state.current;
  bms_msg.battery_temperature   = bms_state.bms_basic_state.temperature;
  bms_msg.Alarm_Status_1        = bms_state.bms_extend_state.alarm_status_1;
  bms_msg.Alarm_Status_2        = bms_state.bms_extend_state.alarm_status_2;
  bms_msg.Warning_Status_1      = bms_state.bms_extend_state.warn_status_1;
  bms_msg.Warning_Status_1      = bms_state.bms_extend_state.warn_status_2;
  BMS_status_publisher_.publish(bms_msg);
  status_publisher_.publish(status_msg);

  // publish odometry and tf
  PublishOdometryToROS(status_msg.linear_velocity, status_msg.steering_angle, dt);

  // record time for next integration
  last_time_ = current_time_;
}

void HunterROSMessenger::PublishSimStateToROS(double linear, double angular) {
  current_time_ = ros::Time::now();
  double dt = 1.0 / sim_control_rate_;

  // publish hunter state message
  hunter_msgs::HunterStatus status_msg;

  status_msg.header.stamp = current_time_;

  // TODO should receive update from simulator
  status_msg.linear_velocity = linear;
  status_msg.steering_angle = angular;

  status_msg.base_state = 0x00;
  status_msg.control_mode = 0x01;
  status_msg.fault_code = 0x00;
  status_msg.battery_voltage = 29.5;

  for (int i = 0; i < 3; ++i) {
    status_msg.motor_states[i].current = 0;
    status_msg.motor_states[i].rpm = 0;
    status_msg.motor_states[i].temperature = 0;
  }

  status_publisher_.publish(status_msg);

  // publish odometry and tf
  PublishOdometryToROS(linear, angular, dt);
}

void HunterROSMessenger::ResetOdometry() {
  position_x_ = 0.0;
  position_y_ = 0.0;
  theta_ = 0.0;
}

void HunterROSMessenger::PublishOdometryToROS(double linear, double angular,
                                              double dt) {

  current_time_ = ros::Time::now();

  // perform numerical integration to get an estimation of pose
  linear_speed_ = linear;
  steering_angle_ = angular;

  // propagate model model
  //std::cout << "-----------------" << theta_ << std::endl;
  asc::state_t state =
      model_.Propagate({position_x_, position_y_, theta_},
                       {linear_speed_, steering_angle_}, 0, dt, dt/100); // dt / 100

  position_x_ = state[0];
  position_y_ = state[1];
  theta_ = state[2];

  geometry_msgs::Quaternion odom_quat = tf::createQuaternionMsgFromYaw(theta_);

  // publish tf transformation
  if (publish_tf_) {
    geometry_msgs::TransformStamped tf_msg;
    tf_msg.header.stamp = current_time_;
    tf_msg.header.frame_id = odom_frame_;
    tf_msg.child_frame_id = base_frame_;

    tf_msg.transform.translation.x = position_x_;
    tf_msg.transform.translation.y = position_y_;
    tf_msg.transform.translation.z = 0.0;
    tf_msg.transform.rotation = odom_quat;

    tf_broadcaster_.sendTransform(tf_msg);
  }

  // publish odometry and tf messages
  nav_msgs::Odometry odom_msg;
  odom_msg.header.stamp = current_time_;
  odom_msg.header.frame_id = odom_frame_;
  odom_msg.child_frame_id = base_frame_;

  odom_msg.pose.pose.position.x = position_x_;
  odom_msg.pose.pose.position.y = position_y_;
  odom_msg.pose.pose.position.z = 0.0;
  odom_msg.pose.pose.orientation = odom_quat;

  odom_msg.twist.twist.linear.x = linear_speed_;
  odom_msg.twist.twist.linear.y = 0.0;
  odom_msg.twist.twist.angular.z = linear_speed_ / l * std::tan(steering_angle_);

  odom_msg.pose.covariance = {
    0.001,      0.0,        0.0,        0.0,        0.0,        0.0,
    0.0,        0.001,      0.0,        0.0,        0.0,        0.0,
    0.0,        0.0,        1000000.0,  0.0,        0.0,        0.0,
    0.0,        0.0,        0.0,        1000000.0,  0.0,        0.0,
    0.0,        0.0,        0.0,        0.0,        1000000.0,  0.0,
    0.0,        0.0,        0.0,        0.0,        0.0,        1000.0};
  odom_msg.twist.covariance = {
    0.001,      0.0,        0.0,        0.0,        0.0,        0.0,
    0.0,        0.001,      0.0,        0.0,        0.0,        0.0,
    0.0,        0.0,        1000000.0,  0.0,        0.0,        0.0,
    0.0,        0.0,        0.0,        1000000.0,  0.0,        0.0,
    0.0,        0.0,        0.0,        0.0,        1000000.0,  0.0,
    0.0,        0.0,        0.0,        0.0,        0.0,        1000.0};

//   std::cerr << "linear: " << linear_speed_ << " , angular: " << steering_angle_
//             << " , pose: (" << position_x_ << "," << position_y_ << ","
//             << theta_ << ")" << std::endl;

  odom_publisher_.publish(odom_msg);
}

double HunterROSMessenger::AngelVelocity2Angel(geometry_msgs::Twist msg,double &radius)
{
  double linear = fabs(msg.linear.x);
  double angular = fabs(msg.angular.z);
  if(angular == 0)
  {
    return 0.0;
  }

  radius = linear / angular;

  int k = msg.angular.z / fabs(msg.angular.z);
  if ((radius-l)<0 )
  {
    return  k*max_steer_angle;
  }

  double phi_i;
  phi_i = atan(l/(radius-w/2));
  if(msg.linear.x<0)
    phi_i *= -1.0;
  return k*phi_i;
}
}  // namespace westonrobot
