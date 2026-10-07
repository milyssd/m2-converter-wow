#include "EquipmentConfig.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace wxl::scripts::equipextension::config;

#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #expression); \
    std::exit(1); } } while (false)

namespace
{
    void TestSlotMapping()
    {
        CHECK(kEquipToModelSlot[14] == 10);
        CHECK(kEquipToModelSlot[18] == 9);
        CHECK(std::strcmp(kSlotConfig[10].folder, "Cape") == 0);
        CHECK(kSlotConfig[10].defAttach1 == 12);
        CHECK(kSlotConfig[10].defAttach2 == 12);
        CHECK(std::strcmp(kSlotConfig[9].folder, "Tabard") == 0);
        CHECK(kSlotConfig[9].defAttach1 == 34);
        CHECK(kSlotConfig[9].defAttach2 == 34);
        for (size_t inventory = 0; inventory < 19; ++inventory)
        {
            const uint32_t slot = kEquipToModelSlot[inventory];
            CHECK(slot == kUnhandledSlot || slot < 11);
            if (inventory == 1 || (inventory >= 10 && inventory <= 13) ||
                (inventory >= 15 && inventory <= 17)) CHECK(slot == kUnhandledSlot);
        }
    }

    void TestGeosets()
    {
        GeosetFilter filter;
        CHECK(ParseGeosetFilter("1201,2301,1201,0,65535", filter));
        CHECK(filter.count == 4);
        CHECK(filter.ids[0] == 1201 && filter.ids[1] == 2301);
        CHECK(filter.ids[2] == 0 && filter.ids[3] == 65535);
        CHECK(ParseGeosetFilter("0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15", filter));
        CHECK(filter.count == 16);

        const char* malformed[] = {
            "1201,", ",1201", "1201,,2301", "1201x", "1 2", " 1", "1\n",
            "-1", "+1", "0x10", "65536", "4294967296", "9999999999999999999999999",
            "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16",
            "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1"
        };
        for (const char* value : malformed)
        {
            GeosetFilter previous = filter;
            CHECK(!ParseGeosetFilter(value, filter));
            CHECK(filter.count == previous.count);
            CHECK(std::memcmp(filter.ids, previous.ids, sizeof(filter.ids)) == 0);
        }
        CHECK(ParseGeosetFilter(nullptr, filter) && filter.count == 0);
        CHECK(ParseGeosetFilter("", filter) && filter.count == 0);
    }

    void TestIcon2()
    {
        uint32_t al = 11, ar = 11, bl = 55, br = 55, flags = 7;
        char folder[64] = "default";
        CHECK(ParseIcon2("6|5:47|48:0x60:Armor\\Paladin", &al, &ar, &bl, &br,
                         &flags, folder, sizeof(folder)));
        CHECK(al == 6 && ar == 5 && bl == 47 && br == 48 && flags == 96);
        CHECK(std::strcmp(folder, "Armor\\Paladin") == 0);
        CHECK(ParseIcon2(":34", &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(al == 6 && ar == 5 && bl == 34 && br == 34 && flags == 96);
        CHECK(ParseIcon2("::6", &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(al == 6 && ar == 5 && bl == 34 && br == 34 && flags == 6);
        CHECK(ParseIcon2("11", &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(al == 11 && ar == 11 && bl == 34 && br == 34 && flags == 6);
        CHECK(ParseIcon2("::0XFFFFFFFF", &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(flags == kUnhandledSlot);
        CHECK(ParseIcon2("::4294967295", &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(flags == kUnhandledSlot);
        CHECK(ParseIcon2("60|4294967295:0|60", &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(al == 60 && ar == kUnhandledSlot && bl == 0 && br == 60);
        CHECK(ParseIcon2("4294967295:4294967295", &al, &ar, &bl, &br,
                         &flags, folder, sizeof(folder)));
        CHECK(al == kUnhandledSlot && ar == kUnhandledSlot &&
              bl == kUnhandledSlot && br == kUnhandledSlot);
        CHECK(ParseIcon2("11:34", &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(ParseIcon2("", &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(ParseIcon2(nullptr, &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(ParseIcon2(":::", &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));

        const char* malformed[] = {
            "6|:5:0", "|5:5:0", "1|2|3", "1x", "-1", "4294967296",
            "61", "1000", "60|61", "11:61", "11:60|1000", "4294967294",
            "1:2:garbage", "1:2:4294967296", "1:2:0x100000000", "1:2:0x", "1:2: 6",
            "1:2:0:Folder:Extra", "1:2:0:..", "1:2:0:Armor\\..\\Other",
            "1:2:0:Armor/../Other", "1:2:0:/absolute", "1:2:0:\\absolute",
            "1:2:0:C:\\absolute", "1:2:0:Armor//Other", "1:2:0:Armor/",
            "1:2:0:Armor/.", "1:2:0:Armor/Other.", "1:2:0:Armor/Other ",
            "1:2:0:Armor\nOther", "1:2:0:Armor*Other"
        };
        for (const char* value : malformed)
        {
            const uint32_t savedAl = al, savedAr = ar, savedBl = bl, savedBr = br, savedFlags = flags;
            const std::string savedFolder(folder);
            CHECK(!ParseIcon2(value, &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
            CHECK(al == savedAl && ar == savedAr && bl == savedBl && br == savedBr && flags == savedFlags);
            CHECK(folder == savedFolder);
        }
        const std::string tooLong = "1:2:0:" + std::string(sizeof(folder), 'x');
        CHECK(!ParseIcon2(tooLong.c_str(), &al, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(al == 11 && ar == 11 && bl == 34 && br == 34 && flags == kUnhandledSlot);
        CHECK(std::strcmp(folder, "Armor\\Paladin") == 0);
        CHECK(!ParseIcon2("1", nullptr, &ar, &bl, &br, &flags, folder, sizeof(folder)));
        CHECK(!ParseIcon2(":::Armor", &al, &ar, &bl, &br, &flags, nullptr, 0));
        CHECK(!ParseIcon2(":::Armor", &al, &ar, &bl, &br, &flags, folder, 0));
    }

    void TestModelPaths()
    {
        char path[264];
        CHECK(BuildSlotPath(path, sizeof(path), "helmet.mdx", "Hu", "F", 0, "Head", false));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Head\\helmet_HuF.mdx") == 0);
        CHECK(BuildSlotPath(path, sizeof(path), "helmet.M2", "Hu", "F", 0x40, "Head", false));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Head\\helmet_Hu_F.mdx") == 0);
        CHECK(BuildSlotPath(path, sizeof(path), "armor", "Hu", "F", 0x2, "Chest", true));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Collections\\armor_Hu.mdx") == 0);
        CHECK(BuildSlotPath(path, sizeof(path), "armor", "Hu", "F", 0x44, "Chest", false));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\armor_F.mdx") == 0);
        CHECK(BuildSlotPath(path, sizeof(path), "armor", nullptr, nullptr, 0x6, "Chest", false));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\armor.mdx") == 0);
        CHECK(BuildSlotPath(path, sizeof(path), "palat2\\foo.mdx", "Hu", "F", 0x60,
                            "Chest", true, "Custom/Nested"));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Custom\\Nested\\palat2\\foo\\foo_Hu_F.mdx") == 0);
        CHECK(BuildSlotPath(path, sizeof(path), "palat2/foo.m2", "Hu", "F", 0x26, "Chest", false));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\palat2\\foo\\foo.mdx") == 0);
        const char* badModels[] = { "", ".mdx", "armor.blp", "armor.png", "../armor", "/armor",
                                    "C:\\armor", "foo\\..\\armor", "foo\\\\armor", "foo:1201" };
        for (const char* name : badModels)
        {
            std::strcpy(path, "previous");
            CHECK(!BuildSlotPath(path, sizeof(path), name, "Hu", "F", 0, "Chest", false));
            CHECK(path[0] == '\0');
        }
        CHECK(!BuildSlotPath(path, sizeof(path), "armor", nullptr, "F", 0, "Chest", false));
        CHECK(!BuildSlotPath(path, sizeof(path), "armor", "Hu", "", 0, "Chest", false));
        CHECK(!BuildSlotPath(path, sizeof(path), "armor", "../Hu", "F", 0, "Chest", false));
        CHECK(!BuildSlotPath(path, sizeof(path), "armor", "Hu", "F", 0, "../Chest", false));
    }

    void TestTexturePaths()
    {
        char path[264];
        CHECK(BuildTexPath(path, sizeof(path), "armor.blp", "Hu", "F", 0, "Chest", false));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\armor.blp") == 0);
        CHECK(BuildTexPath(path, sizeof(path), "armor.BLP", "Hu", "F", 0x58, "Chest", true));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Collections\\armor_Hu_F.blp") == 0);
        CHECK(BuildTexPath(path, sizeof(path), "armor.detail", "Hu", "F", 0x8, "Chest", false));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\armor.detail_Hu.blp") == 0);
        CHECK(BuildTexPath(path, sizeof(path), "armor", "Hu", "F", 0x10, "Chest", false));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\armor_F.blp") == 0);
        CHECK(BuildTexPath(path, sizeof(path), "palat2\\armor.blp", "Hu", "F", 0x20,
                           "Chest", false, nullptr, "palat2\\foo.mdx"));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\palat2\\foo\\armor.blp") == 0);
        CHECK(BuildTexPath(path, sizeof(path), "palat2/foo/armor.blp", "Hu", "F", 0x20,
                           "Chest", false, nullptr, "palat2\\foo.m2"));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\palat2\\foo\\armor.blp") == 0);
        CHECK(BuildTexPath(path, sizeof(path), "foo/armor", "Hu", "F", 0x20,
                           "Chest", false, nullptr, "palat2\\foo"));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\palat2\\foo\\armor.blp") == 0);
        CHECK(BuildTexPath(path, sizeof(path), "other/armor", "Hu", "F", 0x20,
                           "Chest", true, nullptr, "palat2\\foo"));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Collections\\palat2\\foo\\other\\armor.blp") == 0);
        CHECK(BuildTexPath(path, sizeof(path), "armor.blp", nullptr, nullptr, 0x20, "Chest", false));
        CHECK(std::strcmp(path, "Item\\ObjectComponents\\Chest\\armor\\armor.blp") == 0);
        CHECK(!BuildTexPath(path, sizeof(path), "../armor", "Hu", "F", 0, "Chest", false));
        CHECK(path[0] == '\0');
        CHECK(!BuildTexPath(path, sizeof(path), "armor", "Hu", "F", 0x20,
                            "Chest", false, nullptr, "model.blp"));
        CHECK(path[0] == '\0');
    }

    void TestBufferLimits()
    {
        struct { char path[32]; char canary[8]; } storage;
        std::memset(&storage, '!', sizeof(storage));
        const std::string longName(5000, 'x');
        CHECK(!BuildSlotPath(storage.path, sizeof(storage.path), longName.c_str(),
                            "Hu", "F", 0x20, "Chest", false));
        CHECK(storage.path[0] == '\0');
        CHECK(!BuildTexPath(storage.path, sizeof(storage.path), longName.c_str(),
                           "Hu", "F", 0x20, "Chest", false));
        CHECK(storage.path[0] == '\0');
        for (char c : storage.canary) CHECK(c == '!');
        char expected[264];
        CHECK(BuildSlotPath(expected, sizeof(expected), "a", nullptr, nullptr, 6, "Chest", false));
        const size_t length = std::strlen(expected);
        CHECK(BuildSlotPath(storage.path, length + 1, "a", nullptr, nullptr, 6, "Chest", false));
        CHECK(std::strcmp(storage.path, expected) == 0);
        CHECK(!BuildSlotPath(storage.path, length, "a", nullptr, nullptr, 6, "Chest", false));
        CHECK(storage.path[0] == '\0');
        storage.path[0] = '!';
        CHECK(!BuildSlotPath(storage.path, 0, "a", nullptr, nullptr, 6, "Chest", false));
        CHECK(storage.path[0] == '!');
        CHECK(!BuildSlotPath(nullptr, 264, "a", nullptr, nullptr, 6, "Chest", false));
        CHECK(!BuildTexPath(nullptr, 264, "a", nullptr, nullptr, 0, "Chest", false));
        CHECK(!BuildTexPath(storage.path, 1, "a", nullptr, nullptr, 0, "Chest", false));
        CHECK(storage.path[0] == '\0');
    }
}

int main()
{
    TestSlotMapping();
    TestGeosets();
    TestIcon2();
    TestModelPaths();
    TestTexturePaths();
    TestBufferLimits();
    std::puts("equipment configuration tests passed");
}
