#include "DemuxMonitor.h"
#include <string>

GST_DEBUG_CATEGORY_EXTERN(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

namespace WebCore {

DemuxMonitor::~DemuxMonitor()
{
    disconnectSignals();
}

void DemuxMonitor::disconnectSignals()
{
    for (const auto &handlerId : m_handlerIds)
        g_signal_handler_disconnect(handlerId.first, handlerId.second);
    m_handlerIds.clear();
}

void DemuxMonitor::init(GstElement *pipeline)
{
    auto handlerId = g_signal_connect(GST_BIN(pipeline), "element-added", G_CALLBACK(onElementAddedCb), this);
    if (handlerId)
        m_handlerIds.insert({pipeline, handlerId});

    handlerId = g_signal_connect(GST_BIN(pipeline), "element-removed", G_CALLBACK(onElementRemovedCb), this);
    if (handlerId)
        m_handlerIds.insert({pipeline, handlerId});
}

void DemuxMonitor::onElementAddedCb(GstBin*, GstElement *element, gpointer data)
{
    if (!element)
        return;

    DemuxMonitor *that = static_cast<DemuxMonitor*>(data);
    if (std::string(GST_ELEMENT_NAME(element)).find("qtdemux") != std::string::npos)
    {
        auto handlerId = g_signal_connect(element, "pad-added", G_CALLBACK(onPadAddedCb), data);
        if (handlerId)
            that->m_handlerIds.insert({element, handlerId});
        /* "pad-removed" doesn't need to be registered as its callback will be removed when gst element is removed */
    }
    else if (g_signal_lookup("element-added", G_OBJECT_TYPE(element)))
    {
        auto handlerId = g_signal_connect(GST_BIN(element), "element-added", G_CALLBACK(onElementAddedCb), data);
        if (handlerId)
            that->m_handlerIds.insert({element, handlerId});

        handlerId = g_signal_connect(GST_BIN(element), "element-removed", G_CALLBACK(onElementRemovedCb), data);
        if (handlerId)
            that->m_handlerIds.insert({element, handlerId});
    }
}

void DemuxMonitor::onElementRemovedCb(GstBin*, GstElement *element, gpointer data)
{
    if (!element)
        return;

    /* Disconnect and remove all callbacks for given element, otherwise there will be use after free vulnerability
     * when this would be done in DemuxMonitor destructor */
    DemuxMonitor *that = static_cast<DemuxMonitor*>(data);
    auto &elements = that->m_handlerIds;
    auto elementRange = elements.equal_range(element);
    for (auto el = elementRange.first; el != elementRange.second;) {
        g_signal_handler_disconnect(el->first, el->second);
        el = elements.erase(el);
    }
}

void DemuxMonitor::onPadAddedCb(GstElement *element, GstPad *newPad, gpointer data)
{
    if (!newPad)
        return;

    auto caps = gst_pad_get_current_caps(newPad);
    auto capsString = gst_caps_to_string(caps);

    if (std::string(capsString).find("audio") != std::string::npos)
    {
        GstPadProbeType probeType = static_cast<GstPadProbeType>(
            GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER);
        gst_pad_add_probe(newPad, probeType, onPadProbeCb, data, nullptr);
        GST_DEBUG("added probe to pad %s of element %s", GST_PAD_NAME(newPad), GST_ELEMENT_NAME(element));
    }

    g_free(capsString);
    gst_caps_unref(caps);
}

GstPadProbeReturn DemuxMonitor::onPadProbeCb(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    GstPadProbeType type = GST_PAD_PROBE_INFO_TYPE(info);
    GstPadProbeReturn ret = GST_PAD_PROBE_OK;
    DemuxMonitor *that = static_cast<DemuxMonitor*>(data);

    if (type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
    {
        auto event = gst_pad_probe_info_get_event(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT)
            that->onSegmentEvent(event);
    }
    else if (type & GST_PAD_PROBE_TYPE_BUFFER)
    {
        auto buffer = gst_pad_probe_info_get_buffer(info);
        ret = that->onPadProbeBuffer(pad, buffer);
    }

    return ret;
}

void DemuxMonitor::onSegmentEvent(GstEvent *segment)
{
    const GstSegment* seg{};
    gst_event_parse_segment(segment, &seg);

    if (!seg)
        return;
    if (seg->rate > 0.0)
    {
        m_seekTimestamp = seg->start;
        m_skipFirst = true;
    }
}

GstPadProbeReturn DemuxMonitor::onPadProbeBuffer(GstPad*, GstBuffer *buffer)
{
    if (m_seekTimestamp == 0)
        return GST_PAD_PROBE_OK;

    auto ret = GST_PAD_PROBE_OK;
    auto bufTimestamp = GST_BUFFER_TIMESTAMP(buffer);

    if (bufTimestamp < m_seekTimestamp && !(m_skipFirst && (bufTimestamp + SKIP_TRESHOLD) > m_seekTimestamp))
    {
        gst_buffer_unref(buffer);
        ret = GST_PAD_PROBE_HANDLED;
        GST_DEBUG("unref demux buffer, seek timestamp: %llu, pts: %llu", m_seekTimestamp, bufTimestamp);
    }
    else
    {
        m_seekTimestamp = 0;
    }
    m_skipFirst = false;

    return ret;
}

}
