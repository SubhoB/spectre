// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Framework/TestingFramework.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "ControlSystem/UpdateFunctionOfTime.hpp"
#include "DataStructures/DataBox/DataBox.hpp"
#include "DataStructures/DataBox/Tag.hpp"
#include "DataStructures/DataVector.hpp"
#include "DataStructures/Tensor/Tensor.hpp"
#include "DataStructures/Variables.hpp"  // IWYU pragma: keep
#include "Domain/Creators/Brick.hpp"
#include "Domain/Creators/RegisterDerivedWithCharm.hpp"
#include "Domain/Creators/Sphere.hpp"
#include "Domain/Creators/Tags/Domain.hpp"
#include "Domain/Creators/Tags/FunctionsOfTime.hpp"
#include "Domain/Creators/TimeDependence/RegisterDerivedWithCharm.hpp"
#include "Domain/Domain.hpp"
#include "Domain/FunctionsOfTime/RegisterDerivedWithCharm.hpp"
#include "Domain/FunctionsOfTime/Tags.hpp"
#include "Framework/ActionTesting.hpp"
#include "Parallel/Phase.hpp"
#include "Parallel/PhaseDependentActionList.hpp"  // IWYU pragma: keep
#include "ParallelAlgorithms/Interpolation/Actions/InitializeInterpolationTarget.hpp"
#include "ParallelAlgorithms/Interpolation/Actions/InitializeInterpolator.hpp"  // IWYU pragma: keep
#include "ParallelAlgorithms/Interpolation/Actions/InterpolationTargetReceiveVars.hpp"
#include "ParallelAlgorithms/Interpolation/InterpolatedVars.hpp"  // IWYU pragma: keep
#include "ParallelAlgorithms/Interpolation/Protocols/ComputeTargetPoints.hpp"
#include "ParallelAlgorithms/Interpolation/Protocols/InterpolationTargetTag.hpp"
#include "ParallelAlgorithms/Interpolation/Protocols/PostInterpolationCallback.hpp"
#include "PointwiseFunctions/GeneralRelativity/Tags.hpp"
#include "Time/Tags/Time.hpp"
#include "Utilities/ConstantExpressions.hpp"
#include "Utilities/Gsl.hpp"
#include "Utilities/MakeWithValue.hpp"
#include "Utilities/ProtocolHelpers.hpp"
#include "Utilities/Rational.hpp"
#include "Utilities/Requires.hpp"
#include "Utilities/TMPL.hpp"

// IWYU pragma: no_include <boost/variant/get.hpp>

// IWYU pragma: no_forward_declare ActionTesting::InitializeDataBox
// IWYU pragma: no_forward_declare Tensor
// IWYU pragma: no_forward_declare Variables
namespace intrp::Actions {
template <typename InterpolationTargetTag>
struct CleanUpInterpolator;
}  // namespace intrp::Actions
namespace Parallel {
template <typename Metavariables>
class GlobalCache;
}  // namespace Parallel
namespace db {
template <typename TagsList>
class DataBox;
}  // namespace db
namespace intrp::Tags {
template <typename Metavariables>
struct IndicesOfFilledInterpPoints;
struct NumberOfElements;
template <typename TemporalId>
struct PendingTemporalIds;
template <typename TemporalId>
struct TemporalIds;
template <typename TemporalId>
struct CompletedTemporalIds;
}  // namespace intrp::Tags

namespace {

// In the test, we don't care what SendPointsToInterpolator actually does;
// we care only that SendPointsToInterpolator is called with the
// correct arguments.
template <typename InterpolationTargetTag>
struct MockSendPointsToInterpolator {
  template <typename ParallelComponent, typename DbTags, typename Metavariables,
            typename ArrayIndex,
            Requires<tmpl::list_contains_v<
                DbTags, intrp::Tags::TemporalIds<
                            typename Metavariables::InterpolationTargetA::
                                temporal_id::type>>> = nullptr>
  static void apply(
      db::DataBox<DbTags>& box, Parallel::GlobalCache<Metavariables>& /*cache*/,
      const ArrayIndex& /*array_index*/,
      const typename Metavariables::InterpolationTargetA::temporal_id::type&
          temporal_id) {
    using temporal_id_type =
        typename Metavariables::InterpolationTargetA::temporal_id::type;
    CHECK(temporal_id == 14.0 / 16.0);
    // Increment IndicesOfFilledInterpPoints so we can check later
    // whether this function was called.  This isn't the usual usage
    // of IndicesOfFilledInterpPoints; this is done only for the test.
    db::mutate<intrp::Tags::IndicesOfFilledInterpPoints<temporal_id_type>>(
        [&temporal_id](
            const gsl::not_null<std::unordered_map<temporal_id_type,
                                                   std::unordered_set<size_t>>*>
                indices) {
          (*indices)[temporal_id].insert((*indices)[temporal_id].size() + 1);
        },
        make_not_null(&box));
  }
};

template <typename Metavariables, typename InterpolationTargetTag>
struct mock_interpolation_target {
  using metavariables = Metavariables;
  using chare_type = ActionTesting::MockArrayChare;
  using array_index = size_t;
  using component_being_mocked =
      intrp::InterpolationTarget<Metavariables, InterpolationTargetTag>;
  using const_global_cache_tags =
      tmpl::list<domain::Tags::Domain<Metavariables::volume_dim>>;
  using mutable_global_cache_tags =
      tmpl::conditional_t<metavariables::use_time_dependent_maps,
                          tmpl::list<domain::Tags::FunctionsOfTimeInitialize>,
                          tmpl::list<>>;
  using simple_tags = typename intrp::Actions::InitializeInterpolationTarget<
      Metavariables, InterpolationTargetTag>::simple_tags;
  using phase_dependent_action_list = tmpl::list<
      Parallel::PhaseActions<Parallel::Phase::Initialization,
                             tmpl::list<ActionTesting::InitializeDataBox<
                                 simple_tags, typename InterpolationTargetTag::
                                                  compute_items_on_target>>>,
      Parallel::PhaseActions<Parallel::Phase::Testing, tmpl::list<>>>;
  using replace_these_simple_actions = tmpl::list<
      intrp::Actions::SendPointsToInterpolator<InterpolationTargetTag>>;
  using with_these_simple_actions =
      tmpl::list<MockSendPointsToInterpolator<InterpolationTargetTag>>;
};

template <typename InterpolationTargetTag>
struct MockCleanUpInterpolator {
  template <typename ParallelComponent, typename DbTags, typename Metavariables,
            typename ArrayIndex,
            Requires<tmpl::list_contains_v<
                DbTags, typename intrp::Tags::NumberOfElements>> = nullptr>
  static void apply(
      db::DataBox<DbTags>& box,
      const Parallel::GlobalCache<Metavariables>& /*cache*/,
      const ArrayIndex& /*array_index*/,
      const typename Metavariables::InterpolationTargetA::temporal_id::type&
          temporal_id) {
    CHECK(temporal_id == 13.0 / 16.0);
    // Put something in NumberOfElements so we can check later whether
    // this function was called.  This isn't the usual usage of
    // NumberOfElements.
    db::mutate<intrp::Tags::NumberOfElements>(
        [](const gsl::not_null<size_t*> number_of_elements) {
          ++(*number_of_elements);
        },
        make_not_null(&box));
  }
};

// In the test, MockComputeTargetPoints is used only for the
// type aliases; normally the points function of compute_target_points is used
// but since it isn't in this test, it just returns an empty tensor
struct MockComputeTargetPoints
    : tt::ConformsTo<intrp::protocols::ComputeTargetPoints> {
  using is_sequential = std::true_type;
  using frame = ::Frame::Inertial;
  template <typename Metavariables, typename DbTags, typename TemporalId>
  static tnsr::I<DataVector, 3, Frame::Inertial> points(
      const db::DataBox<DbTags>& /*box*/,
      const tmpl::type_<Metavariables>& /*meta*/,
      const TemporalId& /*temporal_id*/) {
    return tnsr::I<DataVector, 3, Frame::Inertial>{};
  }
};

// Simple DataBoxItems to test.
namespace Tags {
struct Square : db::SimpleTag {
  using type = Scalar<DataVector>;
};
struct SquareCompute : Square, db::ComputeTag {
  static void function(gsl::not_null<Scalar<DataVector>*> result,
                       const Scalar<DataVector>& x) {
    get(*result) = square(get(x));
  }
  using argument_tags = tmpl::list<gr::Tags::Lapse<DataVector>>;
  using base = Square;
  using return_type = Scalar<DataVector>;
};
}  // namespace Tags

template <typename DbTags, typename TemporalId>
void callback_impl(const db::DataBox<DbTags>& box,
                   const TemporalId& temporal_id) {
  CHECK(temporal_id == 13.0 / 16.0);
  // The result should be the square of the first 10 integers, in
  // a Scalar<DataVector>.
  const Scalar<DataVector> expected{
      DataVector{{0.0, 1.0, 4.0, 9.0, 16.0, 25.0, 36.0, 49.0, 64.0, 81.0}}};
  CHECK_ITERABLE_APPROX(expected, db::get<Tags::Square>(box));
}

struct MockPostInterpolationCallback
    : tt::ConformsTo<intrp::protocols::PostInterpolationCallback> {
  template <typename DbTags, typename Metavariables>
  static void apply(
      const db::DataBox<DbTags>& box,
      const Parallel::GlobalCache<Metavariables>& /*cache*/,
      const typename Metavariables::InterpolationTargetA::temporal_id::type&
          temporal_id) {
    callback_impl(box, temporal_id);
  }
};

// The sole purpose of this class is to check
// the overload of the callback, and to prevent
// cleanup.  The only time that one would actually want
// to prevent cleanup is in the AH finder, and it is tested there,
//  but this is a more direct test.
struct MockPostInterpolationCallbackNoCleanup
    : tt::ConformsTo<intrp::protocols::PostInterpolationCallback> {
  template <typename DbTags, typename Metavariables>
  static bool apply(
      const gsl::not_null<db::DataBox<DbTags>*> box,
      const gsl::not_null<Parallel::GlobalCache<Metavariables>*> /*cache*/,
      const typename Metavariables::InterpolationTargetA::temporal_id::type&
          temporal_id) {
    callback_impl(*box, temporal_id);
    return false;
  }
};

template <size_t NumberOfInvalidInterpolationPoints>
struct MockPostInterpolationCallbackWithInvalidPoints
    : tt::ConformsTo<intrp::protocols::PostInterpolationCallback> {
  static constexpr double fill_invalid_points_with = 15.0;
  template <typename DbTags, typename Metavariables>
  static void apply(
      const db::DataBox<DbTags>& box,
      const Parallel::GlobalCache<Metavariables>& /*cache*/,
      const typename Metavariables::InterpolationTargetA::temporal_id::type&
          temporal_id) {
    CHECK(temporal_id == 13.0 / 16.0);

    // The result should be the square of the first 10 integers, in
    // a Scalar<DataVector>, followed by several 225s.
    DataVector expected_dv(NumberOfInvalidInterpolationPoints + 10);
    for (size_t i = 0; i < 10; ++i) {
      expected_dv[i] = double(i * i);
    }
    for (size_t i = 10; i < 10 + NumberOfInvalidInterpolationPoints; ++i) {
      expected_dv[i] = 225.0;
    }
    const Scalar<DataVector> expected{expected_dv};
    CHECK_ITERABLE_APPROX(expected, db::get<Tags::Square>(box));
  }
};

template <typename Metavariables>
struct mock_interpolator {
  using metavariables = Metavariables;
  using chare_type = ActionTesting::MockArrayChare;
  using array_index = size_t;
  using phase_dependent_action_list = tmpl::list<
      Parallel::PhaseActions<
          Parallel::Phase::Initialization,
          tmpl::list<intrp::Actions::InitializeInterpolator<
              intrp::Tags::VolumeVarsInfo<Metavariables, ::Tags::Time>,
              intrp::Tags::InterpolatedVarsHolders<Metavariables>>>>,
      Parallel::PhaseActions<Parallel::Phase::Testing, tmpl::list<>>>;

  using component_being_mocked = intrp::Interpolator<Metavariables>;
  using replace_these_simple_actions =
      tmpl::list<intrp::Actions::CleanUpInterpolator<
          typename Metavariables::InterpolationTargetA>>;
  using with_these_simple_actions = tmpl::list<
      MockCleanUpInterpolator<typename Metavariables::InterpolationTargetA>>;
};

template <typename MockCallBackType, typename IsTimeDependent>
struct MockMetavariables {
  struct InterpolationTargetA
      : tt::ConformsTo<intrp::protocols::InterpolationTargetTag> {
    using temporal_id = ::Tags::Time;
    using vars_to_interpolate_to_target =
        tmpl::list<gr::Tags::Lapse<DataVector>>;
    using compute_target_points = MockComputeTargetPoints;
    using post_interpolation_callback = MockCallBackType;
    using compute_items_on_target = tmpl::list<Tags::SquareCompute>;
  };
  using interpolator_source_vars = tmpl::list<gr::Tags::Lapse<DataVector>>;
  using interpolation_target_tags = tmpl::list<InterpolationTargetA>;
  static constexpr size_t volume_dim = 3;
  static constexpr bool use_time_dependent_maps = IsTimeDependent::value;

  using component_list = tmpl::list<
      mock_interpolation_target<MockMetavariables, InterpolationTargetA>,
      mock_interpolator<MockMetavariables>>;
};

template <typename MockCallbackType, typename IsTimeDependent,
          size_t NumberOfExpectedCleanUpActions,
          size_t NumberOfInvalidPointsToAdd>
void test_interpolation_target_receive_vars() {
  using metavars = MockMetavariables<MockCallbackType, IsTimeDependent>;
  using temporal_id_type =
      typename metavars::InterpolationTargetA::temporal_id::type;
  using interp_component = mock_interpolator<metavars>;
  using target_component =
      mock_interpolation_target<metavars,
                                typename metavars::InterpolationTargetA>;

  const size_t num_points = 10;
  const double first_time = 13.0 / 16.0;
  // Add maybe_unused for when IsTimeDependent == false
  // This name must match the hard coded one in UniformTranslation
  [[maybe_unused]] const std::string f_of_t_name = "Translation";
  [[maybe_unused]] std::unordered_map<std::string, double>
      initial_expiration_times{};
  initial_expiration_times[f_of_t_name] = 13.5 / 16.0;
  const double second_time = 14.0 / 16.0;
  [[maybe_unused]] const double new_expiration_time = 14.5 / 16.0;

  std::deque<temporal_id_type> current_temporal_ids{};
  std::deque<temporal_id_type> pending_temporal_ids{};
  std::unique_ptr<ActionTesting::MockRuntimeSystem<metavars>> runner_ptr{};
  if constexpr (IsTimeDependent::value) {
    current_temporal_ids.push_back(first_time);
    pending_temporal_ids.push_back(second_time);
    const auto domain_creator = domain::creators::Brick(
        {{-1.2, 3.0, 2.5}}, {{0.8, 5.0, 3.0}}, {{1, 1, 1}}, {{5, 4, 3}},
        {{false, false, false}},
        std::make_unique<
            domain::creators::time_dependence::UniformTranslation<3>>(
            0.0, std::array<double, 3>({{0.1, 0.2, 0.3}})));
    runner_ptr = std::make_unique<ActionTesting::MockRuntimeSystem<metavars>>(
        domain_creator.create_domain(),
        domain_creator.functions_of_time(initial_expiration_times));
  } else {
    current_temporal_ids.insert(current_temporal_ids.end(),
                                {first_time, second_time});
    const auto domain_creator = domain::creators::Sphere(
        0.9, 4.9, domain::creators::Sphere::Excision{}, 1_st, 5_st, false);
    runner_ptr = std::make_unique<ActionTesting::MockRuntimeSystem<metavars>>(
        domain_creator.create_domain());
  }
  auto& runner = *runner_ptr;

  // Add indices of invalid points (if there are any) at the end.
  std::unordered_map<temporal_id_type, std::unordered_set<size_t>>
      invalid_indices{};
  for (size_t index = num_points;
       index < num_points + NumberOfInvalidPointsToAdd; ++index) {
    invalid_indices[first_time].insert(index);
  }

  // Type alias for better readability below.
  using vars_type = Variables<
      typename metavars::InterpolationTargetA::vars_to_interpolate_to_target>;

  ActionTesting::emplace_component<interp_component>(&runner, 0);
  for (size_t i = 0; i < 2; ++i) {
    ActionTesting::next_action<interp_component>(make_not_null(&runner), 0);
  }
  ActionTesting::emplace_component_and_initialize<target_component>(
      &runner, 0,
      {std::unordered_map<temporal_id_type, std::unordered_set<size_t>>{},
       std::unordered_map<temporal_id_type, std::unordered_set<size_t>>{
           invalid_indices},
       pending_temporal_ids, current_temporal_ids,
       std::deque<temporal_id_type>{},
       std::unordered_map<temporal_id_type,
                          Variables<typename metavars::InterpolationTargetA::
                                        vars_to_interpolate_to_target>>{
           {first_time, vars_type{num_points + NumberOfInvalidPointsToAdd}}},
       // Default-constructed Variables cause problems, so below
       // we construct the Variables with a single point.
       vars_type{1}});
  ActionTesting::set_phase(make_not_null(&runner), Parallel::Phase::Testing);

  // Now set up the vars.
  std::vector<typename ::Variables<
      typename metavars::InterpolationTargetA::vars_to_interpolate_to_target>>
      vars_src;
  std::vector<std::vector<size_t>> global_offsets;

  // Adds more data to vars_src and global_offsets.
  auto add_to_vars_src = [&vars_src, &global_offsets](
                             const std::vector<double>& lapse_vals,
                             const std::vector<size_t>& offset_vals) {
    vars_src.emplace_back(
        ::Variables<typename metavars::InterpolationTargetA::
                        vars_to_interpolate_to_target>{lapse_vals.size()});
    global_offsets.emplace_back(offset_vals);
    auto& lapse = get<gr::Tags::Lapse<DataVector>>(vars_src.back());
    for (size_t i = 0; i < lapse_vals.size(); ++i) {
      get<>(lapse)[i] = lapse_vals[i];
    }
  };

  add_to_vars_src({{3.0, 6.0}}, {{3, 6}});
  add_to_vars_src({{2.0, 7.0}}, {{2, 7}});

  ActionTesting::simple_action<target_component,
                               intrp::Actions::InterpolationTargetReceiveVars<
                                   typename metavars::InterpolationTargetA>>(
      make_not_null(&runner), 0, vars_src, global_offsets, first_time);

  // It should have interpolated 4 points by now.
  CHECK(
      ActionTesting::get_databox_tag<
          target_component,
          intrp::Tags::IndicesOfFilledInterpPoints<temporal_id_type>>(runner, 0)
          .at(first_time)
          .size() == 4);

  // Should be no queued simple action until we get num_points points.
  CHECK(
      ActionTesting::is_simple_action_queue_empty<target_component>(runner, 0));
  CHECK(
      ActionTesting::is_simple_action_queue_empty<interp_component>(runner, 0));

  // And the number of temporal_ids is the same as it used to be.
  CHECK(ActionTesting::get_databox_tag<
            target_component, intrp::Tags::TemporalIds<temporal_id_type>>(
            runner, 0)
            .size() == current_temporal_ids.size());

  vars_src.clear();
  global_offsets.clear();
  add_to_vars_src({{1.0, 888888.0}},
                  {{1, 6}});  // 6 is repeated, point will be ignored.
  add_to_vars_src({{8.0, 0.0, 4.0}}, {{8, 0, 4}});

  ActionTesting::simple_action<target_component,
                               intrp::Actions::InterpolationTargetReceiveVars<
                                   typename metavars::InterpolationTargetA>>(
      make_not_null(&runner), 0, vars_src, global_offsets, first_time);

  // It should have interpolated 8 points by now. (The ninth point had
  // a repeated global_offsets so it should be ignored)
  CHECK(
      ActionTesting::get_databox_tag<
          target_component,
          intrp::Tags::IndicesOfFilledInterpPoints<temporal_id_type>>(runner, 0)
          .at(first_time)
          .size() == 8);

  // Should be no queued simple action until we have added 10 points.
  CHECK(
      ActionTesting::is_simple_action_queue_empty<target_component>(runner, 0));
  CHECK(
      ActionTesting::is_simple_action_queue_empty<mock_interpolator<metavars>>(
          runner, 0));

  vars_src.clear();
  global_offsets.clear();
  add_to_vars_src({{9.0, 5.0}}, {{9, 5}});

  // This will call InterpolationTargetA::post_interpolation_callback
  // where we check that the points are correct.
  ActionTesting::simple_action<target_component,
                               intrp::Actions::InterpolationTargetReceiveVars<
                                   typename metavars::InterpolationTargetA>>(
      make_not_null(&runner), 0, vars_src, global_offsets, first_time);

  if (NumberOfExpectedCleanUpActions == 0) {
    // We called the function without cleanup, as a test, so there should
    // be no queued simple actions (tested below outside the if-else).

    // It should have interpolated all the points by now.
    // But those points should have not been cleaned up.
    CHECK(ActionTesting::get_databox_tag<
              target_component,
              intrp::Tags::IndicesOfFilledInterpPoints<temporal_id_type>>(
              runner, 0)
              .at(first_time)
              .size() == num_points);

    // Check that MockCleanUpInterpolator was NOT called.  If called, it resets
    // the (fake) number of elements, specifically so we can test it here.
    CHECK(ActionTesting::get_databox_tag<interp_component,
                                         intrp::Tags::NumberOfElements>(
              runner, 0) == 0);

    // Also, there should still be the same number of TemporalIds left
    // because we did not clean them up.
    CHECK(ActionTesting::get_databox_tag<
              target_component, intrp::Tags::TemporalIds<temporal_id_type>>(
              runner, 0)
              .size() == current_temporal_ids.size());

    // And there should be 0 CompletedTemporalIds because we did not
    // clean up TemporalIds.
    CHECK(ActionTesting::get_databox_tag<
              target_component,
              intrp::Tags::CompletedTemporalIds<temporal_id_type>>(runner, 0)
              .empty());

  } else {
    // This is the (usual) case where we want a cleanup.

    // It should have interpolated all the points by now,
    // and the list of points should have been cleaned up.
    CHECK(ActionTesting::get_databox_tag<
              target_component,
              intrp::Tags::IndicesOfFilledInterpPoints<temporal_id_type>>(
              runner, 0)
              .count(first_time) == 0);

    // There should be a queued simple action on the target_component,
    // which is either SendPointsToInterpolator or
    // VerifyTemporalIdsAndSendPoints (depending on whether we are
    // time-dependent)
    CHECK(ActionTesting::number_of_queued_simple_actions<target_component>(
              runner, 0) == 1);

    // There should be a queued simple action on the
    // interp_component, which is CleanUpInterpolator, which here we
    // mock.
    CHECK(ActionTesting::number_of_queued_simple_actions<interp_component>(
              runner, 0) == 1);
    ActionTesting::invoke_queued_simple_action<interp_component>(
        make_not_null(&runner), 0);

    // Check that MockCleanUpInterpolator was called.  It resets the
    // (fake) number of elements, specifically so we can test it here.
    CHECK(ActionTesting::get_databox_tag<interp_component,
                                         intrp::Tags::NumberOfElements>(
              runner, 0) == 1);

    // And there should be 1 CompletedTemporalId, and its value
    // should be the value of the initial TemporalId.
    CHECK(ActionTesting::get_databox_tag<
              target_component,
              intrp::Tags::CompletedTemporalIds<temporal_id_type>>(runner, 0)
              .size() == 1);
    CHECK(ActionTesting::get_databox_tag<
              target_component,
              intrp::Tags::CompletedTemporalIds<temporal_id_type>>(runner, 0)
              .front() == first_time);

    if constexpr (IsTimeDependent::value) {
      // There should zero TemporalIds left, but one PendingTemporalId.
      // The PendingTemporalId's value should be second_temporal_id.
      CHECK(ActionTesting::get_databox_tag<
                target_component, intrp::Tags::TemporalIds<temporal_id_type>>(
                runner, 0)
                .empty());
      CHECK(ActionTesting::get_databox_tag<
                target_component,
                intrp::Tags::PendingTemporalIds<temporal_id_type>>(runner, 0)
                .size() == 1);
      CHECK(ActionTesting::get_databox_tag<
                target_component,
                intrp::Tags::PendingTemporalIds<temporal_id_type>>(runner, 0)
                .front() == second_time);

      // Invoke the remaining simple action, VerifyTemporalIdsAndSendPoints.
      ActionTesting::invoke_queued_simple_action<target_component>(
          make_not_null(&runner), 0);

      // Now there should be no more simple actions, because the
      // FunctionsOfTime are not up to date for the pending
      // temporal_id.
      CHECK(ActionTesting::is_simple_action_queue_empty<target_component>(
          runner, 0));

      // Now mutate the FunctionsOfTime.
      auto& cache = ActionTesting::cache<target_component>(runner, 0_st);
      const double current_expiration_time =
          initial_expiration_times[f_of_t_name];
      Parallel::mutate<domain::Tags::FunctionsOfTime,
                       control_system::UpdateSingleFunctionOfTime>(
          cache, f_of_t_name, current_expiration_time, DataVector{3, 0.0},
          new_expiration_time);

      // The callback should have queued a single simple action,
      // VerifyTemporalIdsAndSendPoints.
      CHECK(ActionTesting::number_of_queued_simple_actions<target_component>(
                runner, 0) == 1);

      // So invoke that simple_action.
      ActionTesting::invoke_queued_simple_action<target_component>(
          make_not_null(&runner), 0);

      // Now there should be a single simple_action which is
      // MockSendPointsToInterpolator.
      CHECK(ActionTesting::number_of_queued_simple_actions<target_component>(
                runner, 0) == 1);

      // And PendingTemporalIds should be empty.
      CHECK(ActionTesting::get_databox_tag<
                target_component,
                intrp::Tags::PendingTemporalIds<temporal_id_type>>(runner, 0)
                .empty());
    }

    // There should be only 1 TemporalId left.
    // And its value should be second_temporal_id.
    CHECK(ActionTesting::get_databox_tag<
              target_component, intrp::Tags::TemporalIds<temporal_id_type>>(
              runner, 0)
              .size() == 1);
    CHECK(ActionTesting::get_databox_tag<
              target_component, intrp::Tags::TemporalIds<temporal_id_type>>(
              runner, 0)
              .front() == second_time);

    // Check that MockSendPointsToInterpolator was not yet called.
    // MockSendPointsToInterpolator sets a (fake) value of
    // IndicesOfFilledInterpPoints for the express purpose of this
    // check.
    const auto& indices_to_check = ActionTesting::get_databox_tag<
        target_component,
        intrp::Tags::IndicesOfFilledInterpPoints<temporal_id_type>>(runner, 0);
    CHECK(indices_to_check.find(second_time) == indices_to_check.end());

    // And there is yet one more simple action, SendPointsToInterpolator,
    // which here we mock just to check that it is called.
    ActionTesting::invoke_queued_simple_action<target_component>(
        make_not_null(&runner), 0);

    // Check that MockSendPointsToInterpolator was called.
    // MockSendPointsToInterpolator sets a (fake) value of
    // IndicesOfFilledInterpPoints for the express purpose of this check.
    CHECK(ActionTesting::get_databox_tag<
              target_component,
              intrp::Tags::IndicesOfFilledInterpPoints<temporal_id_type>>(
              runner, 0)
              .at(second_time)
              .size() == 1);
  }

  // There should be no more queued actions; verify this.
  CHECK(
      ActionTesting::is_simple_action_queue_empty<target_component>(runner, 0));
  CHECK(
      ActionTesting::is_simple_action_queue_empty<interp_component>(runner, 0));
}

SPECTRE_TEST_CASE("Unit.NumericalAlgorithms.InterpolationTarget.ReceiveVars",
                  "[Unit]") {
  domain::creators::register_derived_with_charm();
  domain::creators::time_dependence::register_derived_with_charm();
  domain::FunctionsOfTime::register_derived_with_charm();
  test_interpolation_target_receive_vars<MockPostInterpolationCallback,
                                         std::false_type, 1, 0>();
  test_interpolation_target_receive_vars<MockPostInterpolationCallbackNoCleanup,
                                         std::false_type, 0, 0>();
  test_interpolation_target_receive_vars<
      MockPostInterpolationCallbackWithInvalidPoints<3>, std::false_type, 1,
      3>();
  test_interpolation_target_receive_vars<MockPostInterpolationCallback,
                                         std::true_type, 1, 0>();
  test_interpolation_target_receive_vars<MockPostInterpolationCallbackNoCleanup,
                                         std::true_type, 0, 0>();
  test_interpolation_target_receive_vars<
      MockPostInterpolationCallbackWithInvalidPoints<3>, std::true_type, 1,
      3>();
}
}  // namespace
