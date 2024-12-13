set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(TELEMETRY_DIR "${THIRDPARTY_DIR}/telemetry")

set(TELEMETRY_INCLUDE_DIRECTORIES
    "${TELEMETRY_DIR}/include"
    "${CMAKE_BINARY_DIR}"
)

include_directories("${TELEMETRY_INCLUDE_DIRECTORIES}")

if (USE_RDK_TELEMETRY)
    find_package(OdhErrTelemetry)
    if (NOT OdhErrTelemetry_FOUND)
        message(FATAL_ERROR "OdhErrTelemetry is needed for USE_RDK_TELEMETRY")
    endif ()

    find_package(OdhOttTelemetry)
    if (NOT OdhOttTelemetry_FOUND)
        message(FATAL_ERROR "OdhOttTelemetry is needed for USE_RDK_TELEMETRY")
    endif ()

    include_directories(
        ${OdhErrTelemetry_INCLUDE_DIR}
        ${OdhOttTelemetry_INCLUDE_DIR}
    )

    set(TELEMETRY_LIBRARIES
        ${OdhErrTelemetry_LIBRARIES}
        ${OdhOttTelemetry_LIBRARIES}
    )

    set(TELEMETRY_SOURCES
        ${TELEMETRY_DIR}/src/RdkTelemetryReport.cpp
    )

    add_definitions(-DRDK_TELEMETRY)
else ()
    set(TELEMETRY_SOURCES
        ${TELEMETRY_DIR}/src/DummyTelemetryReport.cpp
    )
endif ()

add_library(telemetry STATIC ${TELEMETRY_SOURCES})
target_link_libraries(telemetry PRIVATE ${TELEMETRY_LIBRARIES})
