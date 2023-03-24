/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <boost/iterator/transform_iterator.hpp>
#include <initializer_list>
#include <ostream>

#include <json/json.h>

#include <AbstractDomain.h>
#include <PatriciaTreeMapAbstractPartition.h>

#include <mariana-trench/CalleePortFrames.h>
#include <mariana-trench/FlattenIterator.h>
#include <mariana-trench/Frame.h>
#include <mariana-trench/GroupHashedSetAbstractDomain.h>
#include <mariana-trench/IncludeMacros.h>
#include <mariana-trench/RootPatriciaTreeAbstractPartition.h>
#include <mariana-trench/TaintConfig.h>
#include <mariana-trench/Transforms.h>

namespace marianatrench {

/**
 * Represents a set of frames with the same call position.
 * Based on its position in `Taint`, it is expected that all frames within
 * this class have the same callee and call position.
 */
class CallPositionFrames final
    : public sparta::AbstractDomain<CallPositionFrames> {
 private:
  using FramesByCalleePort = GroupHashedSetAbstractDomain<
      CalleePortFrames,
      CalleePortFrames::GroupHash,
      CalleePortFrames::GroupEqual,
      CalleePortFrames::GroupDifference>;

 private:
  using ConstIterator = FlattenIterator<
      /* OuterIterator */ FramesByCalleePort::iterator,
      /* InnerIterator */ CalleePortFrames::iterator>;

 public:
  // C++ container concept member types
  using iterator = ConstIterator;
  using const_iterator = ConstIterator;
  using value_type = Frame;
  using difference_type = std::ptrdiff_t;
  using size_type = std::size_t;
  using const_reference = const Frame&;
  using const_pointer = const Frame*;

 private:
  explicit CallPositionFrames(
      const Position* MT_NULLABLE position,
      FramesByCalleePort frames)
      : position_(position), frames_(std::move(frames)) {}

 public:
  /* Create the bottom (i.e, empty) frame set. */
  CallPositionFrames()
      : position_(nullptr), frames_(FramesByCalleePort::bottom()) {}

  explicit CallPositionFrames(std::initializer_list<TaintConfig> configs);

  INCLUDE_DEFAULT_COPY_CONSTRUCTORS_AND_ASSIGNMENTS(CallPositionFrames)

  static CallPositionFrames bottom() {
    return CallPositionFrames(
        /* position */ nullptr, FramesByCalleePort::bottom());
  }

  static CallPositionFrames top() {
    return CallPositionFrames(
        /* position */ nullptr, FramesByCalleePort::top());
  }

  bool is_bottom() const override {
    return frames_.is_bottom();
  }

  bool is_top() const override {
    return frames_.is_top();
  }

  void set_to_bottom() override {
    position_ = nullptr;
    frames_.set_to_bottom();
  }

  void set_to_top() override {
    position_ = nullptr;
    frames_.set_to_top();
  }

  bool empty() const {
    return frames_.is_bottom();
  }

  const Position* MT_NULLABLE position() const {
    return position_;
  }

  void add(const TaintConfig& config);

  bool leq(const CallPositionFrames& other) const override;

  bool equals(const CallPositionFrames& other) const override;

  void join_with(const CallPositionFrames& other) override;

  void widen_with(const CallPositionFrames& other) override;

  void meet_with(const CallPositionFrames& other) override;

  void narrow_with(const CallPositionFrames& other) override;

  void difference_with(const CallPositionFrames& other);

  void map(const std::function<void(Frame&)>& f);

  void filter(const std::function<bool(const Frame&)>& predicate);

  ConstIterator begin() const {
    return ConstIterator(frames_.begin(), frames_.end());
  }

  ConstIterator end() const {
    return ConstIterator(frames_.end(), frames_.end());
  }

  void set_origins_if_empty(const MethodSet& origins);

  void set_field_origins_if_empty_with_field_callee(const Field* field);

  FeatureMayAlwaysSet inferred_features() const;

  void add_inferred_features(const FeatureMayAlwaysSet& features);

  LocalPositionSet local_positions() const;

  void add_local_position(const Position* position);

  void set_local_positions(const LocalPositionSet& positions);

  void add_inferred_features_and_local_position(
      const FeatureMayAlwaysSet& features,
      const Position* MT_NULLABLE position);

  /**
   * Propagate the taint from the callee to the caller.
   *
   * Return bottom if the taint should not be propagated.
   */
  CallPositionFrames propagate(
      const Method* callee,
      const AccessPath& callee_port,
      const Position* call_position,
      int maximum_source_sink_distance,
      Context& context,
      const std::vector<const DexType * MT_NULLABLE>& source_register_types,
      const std::vector<std::optional<std::string>>& source_constant_arguments)
      const;

  /* Return the set of leaf frames with the given position. */
  CallPositionFrames attach_position(const Position* position) const;

  void transform_kind_with_features(
      const std::function<std::vector<const Kind*>(const Kind*)>&,
      const std::function<FeatureMayAlwaysSet(const Kind*)>&);

  CallPositionFrames apply_transform(
      const Kinds& kinds,
      const Transforms& transforms,
      const TransformList* local_transforms) const;

  void append_to_artificial_source_input_paths(Path::Element path_element);

  void add_inferred_features_to_real_sources(
      const FeatureMayAlwaysSet& features);

  /**
   * Returns new `CallPositionFrames` containing updated call and local
   * positions computed by the input functions.
   */
  std::unordered_map<const Position*, CallPositionFrames> map_positions(
      const std::function<const Position*(const AccessPath&, const Position*)>&
          new_call_position,
      const std::function<LocalPositionSet(const LocalPositionSet&)>&
          new_local_positions) const;

  void filter_invalid_frames(
      const std::function<
          bool(const Method* MT_NULLABLE, const AccessPath&, const Kind*)>&
          is_valid);

  bool contains_kind(const Kind*) const;

  template <class T>
  std::unordered_map<T, CallPositionFrames> partition_by_kind(
      const std::function<T(const Kind*)>& map_kind) const {
    std::unordered_map<T, CallPositionFrames> result;
    for (const auto& callee_port_frames : frames_) {
      auto partition = callee_port_frames.partition_by_kind(map_kind);

      for (const auto& [mapped_value, partitioned_frames] : partition) {
        auto new_frames = CallPositionFrames(
            position_, FramesByCalleePort{partitioned_frames});

        auto existing = result.find(mapped_value);
        auto existing_or_bottom = existing == result.end()
            ? CallPositionFrames::bottom()
            : existing->second;
        existing_or_bottom.join_with(new_frames);

        result[mapped_value] = existing_or_bottom;
      }
    }
    return result;
  }

  RootPatriciaTreeAbstractPartition<PathTreeDomain> input_paths() const;

  template <class T>
  std::unordered_map<T, std::vector<std::reference_wrapper<const Frame>>>
  partition_map(const std::function<T(const Frame&)>& map) const {
    std::unordered_map<T, std::vector<std::reference_wrapper<const Frame>>>
        result;
    for (const auto& callee_port_frames : frames_) {
      for (const auto& callee_port_frame : callee_port_frames) {
        auto value = map(callee_port_frame);
        result[value].push_back(std::cref(callee_port_frame));
      }
    }

    return result;
  }

  Json::Value to_json(const Method* MT_NULLABLE callee, CallInfo call_info)
      const;

  friend std::ostream& operator<<(
      std::ostream& out,
      const CallPositionFrames& frames);

 private:
  const Position* MT_NULLABLE position_;
  FramesByCalleePort frames_;
};

} // namespace marianatrench
