#include <map>
#include <string>
#include <cstdarg>

#include "TelemetryReport.h"
#include "av_pipeline.h"
#include "odhott_wl.h"
#include "odherr_ctx.h"

namespace AVP = OttReports::AvPipeline;

namespace Telemetry {

static AVP::AvPipeline avPipelineReport{OttReports::Owner::type::Wpe};

void init(const std::string &name) {
    odh_error_report_init(name.c_str());
}

void deinit() {
    odh_error_report_deinit(ODH_ERROR_REPORT_DEINIT_MODE_DEFERRED);
}

void reportErrorV(const char* file, int line, const char* function, const char* format, va_list args)
{
    int length = vsnprintf(NULL, 0, format, args);
    if (length < 0) return;

    char *msg = (char*)malloc(length + 1);
    if (!msg) return;

    vsnprintf(msg, length + 1, format, args);

    char* backtrace = odh_error_report_sprintf("%s:%d %s", file, line, function);
    char* ctx = odh_ctx_create_json("wpe", "ss",
            "function", function,
            "file", file);
    odh_error_report_send_v3(ODH_ERROR_REPORT_SENSITIVITY_NONSENSITIVE,
            ODH_ERROR_REPORT_LEVEL_ERROR,
            "WPE0050",
            nullptr,
            msg,
            ctx,
            backtrace,
            "browser");
    free(ctx);
    free(backtrace);
    free(msg);
}

void reportError(const char* file, int line, const char* function, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    reportErrorV(file, line, function, format, args);
    va_end(args);
}

static std::map<IReport::AVPipelineState, AVP::State::type> pipelineStateMap {
    {IReport::AVPipelineState::CREATE, AVP::State::type::Create},
    {IReport::AVPipelineState::PLAY, AVP::State::type::Play},
    {IReport::AVPipelineState::PAUSE, AVP::State::type::Pause},
    {IReport::AVPipelineState::STOP, AVP::State::type::Stop},
    {IReport::AVPipelineState::DESTROY, AVP::State::type::Destroy},
    {IReport::AVPipelineState::FIRST_FRAME_DECODED, AVP::State::type::FirstFrameDecoded},
    {IReport::AVPipelineState::END_OF_STREAM, AVP::State::type::EndOfStream},
    {IReport::AVPipelineState::DECRYPT_ERROR, AVP::State::type::DecryptError},
    {IReport::AVPipelineState::PLAYBACK_ERROR, AVP::State::type::PlaybackError},
    {IReport::AVPipelineState::DRM_ERROR, AVP::State::type::DrmError},
    {IReport::AVPipelineState::ERROR, AVP::State::type::Error},
    {IReport::AVPipelineState::SEEK_START, AVP::State::type::SeekStart},
    {IReport::AVPipelineState::SEEK_DONE, AVP::State::type::SeekDone},
    {IReport::AVPipelineState::VIDEO_RESOLUTION_CHANGED, AVP::State::type::VideoResolutionChanged},
    {IReport::AVPipelineState::UNKNOWN, AVP::State::type::Unknown}
};

static std::map<IReport::DrmType, AVP::Drm::type> drmTypeMap {
    {IReport::DrmType::PLAYREADY, AVP::Drm::type::Playready},
    {IReport::DrmType::WIDEVINE, AVP::Drm::type::Widevine},
    {IReport::DrmType::NONE, AVP::Drm::type::None},
    {IReport::DrmType::UNKNOWN, AVP::Drm::type::Unknown},
};

static std::map<IReport::WaylandAction, odh_report_wayland_action_t> waylandActionMap {
    {IReport::WaylandAction::INIT_GFX , odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_INIT_GFX},
    {IReport::WaylandAction::DEINIT_GFX, odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_DEINIT_GFX},
    {IReport::WaylandAction::INIT_INPUTS, odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_INIT_INPUTS},
    {IReport::WaylandAction::DEINIT_INPUTS, odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_DEINIT_INPUTS}
};

static std::map<IReport::WaylandGraphicsState, bool> waylandGraphicsStateMap {
    {IReport::WaylandGraphicsState::GFX_NOT_INITIALIZED, false},
    {IReport::WaylandGraphicsState::GFX_INITIALIZED, true}
};

static std::map<IReport::WaylandInputsState, bool> waylandInputsStateMap {
    {IReport::WaylandInputsState::INPUTS_NOT_INITIALIZED, false},
    {IReport::WaylandInputsState::INPUTS_INITIALIZED, true}
};

void RdkTelemetryReport::reportPlaybackState(
            IReport::AVPipelineState state,
            const std::string &additionalInfo,
            MediaType mediaType) {
    avPipelineReport.setSource(AVP::Source::type::Unknown);
    avPipelineReport.setAdditionalInfo(additionalInfo.empty() ? "" : additionalInfo);
    avPipelineReport.send(pipelineStateMap[state]);
}

void RdkTelemetryReport::reportDrmInfo(
                IReport::DrmType drmType,
                const std::string &additionalInfo) {
    avPipelineReport.setDrm(drmTypeMap[drmType]);
    avPipelineReport.setSource(AVP::Source::type::Unknown);
    avPipelineReport.setAdditionalInfo(additionalInfo.empty() ? "" : additionalInfo);
    avPipelineReport.send(AVP::State::type::Unknown);
}

void RdkTelemetryReport::reportWaylandInfo(
                const IWaylandInfoGetter &getter,
                IReport::WaylandAction action,
                IReport::WaylandGraphicsState gfxState,
                IReport::WaylandInputsState inputsState) {
    odh_ott_wayland_report(
        reinterpret_cast<const WaylandContextInfoGetter&>(getter),
        ODH_REPORT_WAYLAND_OWNER_WPE,
        waylandActionMap[action],
        waylandGraphicsStateMap[gfxState],
        waylandInputsStateMap[inputsState]);
}
}
