// Copyright (c) 2017 Franka Emika GmbH
// Use of this source code is governed by the Apache-2.0 license, see LICENSE
#include <array>
#include <cmath>
#include <functional>
#include <iostream>

#include <franka/duration.h>
#include <franka/exception.h>
#include <franka/model.h>
#include <franka/robot.h>

#include <eigen3/Eigen/Dense>

/**
 * @example force_control.cpp
 * A simple PI force controller that renders in the Z axis the gravitational force corresponding
 * to a desired mass.
 *
 * @warning: make sure that no endeffector is mounted and that the robot's last joint is in contact
 * with a horizontal rigid surface before starting.
 */

int main(int argc, char** argv) {
  // Check whether the required arguments were passed
  if (argc != 3) {
    std::cerr << "Usage: ./" << argv[0] << " <robot-hostname>  <desired-mass>" << std::endl;
    return -1;
  }
  std::cout << "Make sure sure that no endeffector is mounted and that the robot's last joint is "
               "in contact with a horizontal rigid surface before starting."
            << std::endl
            << "Press Enter to continue..." << std::endl;
  std::cin.get();

  // parameters
  const double target_mass{std::stod(argv[2])};
  double desired_mass{0.0};
  constexpr double k_p{5.0};            // NOLINT (readability-identifier-naming)
  constexpr double k_i{10.0};           // NOLINT (readability-identifier-naming)
  constexpr double filter_gain{0.001};  // NOLINT (readability-identifier-naming)

  try {
    // connect to robot
    franka::Robot robot(argv[1]);
    // load the kinematics and dynamics model
    franka::Model model = robot.loadModel();

    // set collision behavior
    robot.setCollisionBehavior({{100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0}},
                               {{100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0}},
                               {{100.0, 100.0, 100.0, 100.0, 100.0, 100.0}},
                               {{100.0, 100.0, 100.0, 100.0, 100.0, 100.0}});

    franka::RobotState initial_state = robot.readOnce();

    Eigen::VectorXd initial_tau_ext(7), tau_error_integral(7);
    // Bias torque sensor
    std::array<double, 7> gravity_array = model.gravity(initial_state, 0.0, {{0.0, 0.0, 0.0}});
    Eigen::Map<Eigen::Matrix<double, 7, 1> > initial_tau_measured(initial_state.tau_J.data());
    Eigen::Map<Eigen::Matrix<double, 7, 1> > initial_gravity(gravity_array.data());
    initial_tau_ext = initial_tau_measured - initial_gravity;

    // init integrator
    tau_error_integral.setZero();

    // define callback for the torque control loop
    std::function<franka::Torques(const franka::RobotState&, franka::Duration)>
        force_control_callback =
            [&](const franka::RobotState& robot_state, franka::Duration period) -> franka::Torques {
      // get state variables
      std::array<double, 42> jacobian_array =
          model.zeroJacobian(franka::Frame::kEndEffector, robot_state);

      Eigen::Map<const Eigen::Matrix<double, 6, 7> > jacobian(jacobian_array.data());
      Eigen::Map<const Eigen::Matrix<double, 7, 1> > tau_measured(robot_state.tau_J.data());
      Eigen::Map<const Eigen::Matrix<double, 7, 1> > gravity(gravity_array.data());

      Eigen::VectorXd tau_d(7), desired_force_torque(6), tau_cmd(7), tau_ext(7);
      desired_force_torque.setZero();
      desired_force_torque(2) = desired_mass * -9.81;
      tau_ext << tau_measured - gravity - initial_tau_ext;
      tau_d << jacobian.transpose() * desired_force_torque;
      tau_error_integral << tau_error_integral + period.toSec() * (tau_d - tau_ext);
      // FF + PI control
      tau_cmd << tau_d + k_p * (tau_d - tau_ext) + k_i * tau_error_integral;

      // Smoothly update the mass to reach the desired target value
      desired_mass = filter_gain * target_mass + (1 - filter_gain) * desired_mass;

      std::array<double, 7> tau_d_array{};
      Eigen::VectorXd::Map(&tau_d_array[0], 7) = tau_cmd;
      return tau_d_array;
    };

    // start real-time control loop
    robot.control(force_control_callback);

  } catch (const franka::Exception& ex) {
    // print exception
    std::cout << ex.what() << std::endl;
  }
  return 0;
}