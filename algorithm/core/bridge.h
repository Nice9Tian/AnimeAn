#ifndef ANIMECORE_BRIDGE_H
#define ANIMECORE_BRIDGE_H

#include <type_traits>
#include <utility>

namespace AnimeCore {

// Implicit conversion between a core type and a type outside the core.
//
// The core never names a foreign type. A foreign library specialises
// Bridge<Foreign> with
//     static Core toCore(const Foreign &);
//     static Foreign fromCore(const Core &);
// and a core type opens its converting constructor and conversion operator
// to anything with such a specialisation. A translation unit that includes no
// bridge header sees no conversions at all, so the core stays free of Qt
// while the Qt program keeps passing its own types at the boundary.
template <class Foreign, class = void>
struct Bridge {};

template <class Foreign, class Core, class = void>
struct BridgesToCore : std::false_type {};

template <class Foreign, class Core>
struct BridgesToCore<Foreign, Core,
                     std::enable_if_t<std::is_same_v<
                         decltype(Bridge<Foreign>::toCore(std::declval<const Foreign &>())),
                         Core>>> : std::true_type {};

template <class Foreign, class Core, class = void>
struct BridgesFromCore : std::false_type {};

template <class Foreign, class Core>
struct BridgesFromCore<Foreign, Core,
                       std::enable_if_t<std::is_same_v<
                           decltype(Bridge<Foreign>::fromCore(std::declval<const Core &>())),
                           Foreign>>> : std::true_type {};

} // namespace AnimeCore

#endif // ANIMECORE_BRIDGE_H
