#ifndef COMMON_SCOPE_GUARD_H
#define COMMON_SCOPE_GUARD_H

#include <type_traits>
#include <utility>

#include "common_fwd.h"

namespace TOPNSPC {

template <typename F>
class scope_guard {
public:
    scope_guard() = delete;
    scope_guard(const scope_guard &) = delete;
    scope_guard &operator=(const scope_guard &) = delete;

    scope_guard(scope_guard &&other) noexcept
        : f_(std::move(other.f_)), active_(other.active_) {
        other.active_ = false;
    }

    scope_guard &operator=(scope_guard &&other) noexcept {
        if (this != &other) {
            if (active_) {
                std::move(f_)();
            }
            f_ = std::move(other.f_);
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }

    explicit scope_guard(F &&f) noexcept(std::is_nothrow_move_constructible_v<F>)
        : f_(std::move(f)) {}

    explicit scope_guard(const F &f)
        : f_(f) {}

    template <typename... Args>
    explicit scope_guard(std::in_place_t, Args &&...args) noexcept(std::is_nothrow_constructible_v<F, Args &&...>)
        : f_(std::forward<Args>(args)...) {}

    ~scope_guard() noexcept {
        if (active_) {
            std::move(f_)();
        }
    }

    void dismiss() noexcept {
        active_ = false;
    }

private:
    F f_;
    bool active_ = true;
};

template <typename F>
scope_guard(F &&f) -> scope_guard<std::decay_t<F>>;

template <typename F>
auto make_scope_guard(F &&f) {
    return scope_guard<std::decay_t<F>>(std::forward<F>(f));
}

template <typename F, typename... Args>
scope_guard<F> make_scope_guard(std::in_place_type_t<F>, Args &&...args) {
    return scope_guard<F>(std::in_place, std::forward<Args>(args)...);
}

}  // namespace TOPNSPC

#endif  // COMMON_SCOPE_GUARD_H
