#ifndef ANIMEAN_BACKEND_PLATFORM_SIGNALSLOT_H
#define ANIMEAN_BACKEND_PLATFORM_SIGNALSLOT_H

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

template <typename... Args>
class Signal
{
public:
    using ConnectionId = std::size_t;
    using Callback = std::function<void(const Args &...)>;

    Signal() : m_state(new State()) {}

    ~Signal()
    {
        State::release(m_state);
    }

    Signal(const Signal &other) : m_state(other.m_state)
    {
        State::retain(m_state);
    }

    Signal &operator=(const Signal &other)
    {
        if (this != &other) {
            State::retain(other.m_state);
            State::release(m_state);
            m_state = other.m_state;
        }
        return *this;
    }

    Signal(Signal &&other) noexcept : m_state(other.m_state)
    {
        other.m_state = nullptr;
    }

    Signal &operator=(Signal &&other) noexcept
    {
        if (this != &other) {
            State::release(m_state);
            m_state = other.m_state;
            other.m_state = nullptr;
        }
        return *this;
    }

    ConnectionId connect(Callback callback)
    {
        if (!m_state) {
            m_state = new State();
        }
        const ConnectionId id = m_state->nextId++;
        m_state->entries.push_back(Entry{id, std::move(callback)});
        return id;
    }

    template <typename F,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<F>, Callback> &&
                  !std::is_convertible_v<F &&, Callback> &&
                  sizeof...(Args) != 0 &&
                  std::is_invocable_v<F>>>
    ConnectionId connect(F &&fn)
    {
        using FnType = std::decay_t<F>;
        FnType copy(std::forward<F>(fn));
        return connect(Callback([copy = std::move(copy)](const Args &...) mutable { copy(); }));
    }

    template <typename Obj, typename R, typename... MArgs>
    ConnectionId connect(Obj *object, R (Obj::*method)(MArgs...))
    {
        return connect([object, method](const Args &...args) {
            if constexpr (sizeof...(MArgs) == 0) {
                (object->*method)();
            } else {
                (object->*method)(args...);
            }
        });
    }

    bool disconnect(ConnectionId id)
    {
        if (!m_state || id == 0) {
            return false;
        }
        auto &vec = m_state->entries;
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it->id == id) {
                vec.erase(it);
                return true;
            }
        }
        return false;
    }

    void disconnectAll()
    {
        if (m_state) {
            m_state->entries.clear();
        }
    }

    void operator()(const Args &...args) const
    {
        if (!m_state || m_state->entries.empty()) {
            return;
        }
        // Retain state and snapshot current connection IDs so a callback can
        // safely connect, disconnect (including a pending sibling), or destroy
        // the object owning this Signal during emission.
        State *state = m_state;
        State::retain(state);
        struct Guard {
            State *s;
            ~Guard() { State::release(s); }
        } guard{state};

        std::vector<ConnectionId> ids;
        ids.reserve(state->entries.size());
        for (const Entry &e : state->entries) {
            ids.push_back(e.id);
        }

        for (ConnectionId id : ids) {
            Callback cb;
            for (const Entry &e : state->entries) {
                if (e.id == id) {
                    cb = e.fn;
                    break;
                }
            }
            if (cb) {
                cb(args...);
            }
        }
    }

private:
    struct Entry {
        ConnectionId id = 0;
        Callback fn;
    };

    struct State {
        std::vector<Entry> entries;
        ConnectionId nextId = 1;
        std::size_t refCount = 1;

        static void retain(State *s)
        {
            if (s) {
                ++s->refCount;
            }
        }

        static void release(State *s)
        {
            if (s && --s->refCount == 0) {
                delete s;
            }
        }
    };

    State *m_state = nullptr;
};

namespace AnimeCore {
using ::Signal;
} // namespace AnimeCore

#endif // ANIMEAN_BACKEND_PLATFORM_SIGNALSLOT_H
