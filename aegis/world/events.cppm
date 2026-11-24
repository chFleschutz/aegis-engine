export module aegis.world:events;

import std;

export namespace aegis::world
{
/// @brief An event: a plain struct that systems and scripts send each other within a world. Events are not saved,
///        so a type needs no registration.
template<typename T>
concept Event = std::is_object_v<T> && !std::is_const_v<T> && std::movable<T>;

/// @brief A world's typed event queues, one per event type, double-buffered: what is emitted during a frame is
///        read during the next one, by every system and script, whatever order they run in
/// @note World::update swaps the buffers before anything else runs (deferred commands included), so an event
///       emitted in update N, in a deferred command or in World::begin is read throughout update N+1 and then
///       dropped. World::reset drops every pending and readable event.
class Events
{
public:
    Events() = default;
    Events(const Events&) = delete;
    Events(Events&&) = default;
    auto operator=(const Events&) -> Events& = delete;
    auto operator=(Events&&) -> Events& = default;

    /// @brief Queues 'event' for the next frame
    template<Event T>
    void emit(T event)
    {
        queue<T>().pending.push_back(std::move(event));
    }

    /// @brief The events of type T emitted during the previous frame, in the order they were emitted
    /// @note The span stays valid until the next swap(); emit() doesn't touch it.
    template<Event T>
    [[nodiscard]] auto read() const -> std::span<const T>
    {
        const auto found = m_queues.find(std::type_index{ typeid(T) });
        if (found == m_queues.end())
            return {};
        return static_cast<const Queue<T>&>(*found->second).current;
    }

    /// @brief Makes the pending events of every type readable and drops the ones read so far (World::update)
    void swap()
    {
        for (auto& [type, queue] : m_queues)
            queue->swap();
    }

    /// @brief Drops every pending and readable event (World::reset)
    void clear()
    {
        for (auto& [type, queue] : m_queues)
            queue->clear();
    }

private:
    struct QueueBase
    {
        virtual ~QueueBase() = default;
        virtual void swap() = 0;
        virtual void clear() = 0;
    };

    template<typename T>
    struct Queue final : QueueBase
    {
        std::vector<T> current; // Readable this frame
        std::vector<T> pending; // Emitted this frame

        void swap() override
        {
            current.swap(pending);
            pending.clear();
        }

        void clear() override
        {
            current.clear();
            pending.clear();
        }
    };

    template<typename T>
    auto queue() -> Queue<T>&
    {
        auto& slot = m_queues[std::type_index{ typeid(T) }];
        if (!slot)
            slot = std::make_unique<Queue<T>>();
        return static_cast<Queue<T>&>(*slot);
    }

    std::unordered_map<std::type_index, std::unique_ptr<QueueBase>> m_queues;
};
}
