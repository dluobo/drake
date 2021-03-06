#include "drake/automotive/maliput_railcar.h"

#include <cmath>
#include <memory>

#include <Eigen/Dense>
#include "gtest/gtest.h"

#include "drake/automotive/maliput/api/road_geometry.h"
#include "drake/automotive/maliput/dragway/road_geometry.h"
#include "drake/automotive/maliput/monolane/builder.h"
#include "drake/common/drake_path.h"
#include "drake/common/eigen_matrix_compare.h"
#include "drake/math/roll_pitch_yaw_not_using_quaternion.h"
#include "drake/systems/framework/leaf_context.h"

namespace drake {

using maliput::monolane::ArcOffset;
using maliput::monolane::Endpoint;
using maliput::monolane::EndpointXy;
using maliput::monolane::EndpointZ;

using systems::BasicVector;
using systems::LeafContext;
using systems::Parameters;
using systems::rendering::PoseVector;

namespace automotive {
namespace {

class MaliputRailcarTest : public ::testing::Test {
 protected:
  void InitializeDragwayLane() {
    // Defines the dragway's parameters.
    const int kNumLanes{1};
    const double kDragwayLength{50};
    const double kDragwayLaneWidth{0.5};
    const double kDragwayShoulderWidth{0.25};
    Initialize(std::make_unique<const maliput::dragway::RoadGeometry>(
        maliput::api::RoadGeometryId({"RailcarTestDragway"}), kNumLanes,
        kDragwayLength, kDragwayLaneWidth, kDragwayShoulderWidth));
  }

  void InitializeCurvedMonoLane() {
    maliput::monolane::Builder builder(
        maliput::api::RBounds(-2, 2),   /* lane_bounds       */
        maliput::api::RBounds(-4, 4),   /* driveable_bounds  */
        0.01,                           /* linear tolerance  */
        0.5 * M_PI / 180.0);            /* angular_tolerance */
    builder.Connect(
        "point.0",                                             /* id    */
        Endpoint(EndpointXy(0, 0, 0), EndpointZ(0, 0, 0, 0)),  /* start */
        ArcOffset(kCurvedRoadRadius, kCurvedRoadTheta),        /* arc   */
        EndpointZ(0, 0, 0, 0));                                /* z_end */
    Initialize(
        builder.Build(maliput::api::RoadGeometryId({"RailcarTestCurvedRoad"})));
  }

  void Initialize(std::unique_ptr<const maliput::api::RoadGeometry> road) {
    road_ = std::move(road);
    const maliput::api::Lane* lane = road_->junction(0)->segment(0)->lane(0);
    dut_.reset(new MaliputRailcar<double>(*lane));
    context_ = dut_->CreateDefaultContext();
    output_ = dut_->AllocateOutput(*context_);
    derivatives_ = dut_->AllocateTimeDerivatives();
  }

  void SetInputValue(double desired_acceleration) {
    DRAKE_DEMAND(dut_ != nullptr);
    DRAKE_DEMAND(context_ != nullptr);
    context_->FixInputPort(dut_->command_input().get_index(),
        BasicVector<double>::Make(desired_acceleration));
  }

  MaliputRailcarState<double>* continuous_state() {
    auto result = dynamic_cast<MaliputRailcarState<double>*>(
        context_->get_mutable_continuous_state_vector());
    if (result == nullptr) { throw std::bad_cast(); }
    return result;
  }

  const MaliputRailcarState<double>* state_output() const {
    auto state = dynamic_cast<const MaliputRailcarState<double>*>(
        output_->get_vector_data(dut_->state_output().get_index()));
    DRAKE_DEMAND(state != nullptr);
    return state;
  }

  const PoseVector<double>* pose_output() const {
    auto pose = dynamic_cast<const PoseVector<double>*>(
        output_->get_vector_data(dut_->pose_output().get_index()));
    DRAKE_DEMAND(pose != nullptr);
    return pose;
  }

  // Sets the configuration parameters of the railcar.
  void SetConfig(const MaliputRailcarConfig<double>& config) {
    LeafContext<double>* leaf_context =
        dynamic_cast<LeafContext<double>*>(context_.get());
    ASSERT_NE(leaf_context, nullptr);
    Parameters<double>* parameters = leaf_context->get_mutable_parameters();
    ASSERT_NE(parameters, nullptr);
    BasicVector<double>* vector_param =
        parameters->get_mutable_numeric_parameter(0);
    ASSERT_NE(vector_param, nullptr);
    MaliputRailcarConfig<double>* railcar_config =
        dynamic_cast<MaliputRailcarConfig<double>*>(vector_param);
    ASSERT_NE(railcar_config, nullptr);
    railcar_config->SetFrom(config);
  }

  // The arc radius and theta of the road when it is created using
  // InitializeCurvedMonoLane().
  const double kCurvedRoadRadius{10};
  const double kCurvedRoadTheta{M_PI_2};

  std::unique_ptr<const maliput::api::RoadGeometry> road_;
  std::unique_ptr<MaliputRailcar<double>> dut_;  //< The device under test.
  std::unique_ptr<systems::Context<double>> context_;
  std::unique_ptr<systems::SystemOutput<double>> output_;
  std::unique_ptr<systems::ContinuousState<double>> derivatives_;
};

TEST_F(MaliputRailcarTest, Topology) {
  EXPECT_NO_FATAL_FAILURE(InitializeDragwayLane());
  ASSERT_EQ(dut_->get_num_input_ports(), 1);

  ASSERT_EQ(dut_->get_num_output_ports(), 2);
  const auto& state_output = dut_->state_output();
  EXPECT_EQ(systems::kVectorValued, state_output.get_data_type());
  EXPECT_EQ(MaliputRailcarStateIndices::kNumCoordinates, state_output.size());

  const auto& pose_output = dut_->pose_output();
  EXPECT_EQ(systems::kVectorValued, pose_output.get_data_type());
  EXPECT_EQ(PoseVector<double>::kSize, pose_output.size());

  EXPECT_FALSE(dut_->HasAnyDirectFeedthrough());
}

TEST_F(MaliputRailcarTest, ZeroInitialOutput) {
  EXPECT_NO_FATAL_FAILURE(InitializeDragwayLane());
  dut_->CalcOutput(*context_, output_.get());
  auto state = state_output();
  EXPECT_EQ(state->s(), MaliputRailcar<double>::kDefaultInitialS);
  EXPECT_EQ(state->speed(), MaliputRailcar<double>::kDefaultInitialSpeed);
  auto pose = pose_output();
  EXPECT_TRUE(CompareMatrices(pose->get_isometry().matrix(),
                              Eigen::Isometry3d::Identity().matrix()));
  Eigen::Translation<double, 3> translation = pose->get_translation();
  EXPECT_EQ(translation.x(), 0);
  EXPECT_EQ(translation.y(), 0);
  EXPECT_EQ(translation.z(), 0);
}

TEST_F(MaliputRailcarTest, StateAppearsInOutputDragway) {
  EXPECT_NO_FATAL_FAILURE(InitializeDragwayLane());

  continuous_state()->set_s(1.0);
  continuous_state()->set_speed(2.0);
  dut_->CalcOutput(*context_, output_.get());

  auto state = state_output();
  EXPECT_EQ(state->s(), 1);
  EXPECT_EQ(state->speed(), 2);

  auto pose = pose_output();
  Eigen::Isometry3d expected_pose = Eigen::Isometry3d::Identity();
  // For the dragway, the `(s, r, h)` axes in lane-space coorespond to the
  // `(x, y, z)` axes in geo-space. In this case, `s = 1` while `r` and `h` are
  // by default zero.
  expected_pose.translation() = Eigen::Vector3d(1, 0, 0);
  EXPECT_TRUE(CompareMatrices(pose->get_isometry().matrix(),
                              expected_pose.matrix()));
}

TEST_F(MaliputRailcarTest, StateAppearsInOutputMonolane) {
  EXPECT_NO_FATAL_FAILURE(InitializeCurvedMonoLane());
  const maliput::api::Lane* lane = road_->junction(0)->segment(0)->lane(0);

  continuous_state()->set_s(lane->length());
  continuous_state()->set_speed(3.5);
  dut_->CalcOutput(*context_, output_.get());

  ASSERT_NE(lane, nullptr);
  auto state = state_output();
  EXPECT_EQ(state->s(), lane->length());
  EXPECT_EQ(state->speed(), 3.5);

  auto pose = pose_output();
  Eigen::Isometry3d expected_pose = Eigen::Isometry3d::Identity();
  {
    const Eigen::Vector3d rpy(0, 0, kCurvedRoadTheta);
    const Eigen::Vector3d xyz(kCurvedRoadRadius, kCurvedRoadRadius, 0);
    expected_pose.matrix() << drake::math::rpy2rotmat(rpy), xyz, 0, 0, 0, 1;
  }
  // The following tolerance was determined emperically.
  EXPECT_TRUE(CompareMatrices(pose->get_isometry().matrix(),
                              expected_pose.matrix(), 1e-15 /* tolerance */));
}

TEST_F(MaliputRailcarTest, NonZeroParametersAppearInOutputDragway) {
  EXPECT_NO_FATAL_FAILURE(InitializeDragwayLane());
  const double kR{1.5};
  const double kH{8.2};

  // Sets the parameters to be non-zero values.
  MaliputRailcarConfig<double> config;
  config.set_r(kR);
  config.set_h(kH);
  config.set_initial_speed(1);
  config.set_max_speed(30);
  config.set_velocity_limit_kp(8);
  SetConfig(config);
  dut_->CalcOutput(*context_, output_.get());
  auto pose = pose_output();
  Eigen::Isometry3d expected_pose = Eigen::Isometry3d::Identity();
  expected_pose.translation() = Eigen::Vector3d(0, kR, kH);
  EXPECT_TRUE(CompareMatrices(pose->get_isometry().matrix(),
                              expected_pose.matrix()));
}

TEST_F(MaliputRailcarTest, NonZeroParametersAppearInOutputMonolane) {
  EXPECT_NO_FATAL_FAILURE(InitializeCurvedMonoLane());
  const double kR{1.5};
  const double kH{8.2};

  // Sets the parameters to be non-zero values.
  MaliputRailcarConfig<double> config;
  config.set_r(kR);
  config.set_h(kH);
  config.set_initial_speed(1);
  config.set_max_speed(30);
  config.set_velocity_limit_kp(8);
  SetConfig(config);
  dut_->CalcOutput(*context_, output_.get());
  auto start_pose = pose_output();
  Eigen::Isometry3d expected_start_pose = Eigen::Isometry3d::Identity();
  {
    const Eigen::Vector3d rpy(0, 0, 0);
    const Eigen::Vector3d xyz(0, kR, kH);
    expected_start_pose.matrix()
        << drake::math::rpy2rotmat(rpy), xyz, 0, 0, 0, 1;
  }
  // The following tolerance was determined emperically.
  EXPECT_TRUE(CompareMatrices(start_pose->get_isometry().matrix(),
                              expected_start_pose.matrix(),
                              1e-15 /* tolerance */));

  // Moves the vehicle to be at the end of the curved lane.
  const maliput::api::Lane* lane = road_->junction(0)->segment(0)->lane(0);
  continuous_state()->set_s(lane->length());

  dut_->CalcOutput(*context_, output_.get());
  auto end_pose = pose_output();
  Eigen::Isometry3d expected_end_pose = Eigen::Isometry3d::Identity();
  {
    const Eigen::Vector3d rpy(0, 0, M_PI_2);
    const Eigen::Vector3d xyz(kCurvedRoadRadius - kR, kCurvedRoadRadius, kH);
    expected_end_pose.matrix() << drake::math::rpy2rotmat(rpy), xyz, 0, 0, 0, 1;
  }
  // The following tolerance was determined emperically.
  EXPECT_TRUE(CompareMatrices(end_pose->get_isometry().matrix(),
                              expected_end_pose.matrix(),
                              1e-15 /* tolerance */));
}

TEST_F(MaliputRailcarTest, DerivativesDragway) {
  EXPECT_NO_FATAL_FAILURE(InitializeDragwayLane());
  // Grabs a pointer to where the EvalTimeDerivatives results end up.
  const MaliputRailcarState<double>* const result =
      dynamic_cast<const MaliputRailcarState<double>*>(
          derivatives_->get_mutable_vector());
  ASSERT_NE(nullptr, result);

  // Sets the input command.
  SetInputValue(0 /* desired_acceleration */);

  // Checks the derivatives given the default continous state with r = 0.
  dut_->CalcTimeDerivatives(*context_, derivatives_.get());
  EXPECT_DOUBLE_EQ(result->s(), MaliputRailcar<double>::kDefaultInitialSpeed);
  EXPECT_DOUBLE_EQ(result->speed(), 0.0);  // Expect zero acceleration.

  // Checks that the derivatives are zero given zero throttle and brake commands
  // and a non-default continuous state with r = 0.
  continuous_state()->set_s(3.5);
  dut_->CalcTimeDerivatives(*context_, derivatives_.get());
  EXPECT_DOUBLE_EQ(result->s(), MaliputRailcar<double>::kDefaultInitialSpeed);
  EXPECT_DOUBLE_EQ(result->speed(), 0.0);

  // Checks that the derivatives remain zero given a non-default continuous
  // state with r != 0.
  const double kS{1.5};
  const double kSlowSpeed{2};
  MaliputRailcarConfig<double> config;
  config.set_r(-2);
  config.set_h(0);
  config.set_initial_speed(kSlowSpeed);
  config.set_max_speed(30);
  config.set_velocity_limit_kp(8);
  SetConfig(config);
  continuous_state()->set_s(kS);
  continuous_state()->set_speed(kSlowSpeed);
  dut_->CalcTimeDerivatives(*context_, derivatives_.get());
  EXPECT_DOUBLE_EQ(result->s(), kSlowSpeed);
  EXPECT_DOUBLE_EQ(result->speed(), 0.0);

  // Checks that the maximum throttle command of 1 results in the maximum
  // acceleration.
  SetInputValue(5  /* desired acceleration */);
  dut_->CalcTimeDerivatives(*context_, derivatives_.get());
  EXPECT_DOUBLE_EQ(result->s(), kSlowSpeed);
  EXPECT_DOUBLE_EQ(result->speed(), 5);

  // Checks that an acceleration command of -1 when the current speed is > 0
  // results in an acceleration of -1.
  const double kFastSpeed{27};
  SetInputValue(-1  /* desired acceleration */);
  continuous_state()->set_speed(kFastSpeed);
  dut_->CalcTimeDerivatives(*context_, derivatives_.get());
  EXPECT_DOUBLE_EQ(result->s(), kFastSpeed);
  EXPECT_DOUBLE_EQ(result->speed(), -1);
}

TEST_F(MaliputRailcarTest, DerivativesMonolane) {
  EXPECT_NO_FATAL_FAILURE(InitializeCurvedMonoLane());
  // Grabs a pointer to where the EvalTimeDerivatives results end up.
  const MaliputRailcarState<double>* const result =
      dynamic_cast<const MaliputRailcarState<double>*>(
          derivatives_->get_mutable_vector());
  ASSERT_NE(nullptr, result);

  // Sets the input command.
  SetInputValue(0  /* desired acceleration */);

  const double kInitialSpeed{1};
  const double kR{1};

  // Checks the derivatives given a non-default continuous state with r != 0.
  continuous_state()->set_s(1.5);
  MaliputRailcarConfig<double> config;
  config.set_r(kR);
  config.set_h(0);
  config.set_initial_speed(kInitialSpeed);
  config.set_max_speed(30);
  config.set_velocity_limit_kp(8);
  SetConfig(config);
  dut_->CalcTimeDerivatives(*context_, derivatives_.get());
  EXPECT_DOUBLE_EQ(result->s(),
      kInitialSpeed * kCurvedRoadRadius / (kCurvedRoadRadius - kR));
  EXPECT_DOUBLE_EQ(result->speed(), 0);
}

// Tests that connecting the input port is optional, i.e., that it can contain
// a nullptr value.
TEST_F(MaliputRailcarTest, InputPortNotConnected) {
  EXPECT_NO_FATAL_FAILURE(InitializeDragwayLane());
  // Grabs a pointer to where the EvalTimeDerivatives results end up.
  const MaliputRailcarState<double>* const result =
      dynamic_cast<const MaliputRailcarState<double>*>(
          derivatives_->get_mutable_vector());
  ASSERT_NE(nullptr, result);

  ASSERT_NO_FATAL_FAILURE(
      dut_->CalcTimeDerivatives(*context_, derivatives_.get()));
  EXPECT_DOUBLE_EQ(result->s(), MaliputRailcar<double>::kDefaultInitialSpeed);
  EXPECT_DOUBLE_EQ(result->speed(), 0.0);

  // Checks that the derivatives contain the speed set by the configuration and
  // zero acceleration.
  const double kS{2.25};
  const double kInitialSpeed{2};
  MaliputRailcarConfig<double> config;
  config.set_r(-2);
  config.set_h(0);
  config.set_initial_speed(kInitialSpeed);
  config.set_max_speed(30);
  config.set_velocity_limit_kp(8);
  SetConfig(config);
  continuous_state()->set_s(kS);
  continuous_state()->set_speed(kInitialSpeed);
  dut_->CalcTimeDerivatives(*context_, derivatives_.get());
  EXPECT_DOUBLE_EQ(result->s(), kInitialSpeed);
  EXPECT_DOUBLE_EQ(result->speed(), 0.0);
}

}  // namespace
}  // namespace automotive
}  // namespace drake
