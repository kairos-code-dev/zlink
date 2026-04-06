#ifndef PERF_SINGLE_CALLBACK_RECEIVER_HPP
#define PERF_SINGLE_CALLBACK_RECEIVER_HPP

class callback_receiver_t
{
  public:
    callback_receiver_t ();
    ~callback_receiver_t ();

    callback_receiver_t (const callback_receiver_t &) = delete;
    callback_receiver_t &operator= (const callback_receiver_t &) = delete;

    bool attach (perf_socket_t &socket_, queue_probe_t *queue_probe_);
    bool begin_phase (uint32_t run_id_,
                      perf_single_metric::phase_t phase_,
                      size_t msg_size_,
                      bool active_);
    bool finish_phase (unsigned long long expected_count_,
                       int recv_timeout_ms_,
                       unsigned long long *received_out_,
                       latency_stats_t *latency_out_);
    bool failed () const;

  private:
    struct event_t
    {
        event_t ()
            : token (0),
              run_id (0),
              msg_size (0),
              active (false),
              header_ok (false)
        {
        }

        unsigned long long token;
        uint32_t run_id;
        size_t msg_size;
        perf_single_metric::phase_t phase;
        bool active;
        bool header_ok;
        perf_single_metric::header_t header;
    };

    static void recv_handler (const zlink_routing_id_t *source_rid_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              void *userdata_);
    bool push_event (const event_t &event_);
    void worker_loop ();

    perf_socket_t *_socket;
    queue_probe_t *_queue_probe;
    std::vector<event_t> _queue;
    size_t _queue_head;
    size_t _queue_tail;
    size_t _queue_count;
    bool _stop_worker;
    std::atomic<bool> _failed;
    std::mutex _queue_mutex;
    std::condition_variable _queue_cv;
    std::thread _worker;

    std::atomic<unsigned long long> _current_token;
    std::atomic<uint32_t> _current_run_id;
    std::atomic<size_t> _current_msg_size;
    std::atomic<int> _current_phase;
    std::atomic<bool> _current_active;

    std::mutex _result_mutex;
    std::condition_variable _result_cv;
    unsigned long long _result_token;
    unsigned long long _received_count;
    latency_stats_builder_t _latency_builder;
};

class subscribe_callback_receiver_t
{
  public:
    subscribe_callback_receiver_t ();
    ~subscribe_callback_receiver_t ();

    subscribe_callback_receiver_t (const subscribe_callback_receiver_t &) = delete;
    subscribe_callback_receiver_t &
    operator= (const subscribe_callback_receiver_t &) = delete;

    bool attach_socket (perf_socket_t &socket_, queue_probe_t *queue_probe_);
    bool attach_spot (zlink::service::spot_t &spot_, queue_probe_t *queue_probe_);
    bool begin_phase (uint32_t run_id_,
                      perf_single_metric::phase_t phase_,
                      size_t msg_size_,
                      bool active_,
                      const std::string &topic_);
    bool finish_phase (unsigned long long expected_count_,
                       int recv_timeout_ms_,
                       unsigned long long *received_out_,
                       latency_stats_t *latency_out_);
    bool failed () const;

  private:
    struct event_t
    {
        event_t ()
            : token (0),
              run_id (0),
              msg_size (0),
              active (false),
              header_ok (false)
        {
        }

        unsigned long long token;
        uint32_t run_id;
        size_t msg_size;
        perf_single_metric::phase_t phase;
        bool active;
        bool header_ok;
        perf_single_metric::header_t header;
        std::string topic;
    };

    static void subscribe_handler (const zlink_routing_id_t *source_rid_,
                                   const char *topic_,
                                   size_t topic_len_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   void *userdata_);
    bool push_event (const event_t &event_);
    void worker_loop ();

    queue_probe_t *_queue_probe;
    std::vector<event_t> _queue;
    size_t _queue_head;
    size_t _queue_tail;
    size_t _queue_count;
    bool _stop_worker;
    std::atomic<bool> _failed;
    std::mutex _queue_mutex;
    std::condition_variable _queue_cv;
    std::thread _worker;

    std::atomic<unsigned long long> _current_token;
    std::atomic<uint32_t> _current_run_id;
    std::atomic<size_t> _current_msg_size;
    std::atomic<int> _current_phase;
    std::atomic<bool> _current_active;
    std::string _expected_topic;

    std::mutex _result_mutex;
    std::condition_variable _result_cv;
    unsigned long long _result_token;
    unsigned long long _received_count;
    latency_stats_builder_t _latency_builder;
};

bool run_callback_phase (callback_receiver_t &receiver_,
                         phase_send_fn_t send_fn_,
                         void *send_userdata_,
                         std::vector<char> &payload_,
                         size_t msg_size_,
                         uint32_t run_id_,
                         uint64_t &seq_,
                         perf_single_metric::phase_t phase_,
                         int warmup_count_,
                         int duration_s_,
                         int recv_timeout_ms_,
                         unsigned long long *received_out_,
                         latency_stats_t *latency_out_);

bool run_subscribe_callback_phase (subscribe_callback_receiver_t &receiver_,
                                   phase_send_fn_t send_fn_,
                                   void *send_userdata_,
                                   std::vector<char> &payload_,
                                   size_t msg_size_,
                                   uint32_t run_id_,
                                   uint64_t &seq_,
                                   perf_single_metric::phase_t phase_,
                                   int warmup_count_,
                                   int duration_s_,
                                   int recv_timeout_ms_,
                                   const std::string &topic_,
                                   unsigned long long *received_out_,
                                   latency_stats_t *latency_out_);

#endif
