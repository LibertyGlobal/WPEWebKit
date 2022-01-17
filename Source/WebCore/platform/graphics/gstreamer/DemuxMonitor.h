#pragma once

#include <gst/gst.h>
#include <cstdint>
#include <map>

namespace WebCore {

class DemuxMonitor
{
public:
    DemuxMonitor() = default;
    ~DemuxMonitor();
    void init(GstElement *pipeline);

private:
    void disconnectSignals();
    void onSegmentEvent(GstEvent* segment);
    GstPadProbeReturn onPadProbeBuffer(GstPad* pad, GstBuffer* buffer);

    static void onElementAddedCb(GstBin *pipeline, GstElement *element, gpointer data);
    static void onPadAddedCb(GstElement* element, GstPad* newPad, gpointer data);
    static GstPadProbeReturn onPadProbeCb(GstPad* pad, GstPadProbeInfo* info, gpointer data);

    const uint64_t                SKIP_TRESHOLD{200*GST_MSECOND};
    uint64_t                      m_seekTimestamp{0};
    bool                          m_skipFirst{false};
    std::map<GstElement*, gulong> m_handlerIds{};
};

}