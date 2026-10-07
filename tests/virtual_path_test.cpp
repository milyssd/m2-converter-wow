// Portable regression tests for the virtual provider; game I/O is mocked.
#include "VirtualPath.hpp"
#include "Settings.hpp"
#include "runtime/storage/StorageHook.hpp"
#include "game/io/Io.hpp"

#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
    wxl::runtime::storage::ClientProvider provider = nullptr;
    std::unordered_map<std::string, std::vector<uint8_t>> archive;
    unsigned opens = 0;
    bool shortRead = false;
    bool oversizedFile = false;

    void Write32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        assert(offset + 4 <= bytes.size());
        std::memcpy(bytes.data() + offset, &value, 4);
    }

    uint32_t Read32(const std::vector<uint8_t>& bytes, size_t offset)
    {
        assert(offset + 4 <= bytes.size());
        uint32_t value;
        std::memcpy(&value, bytes.data() + offset, 4);
        return value;
    }

    std::vector<uint8_t> Model(uint32_t skinCount = 1)
    {
        std::vector<uint8_t> bytes(336);
        std::memcpy(bytes.data(), "MD20", 4);
        Write32(bytes, 4, 264);
        Write32(bytes, 68, skinCount);
        Write32(bytes, 80, 2);
        Write32(bytes, 84, 304);
        const std::string path = "Item\\ObjectComponents\\Chest\\Shared.BLP";
        Write32(bytes, 312, static_cast<uint32_t>(path.size() + 1));
        Write32(bytes, 316, static_cast<uint32_t>(bytes.size()));
        Write32(bytes, 320, 11); // replaceable texture: it must remain untouched
        bytes.insert(bytes.end(), path.begin(), path.end());
        bytes.push_back(0);
        return bytes;
    }

    std::vector<uint8_t> Skin()
    {
        std::vector<uint8_t> bytes(48);
        std::memcpy(bytes.data(), "SKIN", 4);
        return bytes;
    }

    std::string Key(void* owner, const char* model, const uint16_t* ids,
                    uint32_t count, const char* texture = "", bool relocate = false,
                    uint32_t attachmentId = UINT32_MAX)
    {
        char result[264];
        assert(wxl::scripts::equipextension::VPathBuildKey(
            result, sizeof(result), owner, model, ids, count, texture, relocate, attachmentId));
        return result;
    }

    std::string SkinKey(std::string modelKey, const char* index)
    {
        modelKey.resize(modelKey.size() - 3);
        return modelKey + index + ".skin";
    }
}

namespace wxl::runtime::storage
{
    void RegisterClientProvider(ClientProvider callback) { provider = callback; }
}

namespace wxl::game::io
{
    bool FileOpen(const char* path, uint32_t, void** handle)
    {
        ++opens;
        auto it = archive.find(path);
        *handle = it == archive.end() ? nullptr : &it->second;
        return *handle != nullptr;
    }
    uint32_t FileSize(void* handle, uint32_t* high)
    {
        *high = oversizedFile ? 1 : 0;
        return static_cast<uint32_t>(static_cast<std::vector<uint8_t>*>(handle)->size());
    }
    void FileRead(void* handle, void* output, uint32_t length, uint32_t* got)
    {
        auto& bytes = *static_cast<std::vector<uint8_t>*>(handle);
        *got = shortRead && length ? length - 1 : length;
        std::memcpy(output, bytes.data(), *got);
    }
    void FileClose(void*) {}
}

namespace wxl::scripts::equipextension::settings
{
    BoolSetting::BoolSetting(const char*, const char*, const char*, bool defaultValue) noexcept
        : value_(defaultValue) {}
}

int main()
{
    using namespace wxl::scripts::equipextension;
    assert(provider);
    void* owner = reinterpret_cast<void*>(std::numeric_limits<uintptr_t>::max());
    void* other = reinterpret_cast<void*>(uintptr_t(0x12345));
    const char* path = "Item/ObjectComponents/Chest/Set/Test_HuF.MDX";
    const uint16_t ids[] = {901, 103, 901, 0};
    const uint16_t sorted[] = {0, 103, 901};
    std::string key = Key(owner, path, ids, 4, "Textures/SetA/Shared.BLP", true);
    assert(key == Key(owner, "item\\objectcomponents\\chest\\set\\test_huf.m2",
                      sorted, 3, "textures\\seta\\shared.blp", true));
    assert(key != Key(owner, path, ids, 4, "Textures/SetB/Shared.BLP", true));
    assert(key != Key(owner, path, ids, 4, "Textures/SetA/Shared.BLP", false));
    assert(key == Key(owner, path, ids, 4, "Textures/SetA/Shared.BLP", true, UINT32_MAX));
    assert(Key(owner, path, ids, 4, "", true, 19) != Key(owner, path, ids, 4, "", true, 53));
    assert(key.find("_wxl_0_103_901_") != std::string::npos);
    assert(Key(owner, "Folder.V1/NoExtension", nullptr, 0).find("folder.v1\\noextension_wxl_") == 0);

    char output[264] = "old";
    char tiny[2] = {'x', 'x'};
    assert(!VPathBuildKey(tiny, sizeof(tiny), owner, path, ids, 4, ""));
    assert(tiny[0] == 0);
    assert(!VPathBuildKey(nullptr, sizeof(output), owner, path, ids, 4, ""));
    assert(!VPathBuildKey(output, sizeof(output), owner, nullptr, ids, 4, ""));
    assert(!VPathBuildKey(output, sizeof(output), owner, path, nullptr, 1, ""));
    assert(!VPathBuildKey(output, sizeof(output), owner, path, ids, 17, ""));
    assert(!VPathBuildKey(output, sizeof(output), owner, std::string(300, 'x').c_str(), nullptr, 0, ""));
    assert(output[0] == 0);

    const std::string real = "item\\objectcomponents\\chest\\set\\test_huf.m2";
    const std::string realStem = real.substr(0, real.size() - 3);
    archive[real] = Model(2);
    archive[realStem + "00.skin"] = Skin();
    std::vector<uint8_t> served;
    // Missing declared LOD must never publish a partially populated model.
    assert(!VPathPopulate(owner, path, ids, 4, "Textures/SetA/Shared.BLP", true));
    assert(!provider(key.c_str(), served));
    archive[realStem + "01.skin"] = Skin();
    assert(VPathPopulate(owner, path, ids, 4, "Textures/SetA/Shared.BLP", true));
    assert(provider(key.c_str(), served));
    assert(served.size() > archive[real].size());
    assert(Read32(served, 4) == 264 && Read32(served, 68) == 2);
    assert(Read32(served, 320) == 11 && Read32(served, 328) == 0);
    uint32_t filenameOffset = Read32(served, 316);
    assert(std::string(reinterpret_cast<const char*>(served.data() + filenameOffset)) ==
           "item\\objectcomponents\\chest\\set\\shared.blp");
    assert(Read32(served, 312) == std::strlen(reinterpret_cast<const char*>(served.data() + filenameOffset)) + 1);
    assert(provider(SkinKey(key, "00").c_str(), served));
    assert(served == Skin());
    assert(provider(SkinKey(key, "01").c_str(), served));
    unsigned previousOpens = opens;
    assert(VPathPopulate(owner, path, sorted, 3, "textures\\seta\\shared.blp", true));
    assert(opens == previousOpens);
    std::string upper = key;
    for (char& c : upper) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (char& c : upper) if (c == '\\') c = '/';
    assert(provider(upper.c_str(), served));

    // Ordinary subfolder models have no geoset filter but need identical relocation.
    std::string otherKey = Key(other, path, nullptr, 0, "", true);
    assert(VPathPopulate(other, path, nullptr, 0, "", true));
    assert(provider(otherKey.c_str(), served));
    // Separate attachment points need independent render-cache model identities,
    // even when every other property (owner/model/filter/texture) is equal.
    std::string groundKey = Key(other, path, nullptr, 0, "", true, 19);
    std::string beltKey = Key(other, path, nullptr, 0, "", true, 53);
    assert(groundKey != beltKey && groundKey != otherKey);
    assert(VPathPopulate(other, path, nullptr, 0, "", true, 19));
    assert(VPathPopulate(other, path, nullptr, 0, "", true, 53));
    assert(provider(groundKey.c_str(), served));
    assert(provider(beltKey.c_str(), served));
    assert(provider(SkinKey(groundKey, "00").c_str(), served));
    assert(provider(SkinKey(beltKey, "00").c_str(), served));
    VPathEvictCmo(owner);
    assert(!provider(key.c_str(), served));
    assert(!provider(SkinKey(key, "00").c_str(), served));
    assert(!provider(SkinKey(key, "01").c_str(), served));
    assert(provider(otherKey.c_str(), served));
    VPathEvictCmo(other);
    assert(!provider(otherKey.c_str(), served));
    assert(!provider(groundKey.c_str(), served));
    assert(!provider(beltKey.c_str(), served));
    VPathEvictCmo(other); // repeated eviction is harmless

    // Invalid filename ranges and unterminated paths must fail before publication.
    auto bad = Model();
    Write32(bad, 316, std::numeric_limits<uint32_t>::max());
    archive[real] = bad;
    assert(!VPathPopulate(owner, path, nullptr, 0, "", true));
    bad = Model();
    bad.back() = 'x';
    archive[real] = bad;
    assert(!VPathPopulate(owner, path, nullptr, 0, "", true));
    bad = Model();
    Write32(bad, 80, std::numeric_limits<uint32_t>::max());
    archive[real] = bad;
    assert(!VPathPopulate(owner, path, nullptr, 0, "", true));
    bad = Model();
    Write32(bad, 4, 272); // modern client model version
    archive[real] = bad;
    assert(!VPathPopulate(owner, path, nullptr, 0, "", true));
    archive[real] = Model();
    shortRead = true;
    assert(!VPathPopulate(owner, path, nullptr, 0, "", true));
    shortRead = false;
    oversizedFile = true;
    assert(!VPathPopulate(owner, path, nullptr, 0, "", true));
    oversizedFile = false;
    assert(VPathPopulate(owner, path, nullptr, 0, "", true));
    VPathEvictCmo(owner);

    // Without relocation, original descriptor bytes are preserved exactly.
    std::string unmodifiedKey = Key(owner, path, sorted, 3);
    assert(VPathPopulate(owner, path, sorted, 3, ""));
    assert(provider(unmodifiedKey.c_str(), served));
    assert(served == archive[real]);
    VPathEvictCmo(owner);
    assert(!provider(nullptr, served));
    assert(!provider("unrelated.m2", served));

    // Archive hooks run on client loader threads while the main thread changes
    // equipment. Copies must stay valid across concurrent publication/eviction.
    std::atomic<bool> stop{false};
    std::atomic<unsigned> iterations{0};
    std::thread loader([&]
    {
        std::vector<uint8_t> modelBytes, skinBytes;
        while (!stop.load())
        {
            if (provider(groundKey.c_str(), modelBytes))
            {
                assert(modelBytes.size() >= 304);
                assert(std::memcmp(modelBytes.data(), "MD20", 4) == 0);
                assert(Read32(modelBytes, 4) == 264);
            }
            if (provider(SkinKey(groundKey, "00").c_str(), skinBytes))
                assert(skinBytes == Skin());
            ++iterations;
        }
    });
    while (iterations.load() == 0) std::this_thread::yield();
    for (unsigned i = 0; i < 250; ++i)
    {
        assert(VPathPopulate(other, path, nullptr, 0, "", true, 19));
        VPathEvictCmo(other);
    }
    stop.store(true);
    loader.join();
    assert(iterations.load() > 0);
    assert(!provider(groundKey.c_str(), served));
    std::cout << "Virtual path regression tests passed\n";
}
