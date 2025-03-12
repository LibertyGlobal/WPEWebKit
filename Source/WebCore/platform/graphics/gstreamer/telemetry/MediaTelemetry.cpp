/*
 * Copyright (C) 2025 Comcast
 * Copyright (C) 2025 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "MediaTelemetry.h"

#if ENABLE(MEDIA_TELEMETRY)

#include <map>
#include "NotImplemented.h"

#include "av_pipeline.h"
#include "odhott_wl.h"
#include "odherr_ctx.h"

namespace AVP = OttReports::AvPipeline;

namespace WebCore {

static AVP::AvPipeline avPipelineReport{OttReports::Owner::type::Wpe};

static std::map<MediaTelemetryReport::AVPipelineState, AVP::State::type> pipelineStateMap {
    {MediaTelemetryReport::AVPipelineState::Create, AVP::State::type::Create},
    {MediaTelemetryReport::AVPipelineState::Play, AVP::State::type::Play},
    {MediaTelemetryReport::AVPipelineState::Pause, AVP::State::type::Pause},
    {MediaTelemetryReport::AVPipelineState::Stop, AVP::State::type::Stop},
    {MediaTelemetryReport::AVPipelineState::Destroy, AVP::State::type::Destroy},
    {MediaTelemetryReport::AVPipelineState::FirstFrameDecoded, AVP::State::type::FirstFrameDecoded},
    {MediaTelemetryReport::AVPipelineState::EndOfStream, AVP::State::type::EndOfStream},
    {MediaTelemetryReport::AVPipelineState::DecryptError, AVP::State::type::DecryptError},
    {MediaTelemetryReport::AVPipelineState::PlaybackError, AVP::State::type::PlaybackError},
    {MediaTelemetryReport::AVPipelineState::DrmError, AVP::State::type::DrmError},
    {MediaTelemetryReport::AVPipelineState::Error, AVP::State::type::Error},
    {MediaTelemetryReport::AVPipelineState::SeekStart, AVP::State::type::SeekStart},
    {MediaTelemetryReport::AVPipelineState::SeekDone, AVP::State::type::SeekDone},
    {MediaTelemetryReport::AVPipelineState::VideoResolutionChanged, AVP::State::type::VideoResolutionChanged},
    {MediaTelemetryReport::AVPipelineState::Unknown, AVP::State::type::Unknown}
};

static std::map<MediaTelemetryReport::DrmType, AVP::Drm::type> drmTypeMap {
    {MediaTelemetryReport::DrmType::PlayReady, AVP::Drm::type::Playready},
    {MediaTelemetryReport::DrmType::Widevine, AVP::Drm::type::Widevine},
    {MediaTelemetryReport::DrmType::None, AVP::Drm::type::None},
    {MediaTelemetryReport::DrmType::Unknown, AVP::Drm::type::Unknown},
};

static std::map<MediaTelemetryReport::WaylandAction, odh_report_wayland_action_t> waylandActionMap {
    {MediaTelemetryReport::WaylandAction::InitGfx , odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_INIT_GFX},
    {MediaTelemetryReport::WaylandAction::DeinitGfx, odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_DEINIT_GFX},
    {MediaTelemetryReport::WaylandAction::InitInputs, odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_INIT_INPUTS},
    {MediaTelemetryReport::WaylandAction::DeinitInputs, odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_DEINIT_INPUTS}
};

static std::map<MediaTelemetryReport::WaylandGraphicsState, bool> waylandGraphicsStateMap {
    {MediaTelemetryReport::WaylandGraphicsState::GfxNotInitialized, false},
    {MediaTelemetryReport::WaylandGraphicsState::GfxInitialized, true}
};

static std::map<MediaTelemetryReport::WaylandInputsState, bool> waylandInputsStateMap {
    {MediaTelemetryReport::WaylandInputsState::InputsNotInitialized, false},
    {MediaTelemetryReport::WaylandInputsState::InputsInitialized, true}
};

MediaTelemetryReport& MediaTelemetryReport::singleton()
{
    static MediaTelemetryReport instance;
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        instance.m_privateMembers = makeUnique<MediaTelemetryReportPrivateMembers>();
        instance.m_name = "WPEWebKit"_s;
        notImplemented();
    });
    return instance;
}

// deInit can be implemented here.
MediaTelemetryReport::~MediaTelemetryReport()
{
    notImplemented();
}

void MediaTelemetryReport::reportPlaybackState(AVPipelineState state, const String& additionalInfo, MediaType mediaType)
{
    avPipelineReport.setSource(AVP::Source::type::Unknown);
    avPipelineReport.setAdditionalInfo(additionalInfo.isEmpty() ? "" : additionalInfo.utf8().data());
    avPipelineReport.send(pipelineStateMap[state]);
}

void MediaTelemetryReport::reportDrmInfo(DrmType drmType, const String& additionalInfo)
{
    avPipelineReport.setDrm(drmTypeMap[drmType]);
    avPipelineReport.setSource(AVP::Source::type::Unknown);
    avPipelineReport.setAdditionalInfo(additionalInfo.isEmpty() ? "" : additionalInfo.utf8().data());
    avPipelineReport.send(AVP::State::type::Unknown);
}

void MediaTelemetryReport::reportWaylandInfo(const MediaTelemetryWaylandInfoGetter& getter, WaylandAction action,
    WaylandGraphicsState gfxState, WaylandInputsState inputsState)
{
    odh_ott_wayland_report(
        reinterpret_cast<const WaylandContextInfoGetter&>(getter),
        ODH_REPORT_WAYLAND_OWNER_WPE,
        waylandActionMap[action],
        waylandGraphicsStateMap[gfxState],
        waylandInputsStateMap[inputsState]);
}

} // namespace WebCore

#endif // ENABLE(MEDIA_TELEMETRY)
