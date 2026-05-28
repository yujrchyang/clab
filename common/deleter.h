#ifndef COMMON_DELETER_H
#define COMMON_DELETER_H

#include <atomic>
#include <cstdlib>
#include <new>
#include <utility>

#include "common_fwd.h"

namespace TOPNSPC {

class deleter final {
public:
    struct impl;
    struct raw_object_tag {};

private:
    impl *_impl = nullptr;

public:
    deleter() = default;
    deleter(const deleter &) = delete;
    deleter &operator=(const deleter &) = delete;
    deleter(deleter &&x) noexcept : _impl(x._impl) { x._impl = nullptr; }
    explicit deleter(impl *i) : _impl(i) {}
    deleter(raw_object_tag tag, void *object)
        : _impl(from_raw_object(object)) {}
    ~deleter();
    deleter &operator=(deleter &&x);
    deleter share();

    explicit operator bool() const { return bool(_impl); }

    void reset(impl *i) {
        this->~deleter();
        new (this) deleter(i);
    }

    void append(deleter d);

private:
    static bool is_raw_object(impl *i) {
        auto x = reinterpret_cast<uintptr_t>(i);
        return x & 1;
    }
    bool is_raw_object() const { return is_raw_object(_impl); }
    static void *to_raw_object(impl *i) {
        auto x = reinterpret_cast<uintptr_t>(i);
        return reinterpret_cast<void *>(x & ~uintptr_t(1));
    }
    void *to_raw_object() const { return to_raw_object(_impl); }
    impl *from_raw_object(void *object) {
        auto x = reinterpret_cast<uintptr_t>(object);
        return reinterpret_cast<impl *>(x | 1);
    }
};

struct deleter::impl {
    std::atomic_uint refs;
    deleter next;
    impl(deleter next) : refs(1), next(std::move(next)) {}
    virtual ~impl() {}
};

inline deleter::~deleter() {
    if (is_raw_object()) {
        std::free(to_raw_object());
        return;
    }
    if (_impl && --_impl->refs == 0) {
        delete _impl;
    }
}

inline deleter &deleter::operator=(deleter &&x) {
    if (this != &x) {
        this->~deleter();
        new (this) deleter(std::move(x));
    }
    return *this;
}

template <typename Deleter>
struct lambda_deleter_impl final : deleter::impl {
    Deleter del;
    lambda_deleter_impl(deleter next, Deleter &&del)
        : impl(std::move(next)), del(std::move(del)) {}
    ~lambda_deleter_impl() override { del(); }
};

template <typename Object>
struct object_deleter_impl final : deleter::impl {
    Object obj;
    object_deleter_impl(deleter next, Object &&obj)
        : impl(std::move(next)), obj(std::move(obj)) {}
};

template <typename Object>
inline object_deleter_impl<Object> *make_object_deleter_impl(deleter next,
                                                             Object obj) {
    return new object_deleter_impl<Object>(std::move(next), std::move(obj));
}

template <typename Object>
deleter make_deleter(deleter next, Object o) {
    return deleter(
        new lambda_deleter_impl<Object>(std::move(next), std::move(o)));
}

template <typename Object>
deleter make_deleter(Object o) {
    return make_deleter(deleter(), std::move(o));
}

struct free_deleter_impl final : deleter::impl {
    void *obj;
    free_deleter_impl(void *obj) : impl(deleter()), obj(obj) {}
    ~free_deleter_impl() override { std::free(obj); }
};

inline deleter deleter::share() {
    if (!_impl) {
        return deleter();
    }
    if (is_raw_object()) {
        _impl = new free_deleter_impl(to_raw_object());
    }
    ++_impl->refs;
    return deleter(_impl);
}

inline void deleter::append(deleter d) {
    if (!d._impl) {
        return;
    }
    impl *next_impl = _impl;
    deleter *next_d = this;

    while (next_impl) {
        if (next_impl == d._impl)
            return;
        if (is_raw_object(next_impl)) {
            next_d->_impl =
                next_impl = new free_deleter_impl(to_raw_object(next_impl));
        }
        if (next_impl->refs != 1) {
            next_d->_impl = next_impl = make_object_deleter_impl(
                std::move(next_impl->next), deleter(next_impl));
        }
        next_d = &next_impl->next;
        next_impl = next_d->_impl;
    }

    next_d->_impl = d._impl;
    d._impl = nullptr;
}

inline deleter make_free_deleter(void *obj) {
    if (!obj) {
        return deleter();
    }
    return deleter(deleter::raw_object_tag(), obj);
}

inline deleter make_free_deleter(deleter next, void *obj) {
    return make_deleter(std::move(next),
                        [obj]() mutable { std::free(obj); });
}

template <typename T>
inline deleter make_object_deleter(T &&obj) {
    return deleter{
        make_object_deleter_impl(deleter(), std::move(obj))};
}

template <typename T>
inline deleter make_object_deleter(deleter d, T &&obj) {
    return deleter{
        make_object_deleter_impl(std::move(d), std::move(obj))};
}

}  // namespace TOPNSPC

#endif  // COMMON_DELETER_H
