/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/video_coding/codecs/av1/scalability_structure_l1t2.h"

#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "api/transport/rtp/dependency_descriptor.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace {

constexpr auto kNotPresent = DecodeTargetIndication::kNotPresent;
constexpr auto kDiscardable = DecodeTargetIndication::kDiscardable;
constexpr auto kSwitch = DecodeTargetIndication::kSwitch;

constexpr DecodeTargetIndication kDtis[3][2] = {
    {kSwitch, kSwitch},           // KeyFrame
    {kNotPresent, kDiscardable},  // DeltaFrame T1
    {kSwitch, kSwitch},           // DeltaFrame T0
};

}  // namespace

ScalabilityStructureL1T2::~ScalabilityStructureL1T2() = default;

ScalableVideoController::StreamLayersConfig
ScalabilityStructureL1T2::StreamConfig() const {
  StreamLayersConfig result;
  result.num_spatial_layers = 1;
  result.num_temporal_layers = 2;
  return result;
}

FrameDependencyStructure ScalabilityStructureL1T2::DependencyStructure() const {
  using Builder = GenericFrameInfo::Builder;
  FrameDependencyStructure structure;
  structure.num_decode_targets = 2;
  structure.num_chains = 1;
  structure.decode_target_protected_by_chain = {0, 0};
  structure.templates = {
      Builder().T(0).Dtis("SS").ChainDiffs({0}).Build(),
      Builder().T(0).Dtis("SS").ChainDiffs({2}).Fdiffs({2}).Build(),
      Builder().T(1).Dtis("-D").ChainDiffs({1}).Fdiffs({1}).Build(),
  };
  return structure;
}

std::vector<ScalableVideoController::LayerFrameConfig>
ScalabilityStructureL1T2::NextFrameConfig(bool restart) {
  if (restart) {
    next_pattern_ = kKeyFrame;
  }
  std::vector<LayerFrameConfig> result(1);

  switch (next_pattern_) {
    case kKeyFrame:
      result[0].Id(0).T(0).Keyframe().Update(0);
      next_pattern_ = kDeltaFrameT1;
      break;
    case kDeltaFrameT1:
      result[0].Id(1).T(1).Reference(0);
      next_pattern_ = kDeltaFrameT0;
      break;
    case kDeltaFrameT0:
      result[0].Id(2).T(0).ReferenceAndUpdate(0);
      next_pattern_ = kDeltaFrameT1;
      break;
  }
  return result;
}

absl::optional<GenericFrameInfo> ScalabilityStructureL1T2::OnEncodeDone(
    LayerFrameConfig config) {
  // Encoder may have generated a keyframe even when not asked for it. Treat
  // such frame same as requested keyframe, in particular restart the sequence.
  if (config.IsKeyframe()) {
    config = NextFrameConfig(/*restart=*/true).front();
  }

  absl::optional<GenericFrameInfo> frame_info;
  if (config.Id() < 0 || config.Id() >= int{ABSL_ARRAYSIZE(kDtis)}) {
    RTC_LOG(LS_ERROR) << "Unexpected config id " << config.Id();
    return frame_info;
  }
  frame_info.emplace();
  frame_info->temporal_id = config.TemporalId();
  frame_info->encoder_buffers = config.Buffers();
  frame_info->decode_target_indications.assign(std::begin(kDtis[config.Id()]),
                                               std::end(kDtis[config.Id()]));
  frame_info->part_of_chain = {config.TemporalId() == 0};
  return frame_info;
}

}  // namespace webrtc
