/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video_codecs/sdp_video_format.h"

#include <optional>
#include <string>

#include "absl/container/inlined_vector.h"
#include "absl/strings/match.h"
#include "api/array_view.h"
#include "api/rtp_parameters.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/av1_profile.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/scalability_mode.h"
#ifdef RTC_ENABLE_H265
#include "api/video_codecs/h265_profile_tier_level.h"
#endif
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/vp9_profile.h"
#include "media/base/media_constants.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"

namespace webrtc {

namespace {

// TODO(bugs.webrtc.org/15847): remove code duplication of IsSameCodecSpecific
// in media/base/codec.cc
std::string GetFmtpParameterOrDefault(const CodecParameterMap& params,
                                      const std::string& name,
                                      const std::string& default_value) {
  const auto it = params.find(name);
  if (it != params.end()) {
    return it->second;
  }
  return default_value;
}

std::string H264GetPacketizationModeOrDefault(const CodecParameterMap& params) {
  // If packetization-mode is not present, default to "0".
  // https://tools.ietf.org/html/rfc6184#section-6.2
  return GetFmtpParameterOrDefault(params, kH264FmtpPacketizationMode, "0");
}

bool H264IsSamePacketizationMode(const CodecParameterMap& left,
                                 const CodecParameterMap& right) {
  return H264GetPacketizationModeOrDefault(left) ==
         H264GetPacketizationModeOrDefault(right);
}

std::string AV1GetTierOrDefault(const CodecParameterMap& params) {
  // If the parameter is not present, the tier MUST be inferred to be 0.
  // https://aomediacodec.github.io/av1-rtp-spec/#72-sdp-parameters
  return GetFmtpParameterOrDefault(params, kAv1FmtpTier, "0");
}

bool AV1IsSameTier(const CodecParameterMap& left,
                   const CodecParameterMap& right) {
  return AV1GetTierOrDefault(left) == AV1GetTierOrDefault(right);
}

std::string AV1GetLevelIdxOrDefault(const CodecParameterMap& params) {
  // If the parameter is not present, it MUST be inferred to be 5 (level 3.1).
  // https://aomediacodec.github.io/av1-rtp-spec/#72-sdp-parameters
  return GetFmtpParameterOrDefault(params, kAv1FmtpLevelIdx, "5");
}

bool AV1IsSameLevelIdx(const CodecParameterMap& left,
                       const CodecParameterMap& right) {
  return AV1GetLevelIdxOrDefault(left) == AV1GetLevelIdxOrDefault(right);
}

#ifdef RTC_ENABLE_H265
#ifdef RTC_ENABLE_H265_TIGHT_CHECKS
std::string GetH265TxModeOrDefault(const CodecParameterMap& params) {
  // If TxMode is not present, a value of "SRST" must be inferred.
  // https://tools.ietf.org/html/rfc7798@section-7.1
  return GetFmtpParameterOrDefault(params, kH265FmtpTxMode, "SRST");
}

bool IsSameH265TxMode(const CodecParameterMap& left,
                      const CodecParameterMap& right) {
  return absl::EqualsIgnoreCase(GetH265TxModeOrDefault(left),
                                GetH265TxModeOrDefault(right));
}
#endif
#endif

// Some (video) codecs are actually families of codecs and rely on parameters
// to distinguish different incompatible family members.
bool IsSameCodecSpecific(const std::string& name1,
                         const CodecParameterMap& params1,
                         const std::string& name2,
                         const CodecParameterMap& params2) {
  // The assumption when calling this function is that the two formats have the
  // same name.
  RTC_DCHECK(absl::EqualsIgnoreCase(name1, name2));

  VideoCodecType codec_type = PayloadStringToCodecType(name1);
  switch (codec_type) {
    case kVideoCodecH264:
      return H264IsSameProfile(params1, params2) &&
             H264IsSamePacketizationMode(params1, params2);
    case kVideoCodecVP9:
      return VP9IsSameProfile(params1, params2);
    case kVideoCodecAV1:
      return AV1IsSameProfile(params1, params2) &&
             AV1IsSameTier(params1, params2) &&
             AV1IsSameLevelIdx(params1, params2);
#ifdef RTC_ENABLE_H265
    case kVideoCodecH265:
#ifdef RTC_ENABLE_H265_TIGHT_CHECKS
      return H265IsSameProfile(params1, params2) &&
             H265IsSameTier(params1, params2) &&
             IsSameH265TxMode(params1, params2);
#else
      return true;
#endif
#endif
    default:
      return true;
  }
}

}  // namespace

SdpVideoFormat::SdpVideoFormat(const std::string& name) : name(name) {}

SdpVideoFormat::SdpVideoFormat(const std::string& name,
                               const CodecParameterMap& parameters)
    : name(name), parameters(parameters) {}

SdpVideoFormat::SdpVideoFormat(
    const std::string& name,
    const CodecParameterMap& parameters,
    const absl::InlinedVector<ScalabilityMode, kScalabilityModeCount>&
        scalability_modes)
    : name(name),
      parameters(parameters),
      scalability_modes(scalability_modes) {}

SdpVideoFormat::SdpVideoFormat(
    const SdpVideoFormat& format,
    const absl::InlinedVector<ScalabilityMode, kScalabilityModeCount>& modes)
    : SdpVideoFormat(format) {
  scalability_modes = modes;
}

SdpVideoFormat::SdpVideoFormat(const SdpVideoFormat&) = default;
SdpVideoFormat::SdpVideoFormat(SdpVideoFormat&&) = default;
SdpVideoFormat& SdpVideoFormat::operator=(const SdpVideoFormat&) = default;
SdpVideoFormat& SdpVideoFormat::operator=(SdpVideoFormat&&) = default;

SdpVideoFormat::~SdpVideoFormat() = default;

std::string SdpVideoFormat::ToString() const {
  StringBuilder builder;
  builder << "Codec name: " << name << ", parameters: {";
  for (const auto& kv : parameters) {
    builder << " " << kv.first << "=" << kv.second;
  }

  builder << " }";
  if (!scalability_modes.empty()) {
    builder << ", scalability_modes: [";
    bool first = true;
    for (const auto scalability_mode : scalability_modes) {
      if (first) {
        first = false;
      } else {
        builder << ", ";
      }
      builder << ScalabilityModeToString(scalability_mode);
    }
    builder << "]";
  }

  return builder.Release();
}

bool SdpVideoFormat::IsSameCodec(const SdpVideoFormat& other) const {
  // Two codecs are considered the same if the name matches (case insensitive)
  // and certain codec-specific parameters match.
  return absl::EqualsIgnoreCase(name, other.name) &&
         IsSameCodecSpecific(name, parameters, other.name, other.parameters);
}

bool SdpVideoFormat::IsCodecInList(
    ArrayView<const SdpVideoFormat> formats) const {
  for (const auto& format : formats) {
    if (IsSameCodec(format)) {
      return true;
    }
  }
  return false;
}

bool operator==(const SdpVideoFormat& a, const SdpVideoFormat& b) {
  return a.name == b.name && a.parameters == b.parameters &&
         a.scalability_modes == b.scalability_modes;
}

const SdpVideoFormat SdpVideoFormat::VP8() {
  return SdpVideoFormat(kVp8CodecName, {});
}

const SdpVideoFormat SdpVideoFormat::H264() {
  // H264 will typically require more tweaking like setting
  // * packetization-mode (which defaults to 0 but 1 is more common)
  // * level-asymmetry-allowed (which defaults to 0 but 1 is more common)
  // * profile-level-id of which there are many.
  return SdpVideoFormat(kH264CodecName, {});
}

const SdpVideoFormat SdpVideoFormat::H265() {
  return SdpVideoFormat(kH265CodecName, {});
}

const SdpVideoFormat SdpVideoFormat::VP9Profile0() {
  return SdpVideoFormat(
      kVp9CodecName,
      {{kVP9FmtpProfileId, VP9ProfileToString(VP9Profile::kProfile0)}});
}

const SdpVideoFormat SdpVideoFormat::VP9Profile1() {
  return SdpVideoFormat(
      kVp9CodecName,
      {{kVP9FmtpProfileId, VP9ProfileToString(VP9Profile::kProfile1)}});
}

const SdpVideoFormat SdpVideoFormat::VP9Profile2() {
  return SdpVideoFormat(
      kVp9CodecName,
      {{kVP9FmtpProfileId, VP9ProfileToString(VP9Profile::kProfile2)}});
}

const SdpVideoFormat SdpVideoFormat::VP9Profile3() {
  return SdpVideoFormat(
      kVp9CodecName,
      {{kVP9FmtpProfileId, VP9ProfileToString(VP9Profile::kProfile3)}});
}

const SdpVideoFormat SdpVideoFormat::AV1Profile0() {
  // https://aomediacodec.github.io/av1-rtp-spec/#72-sdp-parameters
  return SdpVideoFormat(
      kAv1CodecName,
      {{kAv1FmtpProfile, AV1ProfileToString(AV1Profile::kProfile0).data()},
       {kAv1FmtpLevelIdx, "5"},
       {kAv1FmtpTier, "0"}});
}

const SdpVideoFormat SdpVideoFormat::AV1Profile1() {
  // https://aomediacodec.github.io/av1-rtp-spec/#72-sdp-parameters
  return SdpVideoFormat(
      kAv1CodecName,
      {{kAv1FmtpProfile, AV1ProfileToString(AV1Profile::kProfile1).data()},
       {kAv1FmtpLevelIdx, "5"},
       {kAv1FmtpTier, "0"}});
}

std::optional<SdpVideoFormat> FuzzyMatchSdpVideoFormat(
    ArrayView<const SdpVideoFormat> supported_formats,
    const SdpVideoFormat& format) {
  std::optional<SdpVideoFormat> res;
  int best_parameter_match = 0;
  for (const auto& supported_format : supported_formats) {
    if (absl::EqualsIgnoreCase(supported_format.name, format.name)) {
      int matching_parameters = 0;
      for (const auto& kv : supported_format.parameters) {
        auto it = format.parameters.find(kv.first);
        if (it != format.parameters.end() && it->second == kv.second) {
          matching_parameters += 1;
        }
      }

      if (!res || matching_parameters > best_parameter_match) {
        res = supported_format;
        best_parameter_match = matching_parameters;
      }
    }
  }

  if (!res) {
    RTC_LOG(LS_INFO) << "Failed to match SdpVideoFormat " << format.ToString();
  } else if (*res != format) {
    RTC_LOG(LS_INFO) << "Matched SdpVideoFormat " << format.ToString()
                     << " with " << res->ToString();
  }

  return res;
}

}  // namespace webrtc
