#include "TelemetryReport.h"

namespace Telemetry {

void init(const std::string &name) {
    (void)name;
}

void deinit() {
}

void reportErrorV(const char* file, int line, const char* function, const char* format, va_list args) {
    (void)file;
    (void)line;
    (void)function;
    (void)format;
    (void)args;
}

void reportError(const char* file, int line, const char* function, const char* format, ...) {
    (void)file;
    (void)line;
    (void)function;
    (void)format;
}

void DummyTelemetryReport::reportPlaybackState(
            AVPipelineState state,
            const std::string &additionalInfo,
            MediaType mediaType) {
    (void)state;
    (void)additionalInfo;
    (void)mediaType;
}

void DummyTelemetryReport::reportDrmInfo(
           DrmType drmType,
           const std::string &additionalInfo) {
    (void)drmType;
    (void)additionalInfo;
}

void DummyTelemetryReport::reportWaylandInfo(
           const IWaylandInfoGetter &getter,
           WaylandAction action,
           WaylandGraphicsState gfxState,
           WaylandInputsState inputsState) {
    (void)getter;
    (void)action;
    (void)gfxState;
    (void)inputsState;
}
}
