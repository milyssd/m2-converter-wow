#pragma once
#include <cstdint>
#include <vector>
namespace wxl::runtime::storage
{
    using ClientProvider = bool (*)(const char*, std::vector<uint8_t>&);
    void RegisterClientProvider(ClientProvider provider);
}
