#pragma once
#include <cstdint>
#include "offsets/game/M2.hpp"

namespace wxl::game::m2 {
struct M2SkinSection {
    uint16_t skinSectionId, level, indexStart, indexCount;
};
struct M2SkinProfile {
    uint16_t* indices = nullptr;
    M2SkinSection* submeshes = nullptr;
    uint32_t submeshCount = 0;
    uint32_t indexCount = 0;
};
inline M2SkinProfile* Skin(void* model) {
    return *reinterpret_cast<M2SkinProfile**>(
        reinterpret_cast<char*>(model) + offsets::game::m2::kOffModelSkin);
}
void* GetRenderCtx(void*, void*);
void AttachToScene(void*, void*, uint32_t, bool = false);
void DetachSlot(void*, uint32_t);
void ReleaseRenderCtx(void*);
void* LoadResource(const char*, uint32_t = 0);
void BindTexSlot(void*, void*);
void ReleaseResource(void*);
}
