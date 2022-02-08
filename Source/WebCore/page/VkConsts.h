#ifndef VKCONSTS_H
#define VKCONSTS_H

#include "DOMWindowProperty.h"
#include <wtf/Ref.h>
#include <wtf/RefCounted.h>

namespace WebCore {

class Frame;

class VkConsts final : public RefCounted<VkConsts>, public DOMWindowProperty {
public:
    static Ref<VkConsts> create(Frame* frame) { return adoptRef(*new VkConsts(frame)); }

private:
    explicit VkConsts(Frame*);
};
} // namespace WebCore

#endif // VKCONSTS_H
