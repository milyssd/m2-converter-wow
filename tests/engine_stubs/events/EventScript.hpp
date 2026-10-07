#pragma once
#include "events/Event.hpp"

namespace wxl::events {
namespace detail {
template<class C, class A> struct MemArg { using Class = C; using Arg = A; };
template<class C, class A> MemArg<C, A> MemArgOf(void (C::*)(const A&));
}
class EventScript {
protected:
    virtual ~EventScript() = default;
    template<auto Method> void on(Event event) {
        using Traits = decltype(detail::MemArgOf(Method));
        using Self = typename Traits::Class;
        using Arg = typename Traits::Arg;
        Subscribe(event, [](void* user, const void* args) {
            auto* self = static_cast<Self*>(static_cast<EventScript*>(user));
            (self->*Method)(*static_cast<const Arg*>(args));
        }, this);
    }
};
}
