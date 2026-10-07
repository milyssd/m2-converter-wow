#pragma once
#include <cstdint>
#include <vector>

namespace wxl::events {
enum class Event {
    OnItemSlotChange, OnItemSlotClear, OnM2SkinFinalize,
    OnM2PerFrameUpdate, OnBuildBonePalette, OnUpdate, Count
};
struct ItemSlotChangeArgs { void* charModelObj; uint32_t modelSlot; void* itemDataPtr; };
struct ItemSlotClearArgs { void* charModelObj; uint32_t equipSlotWow; };
struct M2SkinFinalizeArgs { void* model; };
struct M2PerFrameUpdateArgs { void* renderCtx; };
struct BuildBonePaletteArgs { void* renderCtx; };
struct UpdateArgs { float dt; uint32_t timeMs; };
using Handler = void (*)(void*, const void*);
struct Subscription { Handler handler; void* user; };
inline auto& Subscribers(Event event) {
    static std::vector<Subscription> subscriptions[static_cast<unsigned>(Event::Count)];
    return subscriptions[static_cast<unsigned>(event)];
}
inline void Subscribe(Event event, Handler handler, void* user) {
    Subscribers(event).push_back({handler, user});
}
inline void Emit(Event event, const void* args) {
    for (const auto& entry : Subscribers(event)) entry.handler(entry.user, args);
}
}
