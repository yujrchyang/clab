#ifndef COMMON_BUFFER_H
#define COMMON_BUFFER_H

#include <string.h>
#include <sys/uio.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "buffer_fwd.h"
#include "cassert.h"
#include "common_fwd.h"
#include "crc32.h"
#include "deleter.h"
#include "inline_memory.h"
#include "page.h"
#include "spinlock.h"

namespace TOPNSPC {

template <typename T>
class DencDumper;

template <class T>
struct nop_delete {
    void operator()(T *) {}
};

template <class T>
struct unique_leakable_ptr : public std::unique_ptr<T, nop_delete<T>> {
    using std::unique_ptr<T, nop_delete<T>>::unique_ptr;
};

namespace buffer {

int get_cached_crc();
int get_cached_crc_adjusted();
int get_missed_crc();
void track_cached_crc(bool b);

class raw;
class raw_malloc;
class raw_static;
class raw_posix_aligned;
class raw_hack_aligned;
class raw_char;
class raw_claimed_char;
class raw_unshareable;
class raw_combined;
class raw_claim_buffer;

unique_leakable_ptr<raw> copy(const char *c, unsigned len);
unique_leakable_ptr<raw> create(unsigned len);
unique_leakable_ptr<raw> create(unsigned len, char c);
unique_leakable_ptr<raw> claim_char(unsigned len, char *buf);
unique_leakable_ptr<raw> create_malloc(unsigned len);
unique_leakable_ptr<raw> claim_malloc(unsigned len, char *buf);
unique_leakable_ptr<raw> create_static(unsigned len, char *buf);
unique_leakable_ptr<raw> create_aligned(unsigned len, unsigned align);
unique_leakable_ptr<raw> create_page_aligned(unsigned len);
unique_leakable_ptr<raw> create_small_page_aligned(unsigned len);
unique_leakable_ptr<raw> claim_buffer(unsigned len, char *buf, deleter del);

class ptr {
    friend class list;

protected:
    raw *_raw;
    unsigned _off, _len;

private:
    void release();

    template <bool is_const>
    class iterator_impl {
        const ptr *bp;
        const char *start;
        const char *pos;
        const char *end_ptr;
        const bool deep;

        iterator_impl(typename std::conditional<is_const, const ptr *, ptr *>::type p,
                      size_t offset, bool d)
            : bp(p),
              start(p->c_str() + offset),
              pos(start),
              end_ptr(p->end_c_str()),
              deep(d) {}

        friend class ptr;

    public:
        using pointer = typename std::conditional<is_const, const char *, char *>::type;
        pointer get_pos_add(size_t n) {
            auto r = pos;
            *this += n;
            return r;
        }
        ptr get_ptr(size_t len) {
            if (deep) {
                return buffer::copy(get_pos_add(len), len);
            } else {
                size_t off = pos - bp->c_str();
                *this += len;
                return ptr(*bp, off, len);
            }
        }

        iterator_impl &operator+=(size_t len);

        const char *get_pos() {
            return pos;
        }
        const char *get_end() {
            return end_ptr;
        }

        size_t get_offset() {
            return pos - start;
        }

        bool end() const {
            return pos == end_ptr;
        }
    };

public:
    using const_iterator = iterator_impl<true>;
    using iterator = iterator_impl<false>;

    ptr() : _raw(nullptr), _off(0), _len(0) {}
    ptr(unique_leakable_ptr<raw> r);
    ptr(unsigned l);
    ptr(const char *d, unsigned l);
    ptr(const ptr &p);
    ptr(ptr &&p) noexcept;
    ptr(const ptr &p, unsigned o, unsigned l);
    ptr(const ptr &p, unique_leakable_ptr<raw> r);
    ptr &operator=(const ptr &p);
    ptr &operator=(ptr &&p) noexcept;
    ~ptr() {
        release();
    }

    bool have_raw() const { return _raw ? true : false; }

    unique_leakable_ptr<raw> clone();
    void swap(ptr &other) noexcept;

    iterator begin(size_t offset = 0) {
        return iterator(this, offset, false);
    }
    const_iterator begin(size_t offset = 0) const {
        return const_iterator(this, offset, false);
    }
    const_iterator cbegin() const {
        return begin();
    }
    const_iterator begin_deep(size_t offset = 0) const {
        return const_iterator(this, offset, true);
    }

    bool is_aligned(unsigned align) const {
        return ((uintptr_t)c_str() & (align - 1)) == 0;
    }
    bool is_page_aligned() const { return is_aligned(page().size); }
    bool is_n_align_sized(unsigned align) const {
        return (length() % align) == 0;
    }
    bool is_n_page_sized() const { return is_n_align_sized(page().size); }
    bool is_partial() const {
        return have_raw() && (start() > 0 || end() < raw_length());
    }

    const char *c_str() const;
    char *c_str();
    const char *end_c_str() const;
    char *end_c_str();
    unsigned length() const { return _len; }
    unsigned offset() const { return _off; }
    unsigned start() const { return _off; }
    unsigned end() const { return _off + _len; }
    unsigned unused_tail_length() const;
    const char &operator[](unsigned n) const;
    char &operator[](unsigned n);

    const char *raw_c_str() const;
    unsigned raw_length() const;
    int raw_nref() const;

    void copy_out(unsigned o, unsigned l, char *dest) const;

    unsigned wasted() const;

    int cmp(const ptr &o) const;
    bool is_zero() const;

    void set_offset(unsigned o) {
        clab_assert(raw_length() >= o);
        _off = o;
    }
    void set_length(unsigned l) {
        clab_assert(raw_length() >= l);
        _len = l;
    }

    unsigned append(char c);
    unsigned append(const char *p, unsigned l);
    unsigned append(std::string_view s) {
        return append(s.data(), s.length());
    }
    void copy_in(unsigned o, unsigned l, const char *src, bool crc_reset = true);
    void zero(bool crc_reset = true);
    void zero(unsigned o, unsigned l, bool crc_reset = true);
    unsigned append_zeros(unsigned l);
};

struct ptr_hook {
    mutable ptr_hook *next;

    ptr_hook() = default;
    ptr_hook(ptr_hook *const next)
        : next(next) {
    }
};

class ptr_node : public ptr_hook, public ptr {
public:
    struct cloner {
        ptr_node *operator()(const ptr_node &clone_this);
    };
    struct disposer {
        void operator()(ptr_node *const delete_this) {
            if (!__builtin_expect(dispose_if_hypercombined(delete_this), 0)) {
                delete delete_this;
            }
        }
    };

    ~ptr_node() = default;

    static std::unique_ptr<ptr_node, disposer>
    create(unique_leakable_ptr<raw> r) {
        return create_hypercombined(std::move(r));
    }
    static std::unique_ptr<ptr_node, disposer>
    create(const unsigned l) {
        return create_hypercombined(buffer::create(l));
    }
    template <class... Args>
    static std::unique_ptr<ptr_node, disposer>
    create(Args &&...args) {
        return std::unique_ptr<ptr_node, disposer>(
            new ptr_node(std::forward<Args>(args)...));
    }

    static ptr_node *copy_hypercombined(const ptr_node &copy_this);

private:
    friend class list;

    template <class... Args>
    ptr_node(Args &&...args) : ptr(std::forward<Args>(args)...) {
    }
    ptr_node(const ptr_node &) = default;

    ptr &operator=(const ptr &p) = delete;
    ptr &operator=(ptr &&p) noexcept = delete;
    ptr_node &operator=(const ptr_node &p) = delete;
    ptr_node &operator=(ptr_node &&p) noexcept = delete;
    void swap(ptr &other) noexcept = delete;
    void swap(ptr_node &other) noexcept = delete;

    static bool dispose_if_hypercombined(ptr_node *delete_this);
    static std::unique_ptr<ptr_node, disposer> create_hypercombined(
        unique_leakable_ptr<raw> r);
};

class list {
public:
    class buffers_t {
        ptr_hook _root;
        ptr_hook *_tail;

    public:
        template <class T>
        class buffers_iterator {
            typename std::conditional<
                std::is_const<T>::value, const ptr_hook *, ptr_hook *>::type cur;
            template <class U>
            friend class buffers_iterator;

        public:
            using value_type = T;
            using reference = typename std::add_lvalue_reference<T>::type;
            using pointer = typename std::add_pointer<T>::type;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;

            template <class U>
            buffers_iterator(U *const p)
                : cur(p) {
            }
            template <class U>
            buffers_iterator(const buffers_iterator<U> &other)
                : cur(other.cur) {
            }
            buffers_iterator() = default;

            T &operator*() const {
                return *reinterpret_cast<T *>(cur);
            }
            T *operator->() const {
                return reinterpret_cast<T *>(cur);
            }

            buffers_iterator &operator++() {
                cur = cur->next;
                return *this;
            }
            buffers_iterator operator++(int) {
                const auto temp(*this);
                ++*this;
                return temp;
            }

            template <class U>
            buffers_iterator &operator=(buffers_iterator<U> &other) {
                cur = other.cur;
                return *this;
            }

            bool operator==(const buffers_iterator &rhs) const {
                return cur == rhs.cur;
            }

            bool operator!=(const buffers_iterator &rhs) const {
                return !(*this == rhs);
            }

            using citer_t = buffers_iterator<typename std::add_const<T>::type>;
            operator citer_t() const {
                return citer_t(cur);
            }
        };

        typedef buffers_iterator<const ptr_node> const_iterator;
        typedef buffers_iterator<ptr_node> iterator;

        typedef const ptr_node &const_reference;
        typedef ptr_node &reference;

        buffers_t()
            : _root(&_root),
              _tail(&_root) {
        }
        buffers_t(const buffers_t &) = delete;
        buffers_t(buffers_t &&other)
            : _root(other._root.next == &other._root ? &_root : other._root.next),
              _tail(other._tail == &other._root ? &_root : other._tail) {
            other._root.next = &other._root;
            other._tail = &other._root;

            _tail->next = &_root;
        }
        buffers_t &operator=(buffers_t &&other) {
            if (&other != this) {
                clear_and_dispose();
                swap(other);
            }
            return *this;
        }

        void push_back(reference item) {
            item.next = &_root;
            _tail->next = &item;
            _tail = &item;
        }

        void push_front(reference item) {
            item.next = _root.next;
            _root.next = &item;
            _tail = _tail == &_root ? &item : _tail;
        }

        iterator erase_after(const_iterator it) {
            const auto *to_erase = it->next;

            it->next = to_erase->next;
            _root.next = _root.next == to_erase ? to_erase->next : _root.next;
            _tail = _tail == to_erase ? (ptr_hook *)&*it : _tail;
            return it->next;
        }

        void insert_after(const_iterator it, reference item) {
            item.next = it->next;
            it->next = &item;
            _root.next = it == static_cast<const buffers_t &>(*this).end() ? &item : _root.next;
            _tail = const_iterator(_tail) == it ? &item : _tail;
        }

        void splice_back(buffers_t &other) {
            if (other.empty()) {
                return;
            }

            other._tail->next = &_root;
            _tail->next = other._root.next;
            _tail = other._tail;

            other._root.next = &other._root;
            other._tail = &other._root;
        }

        bool empty() const { return _tail == &_root; }

        const_iterator begin() const {
            return _root.next;
        }
        const_iterator before_begin() const {
            return &_root;
        }
        const_iterator end() const {
            return &_root;
        }
        iterator begin() {
            return _root.next;
        }
        iterator before_begin() {
            return &_root;
        }
        iterator end() {
            return &_root;
        }

        reference front() {
            return reinterpret_cast<reference>(*_root.next);
        }
        reference back() {
            return reinterpret_cast<reference>(*_tail);
        }
        const_reference front() const {
            return reinterpret_cast<const_reference>(*_root.next);
        }
        const_reference back() const {
            return reinterpret_cast<const_reference>(*_tail);
        }

        void clone_from(const buffers_t &other) {
            clear_and_dispose();
            for (auto &node : other) {
                ptr_node *clone = ptr_node::cloner()(node);
                push_back(*clone);
            }
        }
        void clear_and_dispose() {
            for (auto it = begin(); it != end();) {
                auto &node = *it;
                it = it->next;
                ptr_node::disposer()(&node);
            }
            _root.next = &_root;
            _tail = &_root;
        }
        iterator erase_after_and_dispose(iterator it) {
            auto *to_dispose = &*std::next(it);
            auto ret = erase_after(it);
            ptr_node::disposer()(to_dispose);
            return ret;
        }

        void swap(buffers_t &other) {
            const auto copy_root = _root;
            _root.next =
                other._root.next == &other._root ? &this->_root : other._root.next;
            other._root.next =
                copy_root.next == &_root ? &other._root : copy_root.next;

            const auto copy_tail = _tail;
            _tail = other._tail == &other._root ? &this->_root : other._tail;
            other._tail = copy_tail == &_root ? &other._root : copy_tail;

            _tail->next = &_root;
            other._tail->next = &other._root;
        }
    };

    class iterator;

private:
    buffers_t _buffers;

    ptr_node *_carriage;
    unsigned _len, _num;

    template <bool is_const>
    class iterator_impl {
    protected:
        typedef typename std::conditional<is_const,
                                          const list,
                                          list>::type bl_t;
        typedef typename std::conditional<is_const,
                                          const buffers_t,
                                          buffers_t>::type list_t;
        typedef typename std::conditional<is_const,
                                          typename buffers_t::const_iterator,
                                          typename buffers_t::iterator>::type list_iter_t;
        bl_t *bl;
        list_t *ls;
        list_iter_t p;
        unsigned off;
        unsigned p_off;
        friend class iterator_impl<true>;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = typename std::conditional<is_const, const char, char>::type;
        using difference_type = std::ptrdiff_t;
        using pointer = typename std::add_pointer<value_type>::type;
        using reference = typename std::add_lvalue_reference<value_type>::type;

        iterator_impl()
            : bl(0), ls(0), off(0), p_off(0) {}
        iterator_impl(bl_t *l, unsigned o = 0);
        iterator_impl(bl_t *l, unsigned o, list_iter_t ip, unsigned po)
            : bl(l), ls(&bl->_buffers), p(ip), off(o), p_off(po) {}
        iterator_impl(const list::iterator &i);

        unsigned get_off() const { return off; }

        unsigned get_remaining() const { return bl->length() - off; }

        bool end() const {
            return p == ls->end();
        }
        void seek(unsigned o);
        char operator*() const;
        iterator_impl &operator+=(unsigned o);
        iterator_impl &operator++();
        ptr get_current_ptr() const;
        bool is_pointing_same_raw(const ptr &other) const;

        bl_t &get_bl() const { return *bl; }

        void copy(unsigned len, char *dest);
        void copy(unsigned len, ptr &dest) __attribute__((deprecated));
        void copy_deep(unsigned len, ptr &dest);
        void copy_shallow(unsigned len, ptr &dest);
        void copy(unsigned len, list &dest);
        void copy(unsigned len, std::string &dest);
        void copy_all(list &dest);

        size_t get_ptr_and_advance(size_t want, const char **p);

        uint32_t crc32c(size_t length, uint32_t crc);

        friend bool operator==(const iterator_impl &lhs,
                               const iterator_impl &rhs) {
            return &lhs.get_bl() == &rhs.get_bl() && lhs.get_off() == rhs.get_off();
        }
        friend bool operator!=(const iterator_impl &lhs,
                               const iterator_impl &rhs) {
            return &lhs.get_bl() != &rhs.get_bl() || lhs.get_off() != rhs.get_off();
        }
    };

public:
    typedef iterator_impl<true> const_iterator;

    class iterator : public iterator_impl<false> {
    public:
        iterator() = default;
        iterator(bl_t *l, unsigned o = 0);
        iterator(bl_t *l, unsigned o, list_iter_t ip, unsigned po);
        void copy_in(unsigned len, const char *src, bool crc_reset = true);
        void copy_in(unsigned len, const list &otherl);
    };

    struct reserve_t {
        char *bp_data;
        unsigned *bp_len;
        unsigned *bl_len;
    };

    class contiguous_appender {
        bufferlist &bl;
        bufferlist::reserve_t space;
        char *pos;
        bool deep;

        size_t out_of_band_offset = 0;

        contiguous_appender(bufferlist &bl, size_t len, bool d)
            : bl(bl),
              space(bl.obtain_contiguous_space(len)),
              pos(space.bp_data),
              deep(d) {
        }

        void flush_and_continue() {
            const size_t l = pos - space.bp_data;
            *space.bp_len += l;
            *space.bl_len += l;
            space.bp_data = pos;
        }

        friend class list;
        template <typename Type>
        friend class DencDumper;

    public:
        ~contiguous_appender() {
            flush_and_continue();
        }

        size_t get_out_of_band_offset() const {
            return out_of_band_offset;
        }
        void append(const char *__restrict__ p, size_t l) {
            maybe_inline_memcpy(pos, p, l, 16);
            pos += l;
        }
        char *get_pos_add(size_t len) {
            char *r = pos;
            pos += len;
            return r;
        }
        char *get_pos() const {
            return pos;
        }

        void append(const bufferptr &p) {
            const auto plen = p.length();
            if (!plen) {
                return;
            }
            if (deep) {
                append(p.c_str(), plen);
            } else {
                flush_and_continue();
                bl.append(p);
                space = bl.obtain_contiguous_space(0);
                out_of_band_offset += plen;
            }
        }
        void append(const bufferlist &l) {
            if (deep) {
                for (const auto &p : l._buffers) {
                    append(p.c_str(), p.length());
                }
            } else {
                flush_and_continue();
                bl.append(l);
                space = bl.obtain_contiguous_space(0);
                out_of_band_offset += l.length();
            }
        }

        size_t get_logical_offset() const {
            return out_of_band_offset + (pos - space.bp_data);
        }
    };

    contiguous_appender get_contiguous_appender(size_t len, bool deep = false) {
        return contiguous_appender(*this, len, deep);
    }

    class contiguous_filler {
        friend buffer::list;
        char *pos;

        contiguous_filler(char *const pos) : pos(pos) {}

    public:
        void advance(const unsigned len) {
            pos += len;
        }
        void copy_in(const unsigned len, const char *const src) {
            memcpy(pos, src, len);
            advance(len);
        }
        char *c_str() {
            return pos;
        }
    };
    static_assert(sizeof(contiguous_filler) == sizeof(char *),
                  "contiguous_filler should be no costlier than pointer");

    class page_aligned_appender {
        bufferlist &bl;
        unsigned min_alloc;

        page_aligned_appender(list *l, unsigned min_pages)
            : bl(*l),
              min_alloc(min_pages * page().size) {
        }

        void _refill(size_t len);

        template <class Func>
        void _append_common(size_t len, Func &&impl_f) {
            const auto free_in_last = bl.get_append_buffer_unused_tail_length();
            const auto first_round = std::min(len, free_in_last);
            if (first_round) {
                impl_f(first_round);
            }
            const auto second_round = len - first_round;
            if (second_round) {
                _refill(second_round);
                impl_f(second_round);
            }
        }

        friend class list;

    public:
        void append(const bufferlist &l) {
            bl.append(l);
            bl.obtain_contiguous_space(0);
        }

        void append(const char *buf, size_t entire_len) {
            _append_common(entire_len,
                           [buf, this](const size_t chunk_len) mutable {
                               bl.append(buf, chunk_len);
                               buf += chunk_len;
                           });
        }

        void append_zero(size_t entire_len) {
            _append_common(entire_len, [this](const size_t chunk_len) {
                bl.append_zero(chunk_len);
            });
        }

        void substr_of(const list &bl, unsigned off, unsigned len) {
            for (const auto &bptr : bl.buffers()) {
                if (off >= bptr.length()) {
                    off -= bptr.length();
                    continue;
                }
                const auto round_size = std::min(bptr.length() - off, len);
                append(bptr.c_str() + off, round_size);
                len -= round_size;
                off = 0;
            }
        }
    };

    page_aligned_appender get_page_aligned_appender(unsigned min_pages = 1) {
        return page_aligned_appender(this, min_pages);
    }

private:
    static ptr_node always_empty_bptr;
    ptr_node &refill_append_space(const unsigned len);

    ptr &get_append_buffer() {
        return *_carriage;
    }

public:
    list()
        : _carriage(&always_empty_bptr),
          _len(0),
          _num(0) {
    }
    list(unsigned prealloc)
        : _carriage(&always_empty_bptr),
          _len(0),
          _num(0) {
        reserve(prealloc);
    }

    list(const list &other)
        : _carriage(&always_empty_bptr),
          _len(other._len),
          _num(other._num) {
        _buffers.clone_from(other._buffers);
    }

    list(list &&other) noexcept
        : _buffers(std::move(other._buffers)),
          _carriage(other._carriage),
          _len(other._len),
          _num(other._num) {
        other.clear();
    }

    ~list() {
        _buffers.clear_and_dispose();
    }

    list &operator=(const list &other) {
        if (this != &other) {
            _carriage = &always_empty_bptr;
            _buffers.clone_from(other._buffers);
            _len = other._len;
            _num = other._num;
        }
        return *this;
    }
    list &operator=(list &&other) noexcept {
        _buffers = std::move(other._buffers);
        _carriage = other._carriage;
        _len = other._len;
        _num = other._num;
        other.clear();
        return *this;
    }

    uint64_t get_wasted_space() const;
    unsigned get_num_buffers() const { return _num; }
    const ptr_node &front() const { return _buffers.front(); }
    const ptr_node &back() const { return _buffers.back(); }

    size_t get_append_buffer_unused_tail_length() const {
        return _carriage->unused_tail_length();
    }

    const buffers_t &buffers() const { return _buffers; }
    buffers_t &mut_buffers() { return _buffers; }
    void swap(list &other) noexcept;
    unsigned length() const {
        return _len;
    }

    bool contents_equal(const buffer::list &other) const;
    bool contents_equal(const void *other, size_t length) const;

    bool is_provided_buffer(const char *dst) const;
    bool is_aligned(unsigned align) const;
    bool is_page_aligned() const;
    bool is_n_align_sized(unsigned align) const;
    bool is_n_page_sized() const;
    bool is_aligned_size_and_memory(unsigned align_size,
                                    unsigned align_memory) const;

    bool is_zero() const;

    void clear() noexcept {
        _carriage = &always_empty_bptr;
        _buffers.clear_and_dispose();
        _len = 0;
        _num = 0;
    }
    void push_back(const ptr &bp) {
        if (bp.length() == 0)
            return;
        _buffers.push_back(*ptr_node::create(bp).release());
        _len += bp.length();
        _num += 1;
    }
    void push_back(ptr &&bp) {
        if (bp.length() == 0)
            return;
        _len += bp.length();
        _num += 1;
        _buffers.push_back(*ptr_node::create(std::move(bp)).release());
        _carriage = &always_empty_bptr;
    }
    void push_back(const ptr_node &) = delete;
    void push_back(ptr_node &) = delete;
    void push_back(ptr_node &&) = delete;
    void push_back(std::unique_ptr<ptr_node, ptr_node::disposer> bp) {
        _carriage = bp.get();
        _len += bp->length();
        _num += 1;
        _buffers.push_back(*bp.release());
    }
    void push_back(raw *const r) = delete;
    void push_back(unique_leakable_ptr<raw> r) {
        _buffers.push_back(*ptr_node::create(std::move(r)).release());
        _carriage = &_buffers.back();
        _len += _buffers.back().length();
        _num += 1;
    }

    void zero();
    void zero(unsigned o, unsigned l);

    bool is_contiguous() const;
    void rebuild();
    void rebuild(std::unique_ptr<ptr_node, ptr_node::disposer> nb);
    bool rebuild_aligned(unsigned align);
    bool rebuild_aligned_size_and_memory(unsigned align_size,
                                         unsigned align_memory,
                                         unsigned max_buffers = 0);
    bool rebuild_page_aligned();

    void reserve(size_t prealloc);

    [[deprecated("in favor of operator=(list&&)")]] void claim(list &bl) {
        *this = std::move(bl);
    }
    void claim_append(list &bl);
    void claim_append(list &&bl) {
        claim_append(bl);
    }

    void share(const list &bl) {
        if (this != &bl) {
            clear();
            for (const auto &bp : bl._buffers) {
                _buffers.push_back(*ptr_node::create(bp).release());
            }
            _len = bl._len;
            _num = bl._num;
        }
    }

    iterator begin(size_t offset = 0) {
        return iterator(this, offset);
    }
    iterator end() {
        return iterator(this, _len, _buffers.end(), 0);
    }

    const_iterator begin(size_t offset = 0) const {
        return const_iterator(this, offset);
    }
    const_iterator cbegin(size_t offset = 0) const {
        return begin(offset);
    }
    const_iterator end() const {
        return const_iterator(this, _len, _buffers.end(), 0);
    }

    void append(char c);
    void append(const char *data, unsigned len);
    void append(std::string s) {
        append(s.data(), s.length());
    }
    template <std::size_t N>
    void append(const char (&s)[N]) {
        append(s, N);
    }
    void append(const char *s) {
        append(s, strlen(s));
    }
    void append(std::string_view s) {
        append(s.data(), s.length());
    }
    void append(const ptr &bp);
    void append(ptr &&bp);
    void append(const ptr &bp, unsigned off, unsigned len);
    void append(const list &bl);
    void append(std::istream &in);
    contiguous_filler append_hole(unsigned len);
    void append_zero(unsigned len);
    void prepend_zero(unsigned len);

    reserve_t obtain_contiguous_space(const unsigned len);

    const char &operator[](unsigned n) const;
    char *c_str();
    std::string to_str() const;

    void substr_of(const list &other, unsigned off, unsigned len);

    void splice(unsigned off, unsigned len, list *claim_by = 0);
    void write(int off, int len, std::ostream &out) const;

    void encode_base64(list &o);
    void decode_base64(list &o);

    void write_stream(std::ostream &out) const;
    void hexdump(std::ostream &out, bool trailing_newline = true) const;
    ssize_t pread_file(const char *fn, uint64_t off, uint64_t len, std::string *error);
    int read_file(const char *fn, std::string *error);
    ssize_t read_fd(int fd, size_t len);
    ssize_t recv_fd(int fd, size_t len);
    int write_file(const char *fn, int mode = 0644);
    int write_fd(int fd) const;
    int write_fd(int fd, uint64_t offset) const;
    int send_fd(int fd) const;
    template <typename VectorT>
    void prepare_iov(VectorT *piov) const {
        clab_assert(_num <= IOV_MAX);
        piov->resize(_num);
        unsigned n = 0;
        for (auto &p : _buffers) {
            (*piov)[n].iov_base = (void *)p.c_str();
            (*piov)[n].iov_len = p.length();
            ++n;
        }
    }

    struct iovec_t {
        uint64_t offset;
        uint64_t length;
        std::vector<iovec> iov;
    };
    using iov_vec_t = std::vector<iovec_t>;
    iov_vec_t prepare_iovs() const;

    uint32_t crc32c(uint32_t crc) const;
    void invalidate_crc();

    static list static_from_mem(char *c, size_t l);
    static list static_from_cstring(char *c);
    static list static_from_string(std::string &s);
};

class hash {
    uint32_t crc;

public:
    hash() : crc(0) {}
    hash(uint32_t init) : crc(init) {}

    void update(const buffer::list &bl) {
        crc = bl.crc32c(crc);
    }

    uint32_t digest() {
        return crc;
    }
};

inline bool operator==(const bufferlist &lhs, const bufferlist &rhs) {
    if (lhs.length() != rhs.length())
        return false;
    return std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

inline bool operator<(const bufferlist &lhs, const bufferlist &rhs) {
    auto l = lhs.begin(), r = rhs.begin();
    for (; l != lhs.end() && r != rhs.end(); ++l, ++r) {
        if (*l < *r) return true;
        if (*l > *r) return false;
    }
    return (l == lhs.end()) && (r != rhs.end());
}

inline bool operator<=(const bufferlist &lhs, const bufferlist &rhs) {
    auto l = lhs.begin(), r = rhs.begin();
    for (; l != lhs.end() && r != rhs.end(); ++l, ++r) {
        if (*l < *r) return true;
        if (*l > *r) return false;
    }
    return l == lhs.end();
}

inline bool operator!=(const bufferlist &l, const bufferlist &r) {
    return !(l == r);
}
inline bool operator>(const bufferlist &lhs, const bufferlist &rhs) {
    return rhs < lhs;
}
inline bool operator>=(const bufferlist &lhs, const bufferlist &rhs) {
    return rhs <= lhs;
}

std::ostream &operator<<(std::ostream &out, const buffer::ptr &bp);

std::ostream &operator<<(std::ostream &out, const buffer::raw &r);

std::ostream &operator<<(std::ostream &out, const buffer::list &bl);

inline bufferhash &operator<<(bufferhash &l, const bufferlist &r) {
    l.update(r);
    return l;
}

class raw {
public:
    std::aligned_storage<sizeof(ptr_node),
                         alignof(ptr_node)>::type bptr_storage;

protected:
    char *data;
    unsigned len;

public:
    std::atomic<unsigned> nref{0};
    std::pair<size_t, size_t> last_crc_offset{std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()};
    std::pair<uint32_t, uint32_t> last_crc_val;

    mutable spinlock crc_spinlock;

    explicit raw(unsigned l)
        : data(nullptr), len(l), nref(0) {
    }

    raw(char *c, unsigned l)
        : data(c), len(l), nref(0) {
    }

    virtual ~raw() {
    }

    void _set_len(unsigned l) {
        len = l;
    }

private:
    raw(const raw &other) = delete;
    const raw &operator=(const raw &other) = delete;

public:
    char *get_data() const {
        return data;
    }
    unsigned get_len() const {
        return len;
    }
    virtual raw *clone_empty() = 0;
    unique_leakable_ptr<raw> clone() {
        raw *const c = clone_empty();
        memcpy(c->data, data, len);
        return unique_leakable_ptr<raw>(c);
    }
    bool get_crc(const std::pair<size_t, size_t> &fromto,
                 std::pair<uint32_t, uint32_t> *crc) const {
        std::lock_guard lg(crc_spinlock);
        if (last_crc_offset == fromto) {
            *crc = last_crc_val;
            return true;
        }
        return false;
    }
    void set_crc(const std::pair<size_t, size_t> &fromto,
                 const std::pair<uint32_t, uint32_t> &crc) {
        std::lock_guard lg(crc_spinlock);
        last_crc_offset = fromto;
        last_crc_val = crc;
    }
    void invalidate_crc() {
        std::lock_guard lg(crc_spinlock);
        last_crc_offset.first = std::numeric_limits<size_t>::max();
        last_crc_offset.second = std::numeric_limits<size_t>::max();
    }
};

}  // namespace buffer
}  // namespace TOPNSPC

#endif  // COMMON_BUFFER_H
