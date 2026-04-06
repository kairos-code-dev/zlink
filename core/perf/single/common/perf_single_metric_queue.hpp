#ifndef PERF_SINGLE_METRIC_QUEUE_HPP
#define PERF_SINGLE_METRIC_QUEUE_HPP

#include <atomic>
#include <vector>

struct single_callback_metric_event_t
{
    single_callback_metric_event_t() : phase(0), sent_ts_us(0) {}

    uint32_t phase;
    uint64_t sent_ts_us;
};

class single_callback_metric_queue_t
{
  public:
    explicit single_callback_metric_queue_t(size_t capacity_) :
        _events(capacity_ > 1 ? capacity_ + 1 : 2),
        _head(0),
        _tail(0)
    {
    }

    bool push(const single_callback_metric_event_t &event_)
    {
        const size_t head = _head.load(std::memory_order_relaxed);
        const size_t next = advance(head);
        if (next == _tail.load(std::memory_order_acquire))
            return false;
        _events[head] = event_;
        _head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(single_callback_metric_event_t *event_)
    {
        if (!event_)
            return false;
        const size_t tail = _tail.load(std::memory_order_relaxed);
        if (tail == _head.load(std::memory_order_acquire))
            return false;
        *event_ = _events[tail];
        _tail.store(advance(tail), std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return _tail.load(std::memory_order_acquire)
               == _head.load(std::memory_order_acquire);
    }

  private:
    size_t advance(size_t index_) const
    {
        return index_ + 1 < _events.size() ? index_ + 1 : 0;
    }

    std::vector<single_callback_metric_event_t> _events;
    std::atomic<size_t> _head;
    std::atomic<size_t> _tail;
};

#endif
