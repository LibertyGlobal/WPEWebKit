#pragma once

#include <stdarg.h>
#include <string>

#if defined(RDK_TELEMETRY)
    #include "RdkTelemetryReport.h"
    using TelemetryImpl = Telemetry::RdkTelemetryReport;
#else
    #include "DummyTelemetryReport.h"
    using TelemetryImpl = Telemetry::DummyTelemetryReport;
#endif

namespace Telemetry
{
    void init(const std::string &name = "WebKitBrowser");
    void deinit();
    void reportErrorV(const char* file, int line, const char* function, const char* format, va_list args);
    void reportError(const char* file, int line, const char* function, const char* format, ...);
}
