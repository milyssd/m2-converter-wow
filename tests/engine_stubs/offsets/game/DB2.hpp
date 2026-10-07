#pragma once
#include <cstdint>
#include <cstddef>

namespace test_engine {
uint32_t Lookup(void*, void*, uint32_t, void*);
inline uint32_t raceMin = 1;
inline uint32_t raceMax = 1;
inline const char* racePrefix = "Hum";
inline void* raceRecords[] = {&racePrefix};
inline void* raceTable = raceRecords;
inline const char* genderStrings[] = {"Male", "Female"};
}
namespace wxl::offsets::game::db2 {
namespace itemdisplayinfo {
using LookupFn = uint32_t (*)(void*, void*, uint32_t, void*);
inline const uintptr_t kLookup = reinterpret_cast<uintptr_t>(&test_engine::Lookup);
constexpr uintptr_t kStorageObject = 1;
constexpr size_t kRecordSize = 64;
constexpr size_t kOffModel1 = 8;
constexpr size_t kOffModel2 = 16;
constexpr size_t kOffTex1 = 24;
constexpr size_t kOffTex2 = 32;
constexpr size_t kOffIcon2 = 40;
}
namespace chrraces {
inline const uintptr_t kMinId = reinterpret_cast<uintptr_t>(&test_engine::raceMin);
inline const uintptr_t kMaxId = reinterpret_cast<uintptr_t>(&test_engine::raceMax);
inline const uintptr_t kIdTable = reinterpret_cast<uintptr_t>(&test_engine::raceTable);
constexpr size_t kOffRecordPrefix = 0;
}
namespace genderstrings {
inline const uintptr_t kTable = reinterpret_cast<uintptr_t>(test_engine::genderStrings);
}
}
