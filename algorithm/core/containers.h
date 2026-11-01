#ifndef ANIMECORE_CONTAINERS_H
#define ANIMECORE_CONTAINERS_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "bridge.h"

namespace AnimeCore {

template <class T>
class Vector
{
    struct Data
    {
        T *buf = nullptr;
        std::ptrdiff_t size = 0;
        std::ptrdiff_t cap = 0;

        Data() noexcept = default;

        explicit Data(std::ptrdiff_t capacity)
        {
            if (capacity > 0) {
                std::allocator<T> alloc;
                buf = alloc.allocate(static_cast<std::size_t>(capacity));
                cap = capacity;
            }
        }

        ~Data()
        {
            if (buf) {
                std::destroy_n(buf, size);
                std::allocator<T> alloc;
                alloc.deallocate(buf, static_cast<std::size_t>(cap));
            }
        }

        Data(const Data &) = delete;
        Data &operator=(const Data &) = delete;
    };

    static const std::shared_ptr<Data> &sharedNull() noexcept
    {
        static const std::shared_ptr<Data> kNull = std::make_shared<Data>(0);
        return kNull;
    }

    static std::ptrdiff_t growCapacity(std::ptrdiff_t currentCap, std::ptrdiff_t minNeeded) noexcept
    {
        if (minNeeded <= currentCap)
            return currentCap;
        std::ptrdiff_t next = currentCap <= 0 ? 4 : currentCap * 2;
        if (next < minNeeded)
            next = minNeeded;
        return next;
    }

public:
    using value_type = T;
    using size_type = std::ptrdiff_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T *;
    using const_pointer = const T *;
    using reference = T &;
    using const_reference = const T &;
    using iterator = T *;
    using const_iterator = const T *;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    Vector() noexcept : m_d(sharedNull()) {}

    explicit Vector(std::ptrdiff_t size)
        : m_d(sharedNull())
    {
        if (size > 0) {
            auto d = std::make_shared<Data>(size);
            std::uninitialized_value_construct_n(d->buf, size);
            d->size = size;
            m_d = std::move(d);
        }
    }

    Vector(std::ptrdiff_t size, const T &val)
        : m_d(sharedNull())
    {
        if (size > 0) {
            auto d = std::make_shared<Data>(size);
            std::uninitialized_fill_n(d->buf, size, val);
            d->size = size;
            m_d = std::move(d);
        }
    }

    Vector(std::initializer_list<T> args)
        : m_d(sharedNull())
    {
        const auto n = static_cast<std::ptrdiff_t>(args.size());
        if (n > 0) {
            auto d = std::make_shared<Data>(n);
            std::uninitialized_copy(args.begin(), args.end(), d->buf);
            d->size = n;
            m_d = std::move(d);
        }
    }

    template <class InputIt,
              std::enable_if_t<std::is_convertible_v<
                  typename std::iterator_traits<InputIt>::iterator_category,
                  std::input_iterator_tag>, int> = 0>
    Vector(InputIt first, InputIt last)
        : m_d(sharedNull())
    {
        for (; first != last; ++first)
            append(*first);
    }

    Vector(const Vector &other) noexcept = default;
    Vector &operator=(const Vector &other) noexcept = default;

    Vector(Vector &&other) noexcept
        : m_d(std::move(other.m_d))
    {
        other.m_d = sharedNull();
    }

    Vector &operator=(Vector &&other) noexcept
    {
        if (this != &other) {
            m_d = std::move(other.m_d);
            other.m_d = sharedNull();
        }
        return *this;
    }

    Vector &operator=(std::initializer_list<T> args)
    {
        Vector tmp(args);
        swap(tmp);
        return *this;
    }

    ~Vector() = default;

    template <class F, std::enable_if_t<BridgesToCore<F, Vector<T>>::value, int> = 0>
    Vector(const F &f) : Vector(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Vector<T>>::value, int> = 0>
    operator F() const { return Bridge<F>::fromCore(*this); }

    bool isSharedWith(const Vector &other) const noexcept
    {
        return m_d == other.m_d;
    }

    bool isDetached() const noexcept
    {
        return m_d.use_count() == 1;
    }

    void detach()
    {
        if (m_d.use_count() > 1)
            reallocateData(m_d->size, m_d->cap > m_d->size ? m_d->cap : m_d->size);
    }

    std::ptrdiff_t size() const noexcept { return m_d->size; }
    std::ptrdiff_t count() const noexcept { return m_d->size; }
    std::ptrdiff_t length() const noexcept { return m_d->size; }
    bool isEmpty() const noexcept { return m_d->size == 0; }
    bool empty() const noexcept { return m_d->size == 0; }
    std::ptrdiff_t capacity() const noexcept { return m_d->cap; }

    void reserve(std::ptrdiff_t alloc)
    {
        if (alloc > m_d->cap) {
            reallocateData(m_d->size, alloc);
        } else if (m_d.use_count() > 1) {
            reallocateData(m_d->size, std::max(m_d->size, alloc));
        }
    }

    void squeeze()
    {
        if (m_d->size < m_d->cap || m_d.use_count() > 1)
            reallocateData(m_d->size, m_d->size);
    }

    void resize(std::ptrdiff_t newSize)
    {
        if (newSize < 0)
            newSize = 0;
        if (newSize == m_d->size)
            return;

        if (m_d.use_count() > 1 || newSize > m_d->cap) {
            const std::ptrdiff_t copyCount = std::min(m_d->size, newSize);
            auto d = std::make_shared<Data>(newSize);
            std::uninitialized_copy_n(m_d->buf, copyCount, d->buf);
            d->size = copyCount;
            if (newSize > copyCount) {
                std::uninitialized_value_construct_n(d->buf + copyCount, newSize - copyCount);
                d->size = newSize;
            }
            m_d = std::move(d);
            return;
        }

        if (newSize < m_d->size) {
            std::destroy_n(m_d->buf + newSize, m_d->size - newSize);
            m_d->size = newSize;
        } else {
            std::uninitialized_value_construct_n(m_d->buf + m_d->size, newSize - m_d->size);
            m_d->size = newSize;
        }
    }

    void resize(std::ptrdiff_t newSize, const T &val)
    {
        if (newSize < 0)
            newSize = 0;
        if (newSize == m_d->size)
            return;

        const T copyVal(val);
        if (m_d.use_count() > 1 || newSize > m_d->cap) {
            const std::ptrdiff_t copyCount = std::min(m_d->size, newSize);
            auto d = std::make_shared<Data>(newSize);
            std::uninitialized_copy_n(m_d->buf, copyCount, d->buf);
            d->size = copyCount;
            if (newSize > copyCount) {
                std::uninitialized_fill_n(d->buf + copyCount, newSize - copyCount, copyVal);
                d->size = newSize;
            }
            m_d = std::move(d);
            return;
        }

        if (newSize < m_d->size) {
            std::destroy_n(m_d->buf + newSize, m_d->size - newSize);
            m_d->size = newSize;
        } else {
            std::uninitialized_fill_n(m_d->buf + m_d->size, newSize - m_d->size, copyVal);
            m_d->size = newSize;
        }
    }

    void clear()
    {
        if (m_d->size == 0 && m_d->cap == 0)
            return;
        m_d = sharedNull();
    }

    void swap(Vector &other) noexcept
    {
        m_d.swap(other.m_d);
    }

    T *data()
    {
        detach();
        return m_d->buf;
    }

    const T *data() const noexcept { return m_d->buf; }
    const T *constData() const noexcept { return m_d->buf; }

    const T &at(std::ptrdiff_t i) const noexcept
    {
        assert(i >= 0 && i < m_d->size);
        return m_d->buf[i];
    }

    const T &operator[](std::ptrdiff_t i) const noexcept
    {
        assert(i >= 0 && i < m_d->size);
        return m_d->buf[i];
    }

    T &operator[](std::ptrdiff_t i)
    {
        assert(i >= 0 && i < m_d->size);
        detach();
        return m_d->buf[i];
    }

    T value(std::ptrdiff_t i) const
    {
        if (i < 0 || i >= m_d->size)
            return T();
        return m_d->buf[i];
    }

    T value(std::ptrdiff_t i, const T &defaultValue) const
    {
        if (i < 0 || i >= m_d->size)
            return defaultValue;
        return m_d->buf[i];
    }

    T &first()
    {
        assert(!isEmpty());
        detach();
        return m_d->buf[0];
    }

    const T &first() const noexcept
    {
        assert(!isEmpty());
        return m_d->buf[0];
    }

    const T &constFirst() const noexcept
    {
        assert(!isEmpty());
        return m_d->buf[0];
    }

    T &front() { return first(); }
    const T &front() const noexcept { return first(); }

    T &last()
    {
        assert(!isEmpty());
        detach();
        return m_d->buf[m_d->size - 1];
    }

    const T &last() const noexcept
    {
        assert(!isEmpty());
        return m_d->buf[m_d->size - 1];
    }

    const T &constLast() const noexcept
    {
        assert(!isEmpty());
        return m_d->buf[m_d->size - 1];
    }

    T &back() { return last(); }
    const T &back() const noexcept { return last(); }

    iterator begin()
    {
        detach();
        return m_d->buf;
    }

    const_iterator begin() const noexcept { return m_d->buf; }
    const_iterator cbegin() const noexcept { return m_d->buf; }
    const_iterator constBegin() const noexcept { return m_d->buf; }

    iterator end()
    {
        detach();
        return m_d->buf + m_d->size;
    }

    const_iterator end() const noexcept { return m_d->buf + m_d->size; }
    const_iterator cend() const noexcept { return m_d->buf + m_d->size; }
    const_iterator constEnd() const noexcept { return m_d->buf + m_d->size; }

    reverse_iterator rbegin() { return reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    void append(const T &t)
    {
        const T copy(t);
        ensureCapacityForAppend(1);
        new (m_d->buf + m_d->size) T(copy);
        ++m_d->size;
    }

    void append(T &&t)
    {
        T moved(std::move(t));
        ensureCapacityForAppend(1);
        new (m_d->buf + m_d->size) T(std::move(moved));
        ++m_d->size;
    }

    void append(const Vector<T> &other)
    {
        if (other.isEmpty())
            return;
        const Vector<T> hold(other);
        const std::ptrdiff_t addCount = hold.size();
        ensureCapacityForAppend(addCount);
        std::uninitialized_copy_n(hold.m_d->buf, addCount, m_d->buf + m_d->size);
        m_d->size += addCount;
    }

    void push_back(const T &t) { append(t); }
    void push_back(T &&t) { append(std::move(t)); }

    template <class... Args>
    T &emplace_back(Args &&...args)
    {
        T val(std::forward<Args>(args)...);
        ensureCapacityForAppend(1);
        T *ptr = new (m_d->buf + m_d->size) T(std::move(val));
        ++m_d->size;
        return *ptr;
    }

    void prepend(const T &t)
    {
        insert(0, t);
    }

    void prepend(T &&t)
    {
        insert(0, std::move(t));
    }

    void push_front(const T &t) { prepend(t); }
    void push_front(T &&t) { prepend(std::move(t)); }

    iterator insert(std::ptrdiff_t i, const T &t)
    {
        return insert(i, 1, t);
    }

    iterator insert(std::ptrdiff_t i, T &&t)
    {
        assert(i >= 0 && i <= m_d->size);
        if (i < 0)
            i = 0;
        if (i > m_d->size)
            i = m_d->size;

        T moved(std::move(t));
        if (m_d.use_count() > 1 || m_d->size + 1 > m_d->cap) {
            const std::ptrdiff_t newCap = growCapacity(m_d->cap, m_d->size + 1);
            auto d = std::make_shared<Data>(newCap);
            std::uninitialized_copy_n(m_d->buf, i, d->buf);
            d->size = i;
            new (d->buf + i) T(std::move(moved));
            ++d->size;
            std::uninitialized_copy_n(m_d->buf + i, m_d->size - i, d->buf + i + 1);
            d->size = m_d->size + 1;
            m_d = std::move(d);
            return m_d->buf + i;
        }

        if (i == m_d->size) {
            new (m_d->buf + m_d->size) T(std::move(moved));
            ++m_d->size;
        } else {
            new (m_d->buf + m_d->size) T(std::move(m_d->buf[m_d->size - 1]));
            ++m_d->size;
            for (std::ptrdiff_t idx = m_d->size - 2; idx > i; --idx)
                m_d->buf[idx] = std::move(m_d->buf[idx - 1]);
            m_d->buf[i] = std::move(moved);
        }
        return m_d->buf + i;
    }

    iterator insert(std::ptrdiff_t i, std::ptrdiff_t n, const T &t)
    {
        assert(i >= 0 && i <= m_d->size);
        if (n <= 0) {
            detach();
            return m_d->buf + i;
        }
        if (i < 0)
            i = 0;
        if (i > m_d->size)
            i = m_d->size;

        const T copy(t);
        if (m_d.use_count() > 1 || m_d->size + n > m_d->cap) {
            const std::ptrdiff_t newCap = growCapacity(m_d->cap, m_d->size + n);
            auto d = std::make_shared<Data>(newCap);
            std::uninitialized_copy_n(m_d->buf, i, d->buf);
            d->size = i;
            std::uninitialized_fill_n(d->buf + i, n, copy);
            d->size += n;
            std::uninitialized_copy_n(m_d->buf + i, m_d->size - i, d->buf + i + n);
            d->size = m_d->size + n;
            m_d = std::move(d);
            return m_d->buf + i;
        }

        const std::ptrdiff_t oldSize = m_d->size;
        const std::ptrdiff_t elemsAfter = oldSize - i;
        if (elemsAfter == 0) {
            std::uninitialized_fill_n(m_d->buf + oldSize, n, copy);
            m_d->size += n;
        } else {
            auto d = std::make_shared<Data>(m_d->cap);
            std::uninitialized_copy_n(m_d->buf, i, d->buf);
            d->size = i;
            std::uninitialized_fill_n(d->buf + i, n, copy);
            d->size += n;
            std::uninitialized_copy_n(m_d->buf + i, elemsAfter, d->buf + i + n);
            d->size = oldSize + n;
            m_d = std::move(d);
        }
        return m_d->buf + i;
    }

    template <class It,
              std::enable_if_t<std::is_same_v<It, const_iterator> ||
                               std::is_same_v<It, iterator>, int> = 0>
    iterator insert(It before, const T &t)
    {
        const std::ptrdiff_t idx = before - m_d->buf;
        return insert(idx, t);
    }

    template <class It,
              std::enable_if_t<std::is_same_v<It, const_iterator> ||
                               std::is_same_v<It, iterator>, int> = 0>
    iterator insert(It before, std::ptrdiff_t n, const T &t)
    {
        const std::ptrdiff_t idx = before - m_d->buf;
        return insert(idx, n, t);
    }

    void remove(std::ptrdiff_t i, std::ptrdiff_t n = 1)
    {
        assert(i >= 0 && n >= 0 && i + n <= m_d->size);
        if (n <= 0 || i < 0 || i + n > m_d->size)
            return;

        if (m_d.use_count() > 1) {
            const std::ptrdiff_t newSize = m_d->size - n;
            auto d = std::make_shared<Data>(newSize);
            std::uninitialized_copy_n(m_d->buf, i, d->buf);
            d->size = i;
            std::uninitialized_copy_n(m_d->buf + i + n, m_d->size - (i + n), d->buf + i);
            d->size = newSize;
            m_d = std::move(d);
            return;
        }

        const std::ptrdiff_t tailCount = m_d->size - (i + n);
        for (std::ptrdiff_t k = 0; k < tailCount; ++k)
            m_d->buf[i + k] = std::move(m_d->buf[i + n + k]);
        std::destroy_n(m_d->buf + m_d->size - n, n);
        m_d->size -= n;
    }

    void removeAt(std::ptrdiff_t i)
    {
        remove(i, 1);
    }

    void removeFirst()
    {
        assert(!isEmpty());
        remove(0, 1);
    }

    void removeLast()
    {
        assert(!isEmpty());
        remove(m_d->size - 1, 1);
    }

    void pop_back() { removeLast(); }
    void pop_front() { removeFirst(); }

    bool removeOne(const T &t)
    {
        const std::ptrdiff_t idx = indexOf(t);
        if (idx == -1)
            return false;
        removeAt(idx);
        return true;
    }

    std::ptrdiff_t removeAll(const T &t)
    {
        const T copy(t);
        std::ptrdiff_t firstMatch = -1;
        for (std::ptrdiff_t i = 0; i < m_d->size; ++i) {
            if (m_d->buf[i] == copy) {
                firstMatch = i;
                break;
            }
        }
        if (firstMatch < 0)
            return 0;

        detach();
        std::ptrdiff_t writeIdx = firstMatch;
        for (std::ptrdiff_t readIdx = firstMatch + 1; readIdx < m_d->size; ++readIdx) {
            if (!(m_d->buf[readIdx] == copy)) {
                m_d->buf[writeIdx] = std::move(m_d->buf[readIdx]);
                ++writeIdx;
            }
        }
        const std::ptrdiff_t removed = m_d->size - writeIdx;
        std::destroy_n(m_d->buf + writeIdx, removed);
        m_d->size = writeIdx;
        return removed;
    }

    T takeAt(std::ptrdiff_t i)
    {
        assert(i >= 0 && i < m_d->size);
        T res = m_d->buf[i];
        remove(i, 1);
        return res;
    }

    T takeFirst()
    {
        assert(!isEmpty());
        return takeAt(0);
    }

    T takeLast()
    {
        assert(!isEmpty());
        return takeAt(m_d->size - 1);
    }

    iterator erase(const_iterator pos)
    {
        const std::ptrdiff_t idx = pos - m_d->buf;
        remove(idx, 1);
        detach();
        return m_d->buf + idx;
    }

    iterator erase(const_iterator first, const_iterator last)
    {
        const std::ptrdiff_t idx = first - m_d->buf;
        const std::ptrdiff_t n = last - first;
        remove(idx, n);
        detach();
        return m_d->buf + idx;
    }

    Vector<T> &fill(const T &t, std::ptrdiff_t newSize = -1)
    {
        const T copy(t);
        if (newSize >= 0 && newSize != m_d->size) {
            auto d = std::make_shared<Data>(newSize);
            std::uninitialized_fill_n(d->buf, newSize, copy);
            d->size = newSize;
            m_d = std::move(d);
            return *this;
        }
        if (m_d->size == 0)
            return *this;
        detach();
        for (std::ptrdiff_t i = 0; i < m_d->size; ++i)
            m_d->buf[i] = copy;
        return *this;
    }

    bool contains(const T &t) const noexcept
    {
        return indexOf(t) != -1;
    }

    std::ptrdiff_t count(const T &t) const noexcept
    {
        std::ptrdiff_t c = 0;
        for (std::ptrdiff_t i = 0; i < m_d->size; ++i) {
            if (m_d->buf[i] == t)
                ++c;
        }
        return c;
    }

    std::ptrdiff_t indexOf(const T &t, std::ptrdiff_t from = 0) const noexcept
    {
        if (from < 0)
            from = std::max<std::ptrdiff_t>(from + m_d->size, 0);
        if (from < m_d->size) {
            for (std::ptrdiff_t i = from; i < m_d->size; ++i) {
                if (m_d->buf[i] == t)
                    return i;
            }
        }
        return -1;
    }

    std::ptrdiff_t lastIndexOf(const T &t, std::ptrdiff_t from = -1) const noexcept
    {
        if (from < 0)
            from += m_d->size;
        else if (from >= m_d->size)
            from = m_d->size - 1;
        if (from >= 0) {
            for (std::ptrdiff_t i = from; i >= 0; --i) {
                if (m_d->buf[i] == t)
                    return i;
            }
        }
        return -1;
    }

    Vector<T> mid(std::ptrdiff_t pos, std::ptrdiff_t len = -1) const
    {
        const std::ptrdiff_t sz = m_d->size;
        if (pos > sz)
            return Vector<T>();
        if (pos < 0) {
            if (len < 0 || pos + len >= sz) {
                pos = 0;
                len = sz;
            } else if (pos + len <= 0) {
                return Vector<T>();
            } else {
                len += pos;
                pos = 0;
            }
        } else if (len < 0 || sz - pos < len) {
            len = sz - pos;
        }
        if (len <= 0)
            return Vector<T>();
        if (pos == 0 && len == sz)
            return *this;

        Vector<T> res;
        auto d = std::make_shared<Data>(len);
        std::uninitialized_copy_n(m_d->buf + pos, len, d->buf);
        d->size = len;
        res.m_d = std::move(d);
        return res;
    }

    void swapItemsAt(std::ptrdiff_t i, std::ptrdiff_t j)
    {
        assert(i >= 0 && i < m_d->size && j >= 0 && j < m_d->size);
        if (i == j)
            return;
        detach();
        using std::swap;
        swap(m_d->buf[i], m_d->buf[j]);
    }

    void move(std::ptrdiff_t from, std::ptrdiff_t to)
    {
        assert(from >= 0 && from < m_d->size && to >= 0 && to < m_d->size);
        if (from == to)
            return;
        detach();
        T tmp(std::move(m_d->buf[from]));
        if (from < to) {
            for (std::ptrdiff_t i = from; i < to; ++i)
                m_d->buf[i] = std::move(m_d->buf[i + 1]);
        } else {
            for (std::ptrdiff_t i = from; i > to; --i)
                m_d->buf[i] = std::move(m_d->buf[i - 1]);
        }
        m_d->buf[to] = std::move(tmp);
    }

    Vector<T> toVector() const noexcept { return *this; }
    Vector<T> toList() const noexcept { return *this; }

    Vector<T> &operator+=(const Vector<T> &other)
    {
        append(other);
        return *this;
    }

    Vector<T> &operator+=(const T &t)
    {
        append(t);
        return *this;
    }

    Vector<T> &operator+=(T &&t)
    {
        append(std::move(t));
        return *this;
    }

    Vector<T> operator+(const Vector<T> &other) const
    {
        Vector<T> res(*this);
        res.append(other);
        return res;
    }

    Vector<T> &operator<<(const T &t)
    {
        append(t);
        return *this;
    }

    Vector<T> &operator<<(T &&t)
    {
        append(std::move(t));
        return *this;
    }

    Vector<T> &operator<<(const Vector<T> &other)
    {
        append(other);
        return *this;
    }

    friend bool operator==(const Vector<T> &lhs, const Vector<T> &rhs) noexcept
    {
        if (lhs.m_d == rhs.m_d)
            return true;
        if (lhs.m_d->size != rhs.m_d->size)
            return false;
        for (std::ptrdiff_t i = 0; i < lhs.m_d->size; ++i) {
            if (!(lhs.m_d->buf[i] == rhs.m_d->buf[i]))
                return false;
        }
        return true;
    }

    friend bool operator!=(const Vector<T> &lhs, const Vector<T> &rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    void reallocateData(std::ptrdiff_t newSize, std::ptrdiff_t newCap)
    {
        if (newCap < newSize)
            newCap = newSize;
        if (newCap == 0) {
            m_d = sharedNull();
            return;
        }
        auto d = std::make_shared<Data>(newCap);
        const std::ptrdiff_t copyCount = std::min(m_d->size, newSize);
        if (copyCount > 0) {
            std::uninitialized_copy_n(m_d->buf, copyCount, d->buf);
            d->size = copyCount;
        }
        m_d = std::move(d);
    }

    void ensureCapacityForAppend(std::ptrdiff_t addCount)
    {
        const std::ptrdiff_t needed = m_d->size + addCount;
        if (m_d.use_count() > 1 || needed > m_d->cap) {
            const std::ptrdiff_t newCap = growCapacity(m_d->cap, needed);
            reallocateData(m_d->size, newCap);
        }
    }

    std::shared_ptr<Data> m_d;
};

template <class T>
using List = Vector<T>;

template <class K, class V>
class Map
{
    using StdMap = std::map<K, V>;

    static const std::shared_ptr<StdMap> &sharedEmpty()
    {
        static const std::shared_ptr<StdMap> kEmpty = std::make_shared<StdMap>();
        return kEmpty;
    }

public:
    using key_type = K;
    using mapped_type = V;
    using size_type = std::ptrdiff_t;
    using difference_type = std::ptrdiff_t;

    class const_iterator;

    class iterator
    {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = V;
        using difference_type = std::ptrdiff_t;
        using pointer = V *;
        using reference = V &;

        iterator() = default;
        explicit iterator(typename StdMap::iterator it) : m_it(it) {}

        const K &key() const { return m_it->first; }
        V &value() const { return m_it->second; }

        V &operator*() const { return m_it->second; }
        V *operator->() const { return &m_it->second; }

        iterator &operator++() { ++m_it; return *this; }
        iterator operator++(int) { iterator tmp = *this; ++m_it; return tmp; }
        iterator &operator--() { --m_it; return *this; }
        iterator operator--(int) { iterator tmp = *this; --m_it; return tmp; }

        bool operator==(const iterator &other) const { return m_it == other.m_it; }
        bool operator!=(const iterator &other) const { return m_it != other.m_it; }

        typename StdMap::iterator raw() const { return m_it; }

    private:
        friend class const_iterator;
        friend class Map<K, V>;
        typename StdMap::iterator m_it{};
    };

    class const_iterator
    {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = V;
        using difference_type = std::ptrdiff_t;
        using pointer = const V *;
        using reference = const V &;

        const_iterator() = default;
        explicit const_iterator(typename StdMap::const_iterator it) : m_it(it) {}
        const_iterator(const iterator &it) : m_it(it.m_it) {}

        const K &key() const { return m_it->first; }
        const V &value() const { return m_it->second; }

        const V &operator*() const { return m_it->second; }
        const V *operator->() const { return &m_it->second; }

        const_iterator &operator++() { ++m_it; return *this; }
        const_iterator operator++(int) { const_iterator tmp = *this; ++m_it; return tmp; }
        const_iterator &operator--() { --m_it; return *this; }
        const_iterator operator--(int) { const_iterator tmp = *this; --m_it; return tmp; }

        bool operator==(const const_iterator &other) const { return m_it == other.m_it; }
        bool operator!=(const const_iterator &other) const { return m_it != other.m_it; }
        friend bool operator==(const iterator &lhs, const const_iterator &rhs) { return const_iterator(lhs) == rhs; }
        friend bool operator!=(const iterator &lhs, const const_iterator &rhs) { return const_iterator(lhs) != rhs; }
        friend bool operator==(const const_iterator &lhs, const iterator &rhs) { return lhs == const_iterator(rhs); }
        friend bool operator!=(const const_iterator &lhs, const iterator &rhs) { return lhs != const_iterator(rhs); }

        typename StdMap::const_iterator raw() const { return m_it; }

    private:
        friend class Map<K, V>;
        typename StdMap::const_iterator m_it{};
    };

    Map() : m_d(sharedEmpty()) {}

    Map(std::initializer_list<std::pair<const K, V>> list)
        : m_d(list.size() > 0 ? std::make_shared<StdMap>(list) : sharedEmpty())
    {}

    Map(const Map &other) noexcept = default;
    Map &operator=(const Map &other) noexcept = default;

    Map(Map &&other) noexcept
        : m_d(std::move(other.m_d))
    {
        other.m_d = sharedEmpty();
    }

    Map &operator=(Map &&other) noexcept
    {
        if (this != &other) {
            m_d = std::move(other.m_d);
            other.m_d = sharedEmpty();
        }
        return *this;
    }

    ~Map() = default;

    template <class F, std::enable_if_t<BridgesToCore<F, Map<K, V>>::value, int> = 0>
    Map(const F &f) : Map(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Map<K, V>>::value, int> = 0>
    operator F() const { return Bridge<F>::fromCore(*this); }

    bool isSharedWith(const Map &other) const noexcept { return m_d == other.m_d; }
    bool isDetached() const noexcept { return m_d.use_count() == 1; }

    void detach()
    {
        if (m_d.use_count() > 1)
            m_d = std::make_shared<StdMap>(*m_d);
    }

    std::ptrdiff_t size() const noexcept { return static_cast<std::ptrdiff_t>(m_d->size()); }
    std::ptrdiff_t count() const noexcept { return static_cast<std::ptrdiff_t>(m_d->size()); }
    std::ptrdiff_t count(const K &key) const { return m_d->find(key) != m_d->end() ? 1 : 0; }
    bool isEmpty() const noexcept { return m_d->empty(); }
    bool empty() const noexcept { return m_d->empty(); }

    void clear()
    {
        if (m_d->empty())
            return;
        m_d = sharedEmpty();
    }

    void swap(Map &other) noexcept { m_d.swap(other.m_d); }

    bool contains(const K &key) const
    {
        return m_d->find(key) != m_d->end();
    }

    V value(const K &key, const V &defaultValue = V()) const
    {
        auto it = m_d->find(key);
        return it != m_d->end() ? it->second : defaultValue;
    }

    V &operator[](const K &key)
    {
        detach();
        return (*m_d)[key];
    }

    V operator[](const K &key) const
    {
        return value(key);
    }

    iterator insert(const K &key, const V &val)
    {
        const V copyVal(val);
        detach();
        auto [it, inserted] = m_d->insert_or_assign(key, copyVal);
        (void)inserted;
        return iterator(it);
    }

    std::ptrdiff_t remove(const K &key)
    {
        auto it = m_d->find(key);
        if (it == m_d->end())
            return 0;
        detach();
        return static_cast<std::ptrdiff_t>(m_d->erase(key));
    }

    V take(const K &key)
    {
        auto it = m_d->find(key);
        if (it == m_d->end())
            return V();
        V res = it->second;
        detach();
        m_d->erase(key);
        return res;
    }

    iterator erase(const_iterator pos)
    {
        assert(pos != cend());
        if (m_d.use_count() > 1) {
            const K k = pos.key();
            detach();
            auto it = m_d->find(k);
            return iterator(m_d->erase(it));
        }
        return iterator(m_d->erase(pos.raw()));
    }

    Vector<K> keys() const
    {
        Vector<K> res;
        res.reserve(size());
        for (const auto &kv : *m_d)
            res.append(kv.first);
        return res;
    }

    Vector<K> keys(const V &val) const
    {
        Vector<K> res;
        for (const auto &kv : *m_d) {
            if (kv.second == val)
                res.append(kv.first);
        }
        return res;
    }

    Vector<V> values() const
    {
        Vector<V> res;
        res.reserve(size());
        for (const auto &kv : *m_d)
            res.append(kv.second);
        return res;
    }

    const K &firstKey() const
    {
        assert(!isEmpty());
        return m_d->begin()->first;
    }

    const K &lastKey() const
    {
        assert(!isEmpty());
        return m_d->rbegin()->first;
    }

    V &first()
    {
        assert(!isEmpty());
        detach();
        return m_d->begin()->second;
    }

    const V &first() const
    {
        assert(!isEmpty());
        return m_d->begin()->second;
    }

    const V &constFirst() const { return first(); }

    V &last()
    {
        assert(!isEmpty());
        detach();
        return m_d->rbegin()->second;
    }

    const V &last() const
    {
        assert(!isEmpty());
        return m_d->rbegin()->second;
    }

    const V &constLast() const { return last(); }

    iterator begin() { detach(); return iterator(m_d->begin()); }
    const_iterator begin() const noexcept { return const_iterator(m_d->cbegin()); }
    const_iterator cbegin() const noexcept { return const_iterator(m_d->cbegin()); }
    const_iterator constBegin() const noexcept { return const_iterator(m_d->cbegin()); }

    iterator end() { detach(); return iterator(m_d->end()); }
    const_iterator end() const noexcept { return const_iterator(m_d->cend()); }
    const_iterator cend() const noexcept { return const_iterator(m_d->cend()); }
    const_iterator constEnd() const noexcept { return const_iterator(m_d->cend()); }

    iterator find(const K &key)
    {
        detach();
        return iterator(m_d->find(key));
    }

    const_iterator find(const K &key) const
    {
        return const_iterator(m_d->find(key));
    }

    const_iterator constFind(const K &key) const
    {
        return const_iterator(m_d->find(key));
    }

    iterator lowerBound(const K &key)
    {
        detach();
        return iterator(m_d->lower_bound(key));
    }

    const_iterator lowerBound(const K &key) const
    {
        return const_iterator(m_d->lower_bound(key));
    }

    iterator upperBound(const K &key)
    {
        detach();
        return iterator(m_d->upper_bound(key));
    }

    const_iterator upperBound(const K &key) const
    {
        return const_iterator(m_d->upper_bound(key));
    }

    friend bool operator==(const Map &lhs, const Map &rhs)
    {
        if (lhs.m_d == rhs.m_d)
            return true;
        return *lhs.m_d == *rhs.m_d;
    }

    friend bool operator!=(const Map &lhs, const Map &rhs)
    {
        return !(lhs == rhs);
    }

private:
    std::shared_ptr<StdMap> m_d;
};

template <class K, class V>
class Hash
{
    using StdHash = std::unordered_map<K, V>;

    static const std::shared_ptr<StdHash> &sharedEmpty()
    {
        static const std::shared_ptr<StdHash> kEmpty = std::make_shared<StdHash>();
        return kEmpty;
    }

public:
    using key_type = K;
    using mapped_type = V;
    using size_type = std::ptrdiff_t;
    using difference_type = std::ptrdiff_t;

    class const_iterator;

    class iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = V;
        using difference_type = std::ptrdiff_t;
        using pointer = V *;
        using reference = V &;

        iterator() = default;
        explicit iterator(typename StdHash::iterator it) : m_it(it) {}

        const K &key() const { return m_it->first; }
        V &value() const { return m_it->second; }

        V &operator*() const { return m_it->second; }
        V *operator->() const { return &m_it->second; }

        iterator &operator++() { ++m_it; return *this; }
        iterator operator++(int) { iterator tmp = *this; ++m_it; return tmp; }

        bool operator==(const iterator &other) const { return m_it == other.m_it; }
        bool operator!=(const iterator &other) const { return m_it != other.m_it; }

        typename StdHash::iterator raw() const { return m_it; }

    private:
        friend class const_iterator;
        friend class Hash<K, V>;
        typename StdHash::iterator m_it{};
    };

    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = V;
        using difference_type = std::ptrdiff_t;
        using pointer = const V *;
        using reference = const V &;

        const_iterator() = default;
        explicit const_iterator(typename StdHash::const_iterator it) : m_it(it) {}
        const_iterator(const iterator &it) : m_it(it.m_it) {}

        const K &key() const { return m_it->first; }
        const V &value() const { return m_it->second; }

        const V &operator*() const { return m_it->second; }
        const V *operator->() const { return &m_it->second; }

        const_iterator &operator++() { ++m_it; return *this; }
        const_iterator operator++(int) { const_iterator tmp = *this; ++m_it; return tmp; }

        bool operator==(const const_iterator &other) const { return m_it == other.m_it; }
        bool operator!=(const const_iterator &other) const { return m_it != other.m_it; }
        friend bool operator==(const iterator &lhs, const const_iterator &rhs) { return const_iterator(lhs) == rhs; }
        friend bool operator!=(const iterator &lhs, const const_iterator &rhs) { return const_iterator(lhs) != rhs; }
        friend bool operator==(const const_iterator &lhs, const iterator &rhs) { return lhs == const_iterator(rhs); }
        friend bool operator!=(const const_iterator &lhs, const iterator &rhs) { return lhs != const_iterator(rhs); }

        typename StdHash::const_iterator raw() const { return m_it; }

    private:
        friend class Hash<K, V>;
        typename StdHash::const_iterator m_it{};
    };

    Hash() : m_d(sharedEmpty()) {}

    Hash(std::initializer_list<std::pair<const K, V>> list)
        : m_d(list.size() > 0 ? std::make_shared<StdHash>(list) : sharedEmpty())
    {}

    Hash(const Hash &other) noexcept = default;
    Hash &operator=(const Hash &other) noexcept = default;

    Hash(Hash &&other) noexcept
        : m_d(std::move(other.m_d))
    {
        other.m_d = sharedEmpty();
    }

    Hash &operator=(Hash &&other) noexcept
    {
        if (this != &other) {
            m_d = std::move(other.m_d);
            other.m_d = sharedEmpty();
        }
        return *this;
    }

    ~Hash() = default;

    template <class F, std::enable_if_t<BridgesToCore<F, Hash<K, V>>::value, int> = 0>
    Hash(const F &f) : Hash(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Hash<K, V>>::value, int> = 0>
    operator F() const { return Bridge<F>::fromCore(*this); }

    bool isSharedWith(const Hash &other) const noexcept { return m_d == other.m_d; }
    bool isDetached() const noexcept { return m_d.use_count() == 1; }

    void detach()
    {
        if (m_d.use_count() > 1)
            m_d = std::make_shared<StdHash>(*m_d);
    }

    std::ptrdiff_t size() const noexcept { return static_cast<std::ptrdiff_t>(m_d->size()); }
    std::ptrdiff_t count() const noexcept { return static_cast<std::ptrdiff_t>(m_d->size()); }
    std::ptrdiff_t count(const K &key) const { return m_d->find(key) != m_d->end() ? 1 : 0; }
    bool isEmpty() const noexcept { return m_d->empty(); }
    bool empty() const noexcept { return m_d->empty(); }

    void reserve(std::ptrdiff_t sz)
    {
        if (sz <= 0)
            return;
        detach();
        m_d->reserve(static_cast<std::size_t>(sz));
    }

    std::ptrdiff_t capacity() const noexcept
    {
        return static_cast<std::ptrdiff_t>(m_d->bucket_count());
    }

    void squeeze()
    {
        detach();
        m_d->rehash(0);
    }

    void clear()
    {
        if (m_d->empty())
            return;
        m_d = sharedEmpty();
    }

    void swap(Hash &other) noexcept { m_d.swap(other.m_d); }

    bool contains(const K &key) const
    {
        return m_d->find(key) != m_d->end();
    }

    V value(const K &key, const V &defaultValue = V()) const
    {
        auto it = m_d->find(key);
        return it != m_d->end() ? it->second : defaultValue;
    }

    V &operator[](const K &key)
    {
        detach();
        return (*m_d)[key];
    }

    V operator[](const K &key) const
    {
        return value(key);
    }

    iterator insert(const K &key, const V &val)
    {
        const V copyVal(val);
        detach();
        auto [it, inserted] = m_d->insert_or_assign(key, copyVal);
        (void)inserted;
        return iterator(it);
    }

    bool remove(const K &key)
    {
        auto it = m_d->find(key);
        if (it == m_d->end())
            return false;
        detach();
        return m_d->erase(key) > 0;
    }

    V take(const K &key)
    {
        auto it = m_d->find(key);
        if (it == m_d->end())
            return V();
        V res = it->second;
        detach();
        m_d->erase(key);
        return res;
    }

    iterator erase(const_iterator pos)
    {
        assert(pos != cend());
        if (m_d.use_count() > 1) {
            const K k = pos.key();
            detach();
            auto it = m_d->find(k);
            return iterator(m_d->erase(it));
        }
        return iterator(m_d->erase(pos.raw()));
    }

    Vector<K> keys() const
    {
        Vector<K> res;
        res.reserve(size());
        for (const auto &kv : *m_d)
            res.append(kv.first);
        return res;
    }

    Vector<K> keys(const V &val) const
    {
        Vector<K> res;
        for (const auto &kv : *m_d) {
            if (kv.second == val)
                res.append(kv.first);
        }
        return res;
    }

    Vector<V> values() const
    {
        Vector<V> res;
        res.reserve(size());
        for (const auto &kv : *m_d)
            res.append(kv.second);
        return res;
    }

    iterator begin() { detach(); return iterator(m_d->begin()); }
    const_iterator begin() const noexcept { return const_iterator(m_d->cbegin()); }
    const_iterator cbegin() const noexcept { return const_iterator(m_d->cbegin()); }
    const_iterator constBegin() const noexcept { return const_iterator(m_d->cbegin()); }

    iterator end() { detach(); return iterator(m_d->end()); }
    const_iterator end() const noexcept { return const_iterator(m_d->cend()); }
    const_iterator cend() const noexcept { return const_iterator(m_d->cend()); }
    const_iterator constEnd() const noexcept { return const_iterator(m_d->cend()); }

    iterator find(const K &key)
    {
        detach();
        return iterator(m_d->find(key));
    }

    const_iterator find(const K &key) const
    {
        return const_iterator(m_d->find(key));
    }

    const_iterator constFind(const K &key) const
    {
        return const_iterator(m_d->find(key));
    }

    friend bool operator==(const Hash &lhs, const Hash &rhs)
    {
        if (lhs.m_d == rhs.m_d)
            return true;
        return *lhs.m_d == *rhs.m_d;
    }

    friend bool operator!=(const Hash &lhs, const Hash &rhs)
    {
        return !(lhs == rhs);
    }

private:
    std::shared_ptr<StdHash> m_d;
};

template <class T>
class Set
{
    using StdSet = std::unordered_set<T>;

    static const std::shared_ptr<StdSet> &sharedEmpty()
    {
        static const std::shared_ptr<StdSet> kEmpty = std::make_shared<StdSet>();
        return kEmpty;
    }

public:
    using key_type = T;
    using value_type = T;
    using size_type = std::ptrdiff_t;
    using difference_type = std::ptrdiff_t;
    using const_iterator = typename StdSet::const_iterator;
    using iterator = typename StdSet::const_iterator;

    Set() : m_d(sharedEmpty()) {}

    Set(std::initializer_list<T> list)
        : m_d(list.size() > 0 ? std::make_shared<StdSet>(list) : sharedEmpty())
    {}

    template <class InputIt,
              std::enable_if_t<std::is_convertible_v<
                  typename std::iterator_traits<InputIt>::iterator_category,
                  std::input_iterator_tag>, int> = 0>
    Set(InputIt first, InputIt last)
        : m_d(first != last ? std::make_shared<StdSet>(first, last) : sharedEmpty())
    {}

    Set(const Set &other) noexcept = default;
    Set &operator=(const Set &other) noexcept = default;

    Set(Set &&other) noexcept
        : m_d(std::move(other.m_d))
    {
        other.m_d = sharedEmpty();
    }

    Set &operator=(Set &&other) noexcept
    {
        if (this != &other) {
            m_d = std::move(other.m_d);
            other.m_d = sharedEmpty();
        }
        return *this;
    }

    ~Set() = default;

    template <class F, std::enable_if_t<BridgesToCore<F, Set<T>>::value, int> = 0>
    Set(const F &f) : Set(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Set<T>>::value, int> = 0>
    operator F() const { return Bridge<F>::fromCore(*this); }

    bool isSharedWith(const Set &other) const noexcept { return m_d == other.m_d; }
    bool isDetached() const noexcept { return m_d.use_count() == 1; }

    void detach()
    {
        if (m_d.use_count() > 1)
            m_d = std::make_shared<StdSet>(*m_d);
    }

    std::ptrdiff_t size() const noexcept { return static_cast<std::ptrdiff_t>(m_d->size()); }
    std::ptrdiff_t count() const noexcept { return static_cast<std::ptrdiff_t>(m_d->size()); }
    bool isEmpty() const noexcept { return m_d->empty(); }
    bool empty() const noexcept { return m_d->empty(); }

    void reserve(std::ptrdiff_t sz)
    {
        if (sz <= 0)
            return;
        detach();
        m_d->reserve(static_cast<std::size_t>(sz));
    }

    std::ptrdiff_t capacity() const noexcept
    {
        return static_cast<std::ptrdiff_t>(m_d->bucket_count());
    }

    void squeeze()
    {
        detach();
        m_d->rehash(0);
    }

    void clear()
    {
        if (m_d->empty())
            return;
        m_d = sharedEmpty();
    }

    void swap(Set &other) noexcept { m_d.swap(other.m_d); }

    bool contains(const T &val) const
    {
        return m_d->find(val) != m_d->end();
    }

    bool contains(const Set<T> &other) const
    {
        if (other.size() > size())
            return false;
        for (const T &v : *other.m_d) {
            if (!contains(v))
                return false;
        }
        return true;
    }

    iterator insert(const T &val)
    {
        const T copyVal(val);
        detach();
        return m_d->insert(copyVal).first;
    }

    bool remove(const T &val)
    {
        auto it = m_d->find(val);
        if (it == m_d->end())
            return false;
        detach();
        return m_d->erase(val) > 0;
    }

    iterator erase(const_iterator pos)
    {
        assert(pos != cend());
        if (m_d.use_count() > 1) {
            const T val = *pos;
            detach();
            auto it = m_d->find(val);
            return m_d->erase(it);
        }
        return m_d->erase(pos);
    }

    Vector<T> values() const
    {
        Vector<T> res;
        res.reserve(size());
        for (const T &v : *m_d)
            res.append(v);
        return res;
    }

    Vector<T> toList() const { return values(); }

    Set<T> &unite(const Set<T> &other)
    {
        if (other.isEmpty() || m_d == other.m_d)
            return *this;
        const Set<T> hold(other);
        detach();
        for (const T &v : *hold.m_d)
            m_d->insert(v);
        return *this;
    }

    Set<T> &intersect(const Set<T> &other)
    {
        if (isEmpty() || m_d == other.m_d)
            return *this;
        if (other.isEmpty()) {
            clear();
            return *this;
        }
        const Set<T> hold(other);
        detach();
        for (auto it = m_d->begin(); it != m_d->end();) {
            if (!hold.contains(*it))
                it = m_d->erase(it);
            else
                ++it;
        }
        return *this;
    }

    Set<T> &subtract(const Set<T> &other)
    {
        if (isEmpty() || other.isEmpty())
            return *this;
        if (m_d == other.m_d) {
            clear();
            return *this;
        }
        const Set<T> hold(other);
        detach();
        for (const T &v : *hold.m_d)
            m_d->erase(v);
        return *this;
    }

    bool intersects(const Set<T> &other) const
    {
        const bool otherSmaller = other.size() < size();
        const Set<T> &smallRef = otherSmaller ? other : *this;
        const Set<T> &largeRef = otherSmaller ? *this : other;
        for (const T &v : *smallRef.m_d) {
            if (largeRef.contains(v))
                return true;
        }
        return false;
    }

    const_iterator begin() const noexcept { return m_d->cbegin(); }
    const_iterator cbegin() const noexcept { return m_d->cbegin(); }
    const_iterator constBegin() const noexcept { return m_d->cbegin(); }

    const_iterator end() const noexcept { return m_d->cend(); }
    const_iterator cend() const noexcept { return m_d->cend(); }
    const_iterator constEnd() const noexcept { return m_d->cend(); }

    const_iterator find(const T &val) const { return m_d->find(val); }
    const_iterator constFind(const T &val) const { return m_d->find(val); }

    Set<T> &operator|=(const Set<T> &other) { return unite(other); }
    Set<T> &operator|=(const T &val) { insert(val); return *this; }
    Set<T> &operator+=(const Set<T> &other) { return unite(other); }
    Set<T> &operator+=(const T &val) { insert(val); return *this; }
    Set<T> &operator&=(const Set<T> &other) { return intersect(other); }
    Set<T> &operator&=(const T &val)
    {
        const bool keep = contains(val);
        clear();
        if (keep)
            insert(val);
        return *this;
    }
    Set<T> &operator-=(const Set<T> &other) { return subtract(other); }
    Set<T> &operator-=(const T &val) { remove(val); return *this; }
    Set<T> &operator<<(const T &val) { insert(val); return *this; }

    Set<T> operator|(const Set<T> &other) const { Set<T> r(*this); r.unite(other); return r; }
    Set<T> operator+(const Set<T> &other) const { Set<T> r(*this); r.unite(other); return r; }
    Set<T> operator&(const Set<T> &other) const { Set<T> r(*this); r.intersect(other); return r; }
    Set<T> operator-(const Set<T> &other) const { Set<T> r(*this); r.subtract(other); return r; }

    friend bool operator==(const Set &lhs, const Set &rhs)
    {
        if (lhs.m_d == rhs.m_d)
            return true;
        return *lhs.m_d == *rhs.m_d;
    }

    friend bool operator!=(const Set &lhs, const Set &rhs)
    {
        return !(lhs == rhs);
    }

private:
    std::shared_ptr<StdSet> m_d;
};

template <class T>
class Queue : public Vector<T>
{
public:
    using Vector<T>::Vector;

    Queue() = default;
    Queue(const Vector<T> &v) : Vector<T>(v) {}
    Queue(Vector<T> &&v) noexcept : Vector<T>(std::move(v)) {}

    template <class F, std::enable_if_t<BridgesToCore<F, Queue<T>>::value, int> = 0>
    Queue(const F &f) : Vector<T>(Bridge<F>::toCore(f)) {}

    template <class F, std::enable_if_t<BridgesFromCore<F, Queue<T>>::value, int> = 0>
    operator F() const { return Bridge<F>::fromCore(*this); }

    void swap(Queue<T> &other) noexcept
    {
        Vector<T>::swap(other);
    }

    void enqueue(const T &t) { Vector<T>::append(t); }
    void enqueue(T &&t) { Vector<T>::append(std::move(t)); }

    T dequeue() { return Vector<T>::takeFirst(); }

    T &head() { return Vector<T>::first(); }
    const T &head() const noexcept { return Vector<T>::first(); }
};

} // namespace AnimeCore

#endif // ANIMECORE_CONTAINERS_H
