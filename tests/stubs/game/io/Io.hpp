#pragma once
#include <cstdint>
namespace wxl::game::io
{
    bool FileOpen(const char* path, uint32_t flags, void** handle);
    uint32_t FileSize(void* handle, uint32_t* high);
    void FileRead(void* handle, void* output, uint32_t length, uint32_t* got);
    void FileClose(void* handle);
}
