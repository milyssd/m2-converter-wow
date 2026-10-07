// Exercises the actual equipment event handlers with a deterministic fake engine.
// It validates state and ownership ordering; it cannot validate Windows SEH, native
// offsets, rendering, or compatibility with a live AzerothCore client/server.
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "events/Event.hpp"
#include "game/m2/M2.hpp"
#include "offsets/game/DB2.hpp"
#include "Settings.hpp"
#include "VirtualPath.hpp"

namespace test_engine {
struct alignas(void*) Bytes {
    std::array<unsigned char, 512> bytes{};
    void* data() { return bytes.data(); }
    template<class T> void put(size_t offset, T value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
};
struct Context {
    Bytes memory;
    Bytes model;
    std::array<uint16_t, 9> indices{{1, 2, 3, 4, 5, 6, 7, 8, 9}};
    std::array<wxl::game::m2::M2SkinSection, 3> sections{{
        {0, 0, 0, 3}, {1301, 0, 3, 3}, {1302, 0, 6, 3}
    }};
    wxl::game::m2::M2SkinProfile skin;
    int references = 0;
    bool alive = true;
    std::string key;
    std::string texture;
};
struct Display {
    const char* model1 = "";
    const char* model2 = "";
    const char* texture1 = "";
    const char* texture2 = "";
    const char* config = "";
};
inline std::unordered_map<std::string, std::unique_ptr<Context>> contexts;
inline std::unordered_map<void*, Context*> byPointer;
inline std::unordered_map<void*, std::map<uint32_t, std::vector<void*>>> attachments;
inline std::unordered_map<uint32_t, Display> displays;
inline std::vector<std::string> operations;
inline std::vector<void*> evictions;
inline bool finalizeOnGet = false;

inline void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
inline Context& Find(void* pointer) {
    const auto found = byPointer.find(pointer);
    Require(found != byPointer.end(), "unknown fake render context");
    return *found->second;
}
uint32_t Lookup(void*, void*, uint32_t id, void* output) {
    const auto found = displays.find(id);
    if (found == displays.end()) return 0;
    namespace db = wxl::offsets::game::db2::itemdisplayinfo;
    const auto& row = found->second;
    auto* buffer = static_cast<unsigned char*>(output);
    std::memset(buffer, 0, db::kRecordSize);
    std::memcpy(buffer + db::kOffModel1, &row.model1, sizeof(row.model1));
    std::memcpy(buffer + db::kOffModel2, &row.model2, sizeof(row.model2));
    std::memcpy(buffer + db::kOffTex1, &row.texture1, sizeof(row.texture1));
    std::memcpy(buffer + db::kOffTex2, &row.texture2, sizeof(row.texture2));
    std::memcpy(buffer + db::kOffIcon2, &row.config, sizeof(row.config));
    return 1;
}
bool HasAttachment(void* scene, uint32_t slot, void* context) {
    const auto& items = attachments[scene][slot];
    return std::find(items.begin(), items.end(), context) != items.end();
}
bool MutatedScene() {
    return std::any_of(operations.begin(), operations.end(), [](const std::string& operation) {
        return operation.rfind("detach:", 0) == 0 || operation.rfind("attach:", 0) == 0;
    });
}
}

namespace wxl::game::m2 {
void* GetRenderCtx(void*, void* keyPointer) {
    const std::string key = static_cast<const char*>(keyPointer);
    test_engine::operations.push_back("get:" + key);
    auto& record = test_engine::contexts[key];
    if (!record) {
        record = std::make_unique<test_engine::Context>();
        record->key = key;
        test_engine::byPointer[record->memory.data()] = record.get();
        record->memory.put(wxl::offsets::game::m2::kOffInstModel, record->model.data());
        if (test_engine::finalizeOnGet && key.rfind("virtual:", 0) == 0) {
            namespace off = wxl::offsets::game::m2;
            record->skin.indices = record->indices.data();
            record->skin.indexCount = static_cast<uint32_t>(record->indices.size());
            record->skin.submeshes = record->sections.data();
            record->skin.submeshCount = static_cast<uint32_t>(record->sections.size());
            record->model.put(off::kOffModelSkin, &record->skin);
            std::strcpy(reinterpret_cast<char*>(record->model.bytes.data()) + off::kOffModelPathStem, "common");
            record->memory.put(off::kOffInstModel, record->model.data());
            const wxl::events::M2SkinFinalizeArgs args{record->model.data()};
            wxl::events::Emit(wxl::events::Event::OnM2SkinFinalize, &args);
        }
    }
    test_engine::Require(record->alive, "GetRenderCtx reached a freed cache entry");
    // A retained instance whose native model field was invalidated can be loaded
    // again. The allocation must still be held alive before scene detaches.
    record->memory.put(wxl::offsets::game::m2::kOffInstModel, record->model.data());
    ++record->references;
    return record->memory.data();
}
void AttachToScene(void* context, void* scene, uint32_t slot, bool) {
    auto& record = test_engine::Find(context);
    test_engine::Require(record.alive, "attaching freed render context");
    ++record.references;
    test_engine::attachments[scene][slot].push_back(context);
    test_engine::operations.push_back("attach:" + std::to_string(slot));
}
void ReleaseRenderCtx(void* context) {
    auto& record = test_engine::Find(context);
    test_engine::Require(record.references > 0, "render reference released twice");
    --record.references;
    if (record.references == 0) record.alive = false;
    test_engine::operations.push_back("release:" + record.key);
}
void DetachSlot(void* scene, uint32_t slot) {
    test_engine::operations.push_back("detach:" + std::to_string(slot));
    auto& items = test_engine::attachments[scene][slot];
    for (void* context : items) ReleaseRenderCtx(context);
    items.clear();
}
void* LoadResource(const char* path, uint32_t) { return const_cast<char*>(path); }
void BindTexSlot(void* context, void* texture) {
    test_engine::Find(context).texture = static_cast<const char*>(texture);
}
void ReleaseResource(void*) {}
}

namespace wxl::scripts::equipextension {
settings::BoolSetting::BoolSetting(const char*, const char*, const char*, bool value) noexcept
    : value_(value) {}
size_t VPathBuildKey(char* output, size_t capacity, void* cmo, const char* path,
                     const uint16_t* ids, uint32_t count, const char* texture, bool relocate,
                     uint32_t attachmentId) {
    std::string filter;
    for (uint32_t i = 0; i < count; ++i) filter += std::to_string(ids[i]) + ",";
    const int written = std::snprintf(output, capacity, "virtual:%p:%s:%s:%s:%d:%u", cmo, path,
                                     filter.c_str(), texture, static_cast<int>(relocate), attachmentId);
    return written > 0 && static_cast<size_t>(written) < capacity
        ? static_cast<size_t>(written) : 0;
}
bool VPathPopulate(void*, const char*, const uint16_t*, uint32_t, const char*, bool, uint32_t) { return true; }
void VPathEvictCmo(void* cmo) { test_engine::evictions.push_back(cmo); }
}

// No copied implementation: compile the same handler source the DLL builds.
#include "../src/EquipExtension.cpp"

namespace {
namespace ext = wxl::scripts::equipextension;
namespace event = wxl::events;
namespace engine = test_engine;
namespace offset = wxl::offsets::game::m2;
using engine::Require;

void Update(uint32_t timeMs = 16) {
    const event::UpdateArgs args{0.016f, timeMs};
    event::Emit(event::Event::OnUpdate, &args);
}
void ResetHandlerState() {
    for (const auto& character : ext::g_clearRefs)
        for (void* context : character.second) wxl::game::m2::ReleaseRenderCtx(context);
    ext::g_clearRefs.clear();
    ext::g_pendingDetaches.clear();
    ext::g_pendingRebuilds.clear();
    ext::g_missingScenes.clear();
    ext::g_attached.clear();
    ext::g_currentLoadingEntry = nullptr;
    ext::g_currentRebuildCmo = nullptr;
}
struct Fixture {
    engine::Bytes cmo, scene, owner;
    Fixture() {
        ResetHandlerState();
        engine::attachments.clear();
        engine::contexts.clear();
        engine::byPointer.clear();
        engine::operations.clear();
        engine::evictions.clear();
        engine::displays.clear();
        engine::finalizeOnGet = false;
        cmo.put(offset::kOffCmoSceneNode, scene.data());
        cmo.put(offset::kOffCmoRace, uint32_t{1});
        cmo.put(offset::kOffCmoGender, uint32_t{0});
        scene.put(offset::kOffSceneNodeOwner, owner.data());
    }
    ~Fixture() {
        // Release retained refs and clear all queues before fixture memory dies.
        ResetHandlerState();
    }
    void* Seed(uint32_t modelSlot, uint32_t attach, const char* path,
               bool collection = false, const char* texture = "") {
        ext::AttachEntry entry{};
        entry.equipSlot = modelSlot;
        entry.attachId = attach;
        entry.subObj = scene.data();
        std::snprintf(entry.keyBuf, sizeof(entry.keyBuf), "%s", path);
        std::snprintf(entry.texBuf, sizeof(entry.texBuf), "%s", texture);
        if (collection) { entry.geoFilter.ids[0] = 1301; entry.geoFilter.count = 1; }
        ext::VPathBuildKey(entry.mangledKeyBuf, sizeof(entry.mangledKeyBuf), cmo.data(), path,
                          entry.geoFilter.ids, entry.geoFilter.count, texture, false, attach);
        void* context = wxl::game::m2::GetRenderCtx(owner.data(), entry.mangledKeyBuf);
        wxl::game::m2::AttachToScene(context, scene.data(), attach, collection);
        wxl::game::m2::ReleaseRenderCtx(context);
        entry.renderCtx = context;
        ext::g_attached[cmo.data()].push_back(entry);
        engine::operations.clear();
        return context;
    }
    void Clear(uint32_t wowSlot) {
        const event::ItemSlotClearArgs args{cmo.data(), wowSlot};
        event::Emit(event::Event::OnItemSlotClear, &args);
    }
    void Change(uint32_t modelSlot, uint32_t displayId) {
        const event::ItemSlotChangeArgs args{cmo.data(), modelSlot, &displayId};
        event::Emit(event::Event::OnItemSlotChange, &args);
    }
};

void CapeAndTabardUseNativeSlots() {
    for (const auto& pair : {std::pair<uint32_t, uint32_t>{14, 10}, {18, 9}}) {
        Fixture fixture;
        const uint32_t removed = pair.second;
        const uint32_t survivor = removed == 10 ? 9 : 10;
        const uint32_t removedAttach = removed == 10 ? 12 : 34;
        const uint32_t survivorAttach = survivor == 10 ? 12 : 34;
        fixture.Seed(removed, removedAttach, "removed.mdx");
        void* survivorContext = fixture.Seed(survivor, survivorAttach, "survivor.mdx");
        fixture.Clear(pair.first);
        const auto& entries = ext::g_attached.at(fixture.cmo.data());
        Require(entries.size() == 1 && entries[0].equipSlot == survivor,
                "cape/tabard clear erased the wrong internal slot");
        Require(!engine::MutatedScene(), "clear event mutated scene before native clear");
        wxl::game::m2::DetachSlot(fixture.scene.data(), removedAttach);
        Update();
        Require(engine::HasAttachment(fixture.scene.data(), survivorAttach, survivorContext),
                "surviving cape/tabard disappeared after deferred rebuild");
    }
}

void SharedAttachmentIsRetainedBeforeNativeClear() {
    Fixture fixture;
    void* shared = fixture.Seed(2, 34, "shared.mdx");
    fixture.Seed(3, 34, "shared.mdx");
    fixture.Clear(3); // native WoW shirt slot -> internal model slot 2
    Require(!engine::MutatedScene(), "pre-native clear changed the scene");
    Require(engine::Find(shared).references > 2, "shared survivor was not held before native clear");
    wxl::game::m2::DetachSlot(fixture.scene.data(), 34);
    Require(engine::Find(shared).alive, "native clear freed a surviving shared model");
    Update();
    Require(engine::HasAttachment(fixture.scene.data(), 34, shared),
            "shared survivor was not restored after native clear");
    Require(engine::Find(shared).references == 1, "deferred clear leaked a render reference");
}

void LastEntryDetachesAndEvictsVirtualCache() {
    Fixture fixture;
    fixture.Seed(3, 34, "collection.mdx", true);
    fixture.Clear(4);
    Require(!engine::MutatedScene(), "last-entry clear mutated scene before native clear");
    wxl::game::m2::DetachSlot(fixture.scene.data(), 34);
    Update();
    Require(ext::g_attached.find(fixture.cmo.data()) == ext::g_attached.end(),
            "last-entry clear retained empty character state");
    Require(std::find(engine::evictions.begin(), engine::evictions.end(), fixture.cmo.data())
                != engine::evictions.end(), "last-entry clear did not evict virtual models");
    Require(engine::attachments[fixture.scene.data()][34].empty(),
            "last-entry clear left a native attachment");
}

void EmptyDisplayReplacesOldModel() {
    Fixture fixture;
    fixture.Seed(3, 34, "old.mdx");
    engine::displays[100] = engine::Display{};
    fixture.Change(3, 100);
    Update();
    Require(ext::g_attached.find(fixture.cmo.data()) == ext::g_attached.end(),
            "blank DBC model left stale attachment state");
    Require(engine::attachments[fixture.scene.data()][34].empty(),
            "blank DBC model left old native model visible");
}

void PendingModelSurvivesUninitializedScene() {
    Fixture fixture;
    fixture.cmo.put(offset::kOffCmoSceneNode, static_cast<void*>(nullptr));
    engine::displays[101] = engine::Display{"pending"};
    fixture.Change(3, 101);
    auto found = ext::g_attached.find(fixture.cmo.data());
    Require(found != ext::g_attached.end() && found->second.size() == 1,
            "equip while scene uninitialized discarded model configuration");
    Require(found->second[0].renderCtx == nullptr, "pending model attached before scene existed");
    fixture.cmo.put(offset::kOffCmoSceneNode, fixture.scene.data());
    const event::M2PerFrameUpdateArgs args{fixture.scene.data()};
    event::Emit(event::Event::OnM2PerFrameUpdate, &args);
    Update();
    Require(ext::g_attached.at(fixture.cmo.data())[0].renderCtx != nullptr,
            "pending model did not attach when scene became available");
}

void NormalAttachmentKeepsOwnBonePalette() {
    Fixture fixture;
    void* context = fixture.Seed(0, 11, "helm.mdx");
    std::array<unsigned char, offset::kBonePaletteStride> characterPalette{};
    std::array<unsigned char, offset::kBonePaletteStride> modelPalette{};
    characterPalette.fill(0xA5);
    modelPalette.fill(0x5A);
    fixture.scene.put(offset::kOffInstBonePalette, characterPalette.data());
    engine::Find(context).memory.put(offset::kOffInstBonePalette, modelPalette.data());
    auto& entry = ext::g_attached.at(fixture.cmo.data())[0];
    entry.boneRemap.count = 1;
    entry.boneRemap.collToChar[0] = 0;
    const event::M2PerFrameUpdateArgs args{fixture.scene.data()};
    event::Emit(event::Event::OnM2PerFrameUpdate, &args);
    Require(std::all_of(modelPalette.begin(), modelPalette.end(), [](unsigned char v) { return v == 0x5A; }),
            "normal attached model bone palette was overwritten with character bones");
    // The world-scene bit alone does not turn a normal attachment into a collection.
    engine::Find(context).memory.put(offset::kOffInstInitFlags, uint32_t{0x40000});
    const event::BuildBonePaletteArgs build{context};
    event::Emit(event::Event::OnBuildBonePalette, &build);
    Require(std::all_of(modelPalette.begin(), modelPalette.end(), [](unsigned char v) { return v == 0x5A; }),
            "normal model palette was overwritten merely because initFlags included 0x40000");
}

void CollectionPaletteDoesNotRequireWorldSceneBit() {
    Fixture fixture;
    void* context = fixture.Seed(3, 34, "collection.mdx", true);
    alignas(float) std::array<unsigned char, offset::kBonePaletteStride> characterPalette{};
    alignas(float) std::array<unsigned char, offset::kBonePaletteStride> modelPalette{};
    characterPalette.fill(0xA5);
    modelPalette.fill(0x5A);
    fixture.scene.put(offset::kOffInstBonePalette, characterPalette.data());
    engine::Find(context).memory.put(offset::kOffInstBonePalette, modelPalette.data());
    auto& entry = ext::g_attached.at(fixture.cmo.data())[0];
    entry.boneRemap.count = 1;
    entry.boneRemap.collToChar[0] = 0;
    const event::BuildBonePaletteArgs args{context};
    event::Emit(event::Event::OnBuildBonePalette, &args);
    Require(modelPalette == characterPalette, "paperdoll collection without 0x40000 did not inherit character bones");
}

void SynchronousFinalizeSeparatesSameBasenameGroups() {
    Fixture fixture;
    engine::finalizeOnGet = true;
    fixture.cmo.put(offset::kOffCmoSceneNode, static_cast<void*>(nullptr));
    engine::displays[201] = engine::Display{"common:1301", "", "texture_a", "", "34:34:0:Shared"};
    engine::displays[202] = engine::Display{"common:1302", "", "texture_b", "", "34:34:0:Shared"};
    fixture.Change(3, 201);
    fixture.Change(5, 202);
    fixture.cmo.put(offset::kOffCmoSceneNode, fixture.scene.data());
    Update();
    const auto& entries = ext::g_attached.at(fixture.cmo.data());
    Require(entries.size() == 2 && entries[0].renderCtx && entries[1].renderCtx,
            "synchronous collection groups failed to attach");
    Require(std::strcmp(entries[0].keyBuf, entries[1].keyBuf) == 0,
            "same-basename regression fixture did not share its real model path");
    Require(entries[0].renderCtx != entries[1].renderCtx,
            "distinct collection texture/filter groups shared a render context");
    for (const auto& entry : entries) {
        const auto& record = engine::Find(entry.renderCtx);
        const uint32_t wanted = entry.geoFilter.ids[0];
        Require(record.indices[0] == 1, "collection filter removed the base geoset");
        Require((record.indices[3] != 0) == (wanted == 1301),
                "synchronous finalize mixed geoset 1301 from another texture/filter group");
        Require((record.indices[6] != 0) == (wanted == 1302),
                "synchronous finalize mixed geoset 1302 from another texture/filter group");
    }
}

void InvalidatedGroupQueuesWholeCharacter() {
    Fixture fixture;
    void* invalidated = fixture.Seed(2, 34, "first.mdx");
    void* sibling = fixture.Seed(3, 34, "second.mdx");
    engine::Find(invalidated).memory.put(offset::kOffInstModel, static_cast<void*>(nullptr));
    engine::operations.clear();
    const event::M2PerFrameUpdateArgs args{invalidated};
    event::Emit(event::Event::OnM2PerFrameUpdate, &args);
    Require(!engine::MutatedScene(), "invalidated-model callback changed scene during native traversal");
    Require(ext::g_pendingRebuilds.count(fixture.cmo.data()) == 1,
            "invalidated-model callback did not queue the character rebuild");
    Require(engine::HasAttachment(fixture.scene.data(), 34, sibling),
            "invalidated-model callback removed its shared-attachment sibling");
    Update();
    const auto& entries = ext::g_attached.at(fixture.cmo.data());
    Require(entries.size() == 2, "invalidated-model rebuild lost a logical model group");
    for (const auto& entry : entries) {
        Require(entry.renderCtx && engine::HasAttachment(fixture.scene.data(), 34, entry.renderCtx),
                "invalidated-model rebuild did not restore both attachment groups");
        Require(engine::Find(entry.renderCtx).references == 1,
                "invalidated-model rebuild leaked a retained context reference");
    }
}

void OrdinaryTextureVariantsUseDistinctInstances() {
    Fixture fixture;
    fixture.cmo.put(offset::kOffCmoSceneNode, static_cast<void*>(nullptr));
    engine::displays[301] = engine::Display{"common", "", "texture_a", "", "34:34:0:Shared"};
    engine::displays[302] = engine::Display{"common", "", "texture_b", "", "34:34:0:Shared"};
    fixture.Change(2, 301);
    fixture.Change(3, 302);
    fixture.cmo.put(offset::kOffCmoSceneNode, fixture.scene.data());
    Update();
    const auto& entries = ext::g_attached.at(fixture.cmo.data());
    Require(entries.size() == 2 && entries[0].renderCtx && entries[1].renderCtx,
            "ordinary texture variants failed to attach");
    Require(entries[0].geoFilter.count == 0 && entries[1].geoFilter.count == 0,
            "ordinary texture variant regression fixture used collection models");
    Require(std::strcmp(entries[0].keyBuf, entries[1].keyBuf) == 0,
            "ordinary texture variant regression fixture did not share model path");
    Require(entries[0].renderCtx != entries[1].renderCtx,
            "ordinary texture variants shared the same render instance");
    for (const auto& entry : entries)
        Require(engine::Find(entry.renderCtx).texture == entry.texBuf,
                "ordinary model texture binding leaked into the other variant");
}

void SameCollectionOnDifferentAttachmentPointsUsesDistinctInstances() {
    Fixture fixture;
    fixture.cmo.put(offset::kOffCmoSceneNode, static_cast<void*>(nullptr));
    engine::displays[401] = engine::Display{"common:1301", "", "texture_a", "", "6:6:0:Shared"};
    engine::displays[402] = engine::Display{"common:1301", "", "texture_a", "", "5:5:0:Shared"};
    fixture.Change(2, 401);
    fixture.Change(3, 402);
    fixture.cmo.put(offset::kOffCmoSceneNode, fixture.scene.data());
    Update();
    const auto& entries = ext::g_attached.at(fixture.cmo.data());
    Require(entries.size() == 2 && entries[0].renderCtx && entries[1].renderCtx,
            "collection attachment-point variants failed to attach");
    Require(std::strcmp(entries[0].keyBuf, entries[1].keyBuf) == 0 &&
            std::strcmp(entries[0].texBuf, entries[1].texBuf) == 0 &&
            entries[0].geoFilter.ids[0] == entries[1].geoFilter.ids[0] &&
            entries[0].attachId != entries[1].attachId,
            "attachment-point variant fixture differed in model, texture or filter");
    Require(entries[0].renderCtx != entries[1].renderCtx,
            "collection attached to two points reused one scene instance");
    for (const auto& entry : entries)
        Require(engine::HasAttachment(fixture.scene.data(), entry.attachId, entry.renderCtx),
                "collection scene instance was not attached to its own point");
}

void ShortMissingSceneRetainsPendingUiConfiguration() {
    Fixture fixture;
    fixture.Seed(3, 34, "ui_collection.mdx", true);
    fixture.cmo.put(offset::kOffCmoSceneNode, static_cast<void*>(nullptr));
    engine::operations.clear();
    Update(100);
    Update(1000);
    const auto found = ext::g_attached.find(fixture.cmo.data());
    Require(found != ext::g_attached.end() && found->second.size() == 1,
            "short scene absence discarded UI equipment metadata");
    Require(!found->second[0].renderCtx && !found->second[0].subObj,
            "short scene absence kept stale native pointers");
    Require(!engine::MutatedScene(), "short scene absence mutated the old native scene");
    Require(engine::evictions.empty(), "short scene absence evicted the virtual cache too early");
    fixture.cmo.put(offset::kOffCmoSceneNode, fixture.scene.data());
    Update(1001);
    const auto& entry = ext::g_attached.at(fixture.cmo.data())[0];
    Require(entry.renderCtx && engine::HasAttachment(fixture.scene.data(), entry.attachId, entry.renderCtx),
            "UI model did not recover when its scene returned during the grace period");
    Require(ext::g_missingScenes.find(fixture.cmo.data()) == ext::g_missingScenes.end(),
            "restored UI scene remained in missing-scene tracking");
}

void LongMissingSceneExpiresAllCharacterState() {
    Fixture fixture;
    fixture.Seed(2, 34, "removed_ui.mdx", true);
    fixture.Seed(3, 34, "surviving_ui.mdx", true);
    fixture.Clear(3);
    Require(!ext::g_clearRefs.at(fixture.cmo.data()).empty(),
            "missing-scene expiry fixture did not hold native references");
    fixture.cmo.put(offset::kOffCmoSceneNode, static_cast<void*>(nullptr));
    engine::operations.clear();
    Update(100);
    Require(ext::g_clearRefs.find(fixture.cmo.data()) == ext::g_clearRefs.end(),
            "missing scene retained references into the old scene");
    Update(1000);
    Require(ext::g_attached.find(fixture.cmo.data()) != ext::g_attached.end(),
            "missing-scene metadata expired before the grace period");
    Update(30100);
    Require(ext::g_attached.find(fixture.cmo.data()) == ext::g_attached.end() &&
            ext::g_pendingDetaches.find(fixture.cmo.data()) == ext::g_pendingDetaches.end() &&
            ext::g_pendingRebuilds.count(fixture.cmo.data()) == 0 &&
            ext::g_clearRefs.find(fixture.cmo.data()) == ext::g_clearRefs.end() &&
            ext::g_missingScenes.find(fixture.cmo.data()) == ext::g_missingScenes.end(),
            "expired missing scene left character state or queued native references");
    Require(std::find(engine::evictions.begin(), engine::evictions.end(), fixture.cmo.data())
                != engine::evictions.end(), "expired missing scene did not evict virtual cache");
    Require(!engine::MutatedScene(), "missing-scene expiry dereferenced or detached the old native scene");
}

void EmptyMissingSceneExpiresImmediately() {
    Fixture fixture;
    fixture.Seed(3, 34, "last_ui.mdx", true);
    fixture.Clear(4);
    fixture.cmo.put(offset::kOffCmoSceneNode, static_cast<void*>(nullptr));
    engine::operations.clear();
    Update(100);
    Require(ext::g_attached.find(fixture.cmo.data()) == ext::g_attached.end() &&
            ext::g_pendingDetaches.find(fixture.cmo.data()) == ext::g_pendingDetaches.end() &&
            ext::g_pendingRebuilds.count(fixture.cmo.data()) == 0 &&
            ext::g_clearRefs.find(fixture.cmo.data()) == ext::g_clearRefs.end() &&
            ext::g_missingScenes.find(fixture.cmo.data()) == ext::g_missingScenes.end(),
            "empty missing scene waited for timeout or left character state");
    Require(std::find(engine::evictions.begin(), engine::evictions.end(), fixture.cmo.data())
                != engine::evictions.end(), "empty missing scene did not evict virtual cache");
    Require(!engine::MutatedScene(), "empty missing scene detached a stale native scene");
}

void OversizedMergedGeosetGroupDoesNotLoadPartialModel() {
    Fixture fixture;
    ext::AttachEntry first{};
    first.equipSlot = 2;
    first.attachId = 34;
    std::strcpy(first.keyBuf, "common.mdx");
    std::strcpy(first.texBuf, "common.blp");
    first.geoFilter.count = 16;
    for (uint32_t i = 0; i < first.geoFilter.count; ++i)
        first.geoFilter.ids[i] = static_cast<uint16_t>(1301 + i);
    ext::AttachEntry second = first;
    second.equipSlot = 3;
    second.geoFilter.count = 1;
    second.geoFilter.ids[0] = 1317;
    ext::g_attached[fixture.cmo.data()] = {first, second};
    Update();
    Update(32);
    const auto& entries = ext::g_attached.at(fixture.cmo.data());
    Require(entries.size() == 2, "oversized merged filter discarded pending collection metadata");
    for (const auto& entry : entries)
        Require(entry.renderCtx == nullptr && entry.mangledKeyBuf[0] == '\0',
                "oversized merged filter produced a truncated virtual model or live context");
    Require(engine::contexts.empty(), "oversized merged filter loaded a partial or unfiltered model");
    Require(std::none_of(engine::operations.begin(), engine::operations.end(), [](const std::string& operation) {
        return operation.rfind("get:", 0) == 0 || operation.rfind("attach:", 0) == 0;
    }), "oversized merged filter acquired or attached a render instance");
}
}

int main() {
    const std::pair<const char*, void (*)()> cases[] = {
        {"cape/tabard slot mapping and deferred clear", CapeAndTabardUseNativeSlots},
        {"shared attachment reference retention", SharedAttachmentIsRetainedBeforeNativeClear},
        {"last-entry detach and cache eviction", LastEntryDetachesAndEvictsVirtualCache},
        {"blank display replaces old model", EmptyDisplayReplacesOldModel},
        {"pending model attaches after scene initialization", PendingModelSurvivesUninitializedScene},
        {"normal models keep their own bone palette", NormalAttachmentKeepsOwnBonePalette},
        {"paperdoll collection palette without world-scene bit", CollectionPaletteDoesNotRequireWorldSceneBit},
        {"synchronous same-basename collection groups", SynchronousFinalizeSeparatesSameBasenameGroups},
        {"invalidated model defers rebuilding all shared attachment groups", InvalidatedGroupQueuesWholeCharacter},
        {"ordinary texture variants use distinct instances", OrdinaryTextureVariantsUseDistinctInstances},
        {"same collection on different attachment points uses distinct instances", SameCollectionOnDifferentAttachmentPointsUsesDistinctInstances},
        {"short missing UI scene retains pending configuration", ShortMissingSceneRetainsPendingUiConfiguration},
        {"long missing scene expires all character state", LongMissingSceneExpiresAllCharacterState},
        {"empty missing scene expires immediately", EmptyMissingSceneExpiresImmediately},
        {"oversized merged geoset group does not load a partial model", OversizedMergedGeosetGroupDoesNotLoadPartialModel},
    };
    try {
        for (const auto& test : cases) {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
