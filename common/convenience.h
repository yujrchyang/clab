#ifndef COMMON_CONVENIENCE_H
#define COMMON_CONVENIENCE_H

#include <boost/optional.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <utility>

#include "common_fwd.h"

namespace TOPNSPC {

template <typename T, typename F>
auto maybe_do(const boost::optional<T> &t, F &&f)
    -> boost::optional<std::result_of_t<F(const std::decay_t<T>)>> {
    if (t)
        return {std::forward<F>(f)(*t)};
    else
        return boost::none;
}

template <typename T, typename F, typename U>
auto maybe_do_or(const boost::optional<T> &t, F &&f, U &&u)
    -> std::result_of_t<F(const std::decay_t<T>)> {
    static_assert(std::is_convertible_v<U, std::result_of_t<F(T)>>,
                  "Alternate value must be convertible to function return type.");
    if (t)
        return std::forward<F>(f)(*t);
    else
        return std::forward<U>(u);
}

template <typename T, typename F>
auto maybe_do(const std::optional<T> &t, F &&f)
    -> std::optional<std::result_of_t<F(const std::decay_t<T>)>> {
    if (t)
        return {std::forward<F>(f)(*t)};
    else
        return std::nullopt;
}

template <typename T, typename F, typename U>
auto maybe_do_or(const std::optional<T> &t, F &&f, U &&u)
    -> std::result_of_t<F(const std::decay_t<T>)> {
    static_assert(std::is_convertible_v<U, std::result_of_t<F(T)>>,
                  "Alternate value must be convertible to function return type.");
    if (t)
        return std::forward<F>(f)(*t);
    else
        return std::forward<U>(u);
}

namespace _convenience {

template <typename... Ts, typename F, std::size_t... Is>
inline void for_each_helper(const std::tuple<Ts...> &t, const F &f,
                            std::index_sequence<Is...>) {
    (f(std::get<Is>(t)), ..., void());
}

template <typename... Ts, typename F, std::size_t... Is>
inline void for_each_helper(std::tuple<Ts...> &t, const F &f,
                            std::index_sequence<Is...>) {
    (f(std::get<Is>(t)), ..., void());
}

template <typename... Ts, typename F, std::size_t... Is>
inline void for_each_helper(const std::tuple<Ts...> &t, F &f,
                            std::index_sequence<Is...>) {
    (f(std::get<Is>(t)), ..., void());
}

template <typename... Ts, typename F, std::size_t... Is>
inline void for_each_helper(std::tuple<Ts...> &t, F &f,
                            std::index_sequence<Is...>) {
    (f(std::get<Is>(t)), ..., void());
}

}  // namespace _convenience

template <typename... Ts, typename F>
inline void for_each(const std::tuple<Ts...> &t, const F &f) {
    _convenience::for_each_helper(t, f, std::index_sequence_for<Ts...>{});
}

template <typename... Ts, typename F>
inline void for_each(std::tuple<Ts...> &t, const F &f) {
    _convenience::for_each_helper(t, f, std::index_sequence_for<Ts...>{});
}

template <typename... Ts, typename F>
inline void for_each(const std::tuple<Ts...> &t, F &f) {
    _convenience::for_each_helper(t, f, std::index_sequence_for<Ts...>{});
}

template <typename... Ts, typename F>
inline void for_each(std::tuple<Ts...> &t, F &f) {
    _convenience::for_each_helper(t, f, std::index_sequence_for<Ts...>{});
}

}  // namespace TOPNSPC

#endif  // COMMON_CONVENIENCE_H
