#pragma once

#include <string>
#include "ITelemetry.h"

namespace Telemetry {

class RdkTelemetryReport: public IReport {

public:
    void reportPlaybackState(
                AVPipelineState state,
                const std::string &additionalInfo = "",
                MediaType mediaType = MediaType::NONE) override;
    void reportDrmInfo(
                DrmType drmType,
                const std::string &additionalInfo = "") override;
    void reportWaylandInfo(
                const IWaylandInfoGetter &getter,
                WaylandAction action,
                WaylandGraphicsState gfxState,
                WaylandInputsState inputsState) override;
};
}
