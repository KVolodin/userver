#pragma once

/// @file userver/engine/io/poller.hpp
/// @brief @copybrief engine::io::Poller

#include <unordered_map>

#include <userver/concurrent/mpsc_queue.hpp>
#include <userver/engine/deadline.hpp>
#include <userver/engine/io/common.hpp>
#include <userver/utils/fast_pimpl.hpp>
#include <userver/utils/flags.hpp>

USERVER_NAMESPACE_BEGIN

namespace engine::io {

/// @brief I/O event monitor.
/// @warning Generally not thread-safe, awaited events should not be changed
/// during active waiting.
/// @note Reports HUP as readiness.
class Poller final {
public:
    /// I/O event.
    struct Event {
        /// I/O event type.
        enum Type {
            kNone = 0,          ///< No active event (or interruption)
            kRead = (1 << 0),   ///< File descriptor is ready for reading
            kWrite = (1 << 1),  ///< File descriptor is ready for writing
            kError = (1 << 2),  ///< File descriptor is in error state (always awaited)
        };

        /// File descriptor responsible for the event
        int fd{kInvalidFd};
        /// Triggered event types
        utils::Flags<Type> type{kNone};
        /// Event epoch, for internal use
        size_t epoch{0};
    };

    /// Event retrieval status
    enum class [[nodiscard]] Status {
        kSuccess,    ///< Received an event
        kInterrupt,  ///< Received an interrupt request
        kNoEvents,   ///< No new events available or task has been cancelled
    };

    Poller();
    ~Poller();

    Poller(const Poller&) = delete;
    Poller(Poller&&) = delete;

    /// Updates a set of events to be monitored for the file descriptor.
    ///
    /// At most one set of events is reported for every `Add` invocation.
    ///
    /// @note Event::Type::kError is implicit when any other event type is
    /// specified.
    void Add(int fd, utils::Flags<Event::Type> events);

    /// Disables event monitoring on a specific file descriptor.
    ///
    /// This function must be called before closing the socket.
    void Remove(int fd);

    /// Waits for the next event and stores it at the provided structure.
    Status NextEvent(Event&, Deadline);

    /// Outputs the next event if immediately available.
    Status NextEventNoblock(Event&);

    /// Emits an event for an invalid fd with empty event types set.
    void Interrupt();

    /// Clears all the watched events and file descriptors
    void Reset();

private:
    explicit Poller(const std::shared_ptr<USERVER_NAMESPACE::concurrent::MpscQueue<Event>>&);

    struct IoWatcher;

    void RemoveImpl(IoWatcher& watcher);

    template <typename EventSource>
    Status EventsFilter(EventSource, Event&);

    USERVER_NAMESPACE::concurrent::MpscQueue<Event>::Consumer event_consumer_;
    USERVER_NAMESPACE::concurrent::MpscQueue<Event>::Producer event_producer_;
    utils::FastPimpl<std::unordered_map<int, IoWatcher>, 56, alignof(double), false> watchers_;
};

}  // namespace engine::io

USERVER_NAMESPACE_END
