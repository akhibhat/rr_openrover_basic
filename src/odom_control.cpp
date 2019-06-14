#include <ctime>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <iostream>
#include "ros/ros.h"

#include <rr_openrover_basic/odom_control.hpp>
#include <rr_openrover_basic/constants.hpp>

namespace openrover
{
void OdomControl::start(bool use_control, PidGains pid_gains, int max, int min)
{
  K_P_ = pid_gains.Kp;
  K_I_ = pid_gains.Ki;
  K_D_ = pid_gains.Kd;
}

OdomControl::OdomControl()
  : MOTOR_MAX_(MOTOR_SPEED_MAX)
  , MOTOR_MIN_(MOTOR_SPEED_MIN)
  , MOTOR_DEADBAND_(9)
  , MAX_ACCEL_CUTOFF_(20.0)
  , MIN_VELOCITY_(0.03)
  , MAX_VELOCITY_(5)
  , K_P_(0)
  , K_I_(0)
  , K_D_(0)
  , velocity_history_(3, 0)
  , use_control_(false)
  , skip_measurement_(false)
  , at_max_motor_speed_(false)
  , at_min_motor_speed_(false)
{
}

OdomControl::OdomControl(bool use_control, PidGains pid_gains, int max, int min, std::string log_filename)
  : MOTOR_MAX_(max)
  , MOTOR_MIN_(min)
  , MOTOR_DEADBAND_(9)
  , MAX_ACCEL_CUTOFF_(20.0)
  , MIN_VELOCITY_(0.03)
  , MAX_VELOCITY_(3)
  , enable_file_logging_(false)  // not implemented
  , log_filename_(log_filename)
  , K_P_(pid_gains.Kp)
  , K_I_(pid_gains.Ki)
  , K_D_(pid_gains.Kd)
  , velocity_history_(3, 0)
  , use_control_(use_control)
  , skip_measurement_(false)
  , at_max_motor_speed_(false)
  , at_min_motor_speed_(false)
{
}

OdomControl::OdomControl(bool use_control, PidGains pid_gains, int max, int min)
  : MOTOR_MAX_(max)
  , MOTOR_MIN_(min)
  , MOTOR_DEADBAND_(9)
  , MAX_ACCEL_CUTOFF_(20.0)
  , MIN_VELOCITY_(0.03)
  , MAX_VELOCITY_(3)
  , enable_file_logging_(false)  // not implemented
  , K_P_(pid_gains.Kp)
  , K_I_(pid_gains.Ki)
  , K_D_(pid_gains.Kd)
  , velocity_history_(3, 0)
  , use_control_(use_control)
  , skip_measurement_(false)
  , at_max_motor_speed_(false)
  , at_min_motor_speed_(false)
  , stop_integrating_(false)
  , error_value_(0)
  , integral_value_(0)
  , velocity_commanded_(0)
  , velocity_measured_(0)
  , velocity_filtered_(0)
{
  ROS_INFO("odom Kp: %f", K_P_);
  ROS_INFO("odom Ki: %f", K_I_);
}

unsigned char OdomControl::run(bool e_stop_on, bool control_on, double commanded_vel, double measured_vel, double dt)
{
  velocity_commanded_ = commanded_vel;
  velocity_measured_ = measured_vel;
  velocity_filtered_ = filter(measured_vel, dt);

  if (e_stop_on)
  {
    //    ROS_INFO("Odom E-Stop");
    reset();
    return MOTOR_NEUTRAL;
  }

      if (commanded_vel == 0)
  {  // If stopping, stop now
    // ROS_INFO("Commanded vel == 0");
    integral_value_ = 0;
    if (hasZeroHistory(velocity_history_))
    {
      return (unsigned char)MOTOR_NEUTRAL;
    }
  }

  if (control_on)
  {
    // ROS_INFO("Control on");
    error_value_ = commanded_vel - velocity_filtered_;
    if (!skip_measurement_)
    {
      motor_speed_ = PID(error_value_, dt);
      // ROS_INFO("PID: %i", motor_speed_);
    }
  }
  else
  {
    // ROS_INFO("Control off");
    motor_speed_ = feedThroughControl();
  }

  // motor_speed_ = deadbandOffset(motor_speed_, MOTOR_DEADBAND_);
  motor_speed_ = boundMotorSpeed(motor_speed_, MOTOR_MAX_, MOTOR_MIN_);

  return (unsigned char)motor_speed_;
}

int OdomControl::feedThroughControl()
{
  return (int)round(velocity_commanded_ * 50 + MOTOR_NEUTRAL);
}

void OdomControl::reset()
{
  integral_value_ = 0;
  error_value_ = 0;
  velocity_commanded_ = 0;
  velocity_measured_ = 0;
  velocity_filtered_ = 0;
  std::fill(velocity_history_.begin(), velocity_history_.end(), 0);
  motor_speed_ = MOTOR_NEUTRAL;
  skip_measurement_ = false;
}

int OdomControl::PID(double error, double dt)
{
  double p_val = P(error, dt);
  double i_val = I(error, dt);
  double d_val = D(error, dt);
  double pid_val = p_val + i_val + d_val;
  // ROS_INFO("\nerror: %lf\n dt: %lf", error, dt);
  // ROS_INFO("\n kp: %lf \n ki: %lf \n kd: %lf \n", p_val, i_val, d_val);

  if (fabs(pid_val) > 125)
  {
    stop_integrating_ = true;
  }
  else
  {
    stop_integrating_ = false;
  }

  return (int)round(pid_val + 125.0);
}

double OdomControl::D(double error, double dt)
{
  return K_D_ * (velocity_history_[0] - velocity_history_[1]) / dt;
}

double OdomControl::I(double error, double dt)
{
  if (!stop_integrating_)
  {
    integral_value_ += (K_I_ * error) * dt;
  }
  return integral_value_;
}

double OdomControl::P(double error, double dt)
{
  double p_val = error * K_P_;
  return error * K_P_;
}

bool OdomControl::hasZeroHistory(const std::vector<double>& vel_history)
{
  double avg = (fabs(vel_history[0]) + fabs(vel_history[1]) + fabs(vel_history[2])) / 3.0;
  if (avg < 0.03)
    return true;
  else
    return false;
}

int OdomControl::boundMotorSpeed(int motor_speed, int max, int min)
{
  at_max_motor_speed_ = false;
  at_min_motor_speed_ = false;

  if (motor_speed > max)
  {
    motor_speed = max;
    at_max_motor_speed_ = true;
  }
  if (motor_speed < min)
  {
    motor_speed = min;
    at_min_motor_speed_ = true;
  }

  return motor_speed;
}

int OdomControl::deadbandOffset(int motor_speed, int deadband_offset)
{
  // Compensate for deadband
  if (motor_speed > MOTOR_NEUTRAL)
  {
    return (motor_speed + deadband_offset);
  }
  else if (motor_speed < MOTOR_NEUTRAL)
  {
    return (motor_speed - deadband_offset);
  }
}

double OdomControl::filter(double velocity, double dt)
{
  static double time = 0;

  if (skip_measurement_)
  {
    time += dt;
  }
  else
  {
    time = dt;
  }

  // Check for impossible acceleration, if it is impossible, ignore the measurement.
  float accel = (velocity - velocity_history_[0]) / time;

  if (fabs(accel) > MAX_ACCEL_CUTOFF_)
  {
    skip_measurement_ = true;
  }
  else
  {
    skip_measurement_ = false;
    // Hanning low pass filter filter
    velocity_filtered_ = 0.25 * velocity + 0.5 * velocity_history_[0] + 0.25 * velocity_history_[1];
    velocity_history_.insert(velocity_history_.begin(), velocity_filtered_);
    velocity_history_.pop_back();
  }
  return velocity_filtered_;
}

}  // namespace openrover
