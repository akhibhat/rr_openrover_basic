#include <ctime>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <iostream>
#include "ros/ros.h"

#include <rr_openrover_basic/odom_control.hpp>

namespace openrover {

OdomControl::OdomControl(bool use_control, double Kp, double Ki, double Kd, int max, int min, std::string log_filename) :
    MOTOR_NEUTRAL_(125),
    MOTOR_MAX_(max),
    MOTOR_MIN_(min),
    MOTOR_DEADBAND_(9),
    MAX_ACCEL_CUTOFF_(20.0),
    MIN_VELOCITY_(0.03),
    MAX_VELOCITY_(3),
    enable_file_logging_(false), //not implemented
    log_filename_(log_filename),
    K_P_(Kp),
    K_I_(Ki),
    K_D_(Kd),
    velocity_history_(3, 0),
    use_control_(use_control),
    skip_measurement_(false),
    at_max_motor_speed_(false),
    at_min_motor_speed_(false)
{
}

OdomControl::OdomControl(bool use_control, double Kp, double Ki, double Kd, int max, int min) :
    MOTOR_NEUTRAL_(125),
    MOTOR_MAX_(max),
    MOTOR_MIN_(min),
    MOTOR_DEADBAND_(9),
    MAX_ACCEL_CUTOFF_(20.0),
    MIN_VELOCITY_(0.03),
    MAX_VELOCITY_(3),
    enable_file_logging_(false), //not implemented
    K_P_(Kp),
    K_I_(Ki),
    K_D_(Kd),
    velocity_history_(3, 0),
    use_control_(use_control),
    skip_measurement_(false),
    at_max_motor_speed_(false),
    at_min_motor_speed_(false),
    stop_integrating_(false),
    error_(0),
    integral_value_(0),
    velocity_commanded_(0),
    velocity_measured_(0),
    velocity_filtered_(0)
{
}

/*OdomControl::OdomControl() :
    MOTOR_NEUTRAL_(125),
    MOTOR_MAX_(250),
    MOTOR_MIN_(0),
    MOTOR_DEADBAND_(9),
    MAX_ACCEL_CUTOFF_(20.0),
    MIN_VELOCITY_(0.03),
    MAX_VELOCITY_(3)
    {}
*/

unsigned char OdomControl::calculate(double commanded_vel, double measured_vel, double dt)
{
    velocity_commanded_ = commanded_vel;
    velocity_measured_ = measured_vel;

    if (commanded_vel == 0)
    {    // If stopping, stop now
        integral_value_ = 0;
        if (hasZeroHistory(velocity_history_))
        {
            velocity_filtered_ = filter(measured_vel, dt);
            return (unsigned char) MOTOR_NEUTRAL_;
        }
    }

    velocity_filtered_ = filter(measured_vel, dt);
    error_ = commanded_vel - velocity_filtered_;
    if (!skip_measurement_) 
    {
        motor_speed_ = PID(error_, dt);
    }

    motor_speed_ = deadbandOffset(motor_speed_, MOTOR_DEADBAND_);
    motor_speed_ = boundMotorSpeed(motor_speed_, MOTOR_MAX_, MOTOR_MIN_);

    return (unsigned char) motor_speed_;
}

void OdomControl::reset()
{
    integral_value_ = 0;
    error_ = 0;
    velocity_commanded_ = 0;
    velocity_measured_ = 0;
    velocity_filtered_ = 0;
    std::fill(velocity_history_.begin(), velocity_history_.end(), 0);
    motor_speed_ = MOTOR_NEUTRAL_;
    skip_measurement_ = false;
}

int OdomControl::PID(double error, double dt)
{
    double p_val = P(error, dt);
    double i_val = I(error, dt);
    double d_val = D(error, dt);
    double pid_val = p_val + i_val + d_val;

    if (fabs(pid_val) > 125)
    {
        stop_integrating_ = true;
    }
    else
    {
        stop_integrating_ = false;
    }
    PID_ = pid_val;
    return (int)round(pid_val + 125.0);
}

double OdomControl::D(double error, double dt)
{
    return K_D_ * (velocity_history_[0]-velocity_history_[1]) / dt;
}

double OdomControl::I(double error, double dt)
{
    if (!stop_integrating_)
    {
        integral_value_ += (K_I_*error)*dt;
    }
    return integral_value_;
}

double OdomControl::P(double error, double dt)
{
    double p_val = error*K_P_;
    return error*K_P_;
}

bool OdomControl::hasZeroHistory(const std::vector<double>& vel_history)
{
    double avg = (fabs(vel_history[0]) + fabs(vel_history[1]) + fabs(vel_history[2]))/3.0;
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
    //Compensate for deadband 
    if (motor_speed > 125) 
    { 
        return (motor_speed + deadband_offset); 
    } 
    else if (motor_speed < 125 ) 
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
        ROS_WARN("Skipping measurement");
    }
    else
    {
        time = dt;
    }

    //Check for impossible acceleration
    float accel = (velocity - velocity_history_[0]) / time;
    ROS_INFO("Acceleration: %f", accel);

    if (false) //fabs(accel) > MAX_ACCEL_CUTOFF_)
    {
        skip_measurement_ = true;
        //throw std::string("Skipping encoder reading");
    }
    else
    {
        skip_measurement_ = false;
        //Hanning filter
        velocity_filtered_ = 0.25 * velocity + 0.5 * velocity_history_[0] + 0.25 * velocity_history_[1];
        velocity_history_.insert(velocity_history_.begin(), velocity_filtered_);
        velocity_history_.pop_back();
    }

    return velocity_filtered_;

    //for billinear transform
/*    static std::vector<float>  right_x_history(3,0);
    static std::vector<float>  left_x_history(3,0);
    left_x_history.insert(left_x_history.begin(), left_vel);
    left_x_history.pop_back();
*/

    //dumb low pass filter
/*    velocity_filtered_ = velocity_measured_ / 2 + velocity_filtered_ / 2;


    //Billinear 2nd Order IIR Butterworth Filter
    //x_history is measured velocitys
/*    float a1 = 0.20657;
    float a2 = 0.41314;
    float a3 = 0.20657;
    float b1 = 0.36953;
    float b2 = -0.19582;

    velocity_filtered_ = a1*left_x_history[0] + a2*left_x_history[1] + a3*left_x_history[2] + 
        b1*velocity_history_[0] + b2*velocity_history_[1];
    velocity_history_.insert(velocity_history_.begin(), velocity_filtered_);
    velocity_history_.pop_back();
    //ROS_INFO("%1.3f ||| %1.3f %1.3f %1.3f", velocity_filtered_, left_vel, velocity_history_[0], velocity_history_[1]);
 */
}

}