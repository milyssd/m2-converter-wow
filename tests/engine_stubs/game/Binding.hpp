#pragma once
#include <cstdint>
namespace wxl::game {
template<class Function> Function Native(uintptr_t address) {
    return reinterpret_cast<Function>(address);
}
}
