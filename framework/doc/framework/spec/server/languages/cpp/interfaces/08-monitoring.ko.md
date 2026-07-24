# C++ monitoring exact interface

[C++ exact interface 목차](README.ko.md)

Monitoring의 Channel, ClientServer와 placement weight는 configuration·descriptor와 같은 signed `int`
`0..10000`을 사용한다. `std::uint8_t`나 다른 좁은 unsigned projection을 사용하지 않는다.

## 1. Host termination 관측

Host 단위 상태는 RouteMesh·ClientServer·fanout snapshot과 분리한다. `mesh_node_state_t`는 MeshNode 상태를
나타내며 host termination 상태로 재사용하지 않는다.

```cpp
struct framework_runtime_snapshot_t {
    framework_runtime_state_t state;
    std::optional<termination_intent_t> effective_intent;
    std::optional<std::chrono::system_clock::time_point> deadline;
    bool work_sealed;
    std::optional<termination_reason_t> blocker_reason;
    std::uint64_t pending_request_count;
    std::uint64_t pending_relocation_count;
    std::uint64_t pending_stream_barrier_count;
    std::optional<termination_result_t> terminal_result;
    std::uint64_t sequence;
    std::chrono::system_clock::time_point observed_at;
};

struct framework_runtime_event_t {
    std::string identifier;
    std::uint64_t sequence;
    std::chrono::system_clock::time_point timestamp;
    framework_runtime_state_t state;
    std::optional<termination_intent_t> effective_intent;
    std::optional<termination_outcome_t> outcome;
    std::optional<termination_reason_t> reason;
};

class framework_runtime_t {
public:
    virtual ~framework_runtime_t() = default;
    virtual framework_runtime_state_t state() const noexcept = 0;
    virtual bool is_ready() const noexcept = 0;
    virtual framework_runtime_snapshot_t snapshot() const = 0;
    virtual std::unique_ptr<runtime_observation_t> observe(
      std::size_t capacity,
      std::function<void(const framework_runtime_event_t &)> observer) = 0;
};
```

### 1.1 MeshNode placement capacity

`mesh_node_snapshot_t`의 exact declaration은
[Channel messaging](03-channel-messaging.ko.md)에 있고, snapshot이 사용하는 `capacity_usage_t`,
`spot_type_capacity_t`와 `placement_capacity_t`는
[Location Store·Redis](07-location-store.ko.md)에 있다. `object_capacity.actors`,
`object_capacity.spots`와 각 `object_capacity.spot_types` 항목은 각각 `active`, `reserved`, `limit`을
제공한다. Limit `0`은 제한 없음을 뜻한다.

Actor 집계에는 Entry Spot과 User Spot의 Actor를 모두 포함한다. Spot 전체와 stable type별 집계에는
User·Instance Spot을 포함하지만 Entry Spot은 제외한다. Spot stable type 항목은 User Spot과 Instance Spot을
`object_kind`로 구분한다. `activation_concurrency`는 현재 factory·initialization 실행 수와 양수 limit을
별도로 제공하며 population capacity의 `reserved`에 합치지 않는다.

이 값은 Location Store의 authoritative counter를 관측용으로 투영한 snapshot이다. Descriptor와 runtime
snapshot의 projection이 늦게 갱신될 수 있으므로 placement 수락 여부를 판단하는 API로 사용하지 않는다.
Capacity reservation failure는 기존 `placement_reservation_failure_count`와
`last_placement_reservation_failure`에 기록하며 application factory나 handler exception으로 바꾸지 않는다.

## 2. 메시지 흐름 관측

```cpp
enum class message_flow_log_mode_t {
    off = 0,
    errors_only = 1,
    key_transitions = 2,
    verbose = 3,
    diagnostic = 4
};
enum class message_flow_outcome_t {
    received = 0,
    dispatched = 1,
    replied = 2,
    dropped = 3,
    sent = 4,
    reply_received = 5,
    error = 6
};
enum class dispatch_error_surface_t {
    channel = 0,
    route_mesh_channel = 1,
    spot_route = 2,
    spot_subscription = 3,
    spot_actor = 4,
    stream_session = 5
};
enum class dispatch_message_kind_t {
    request = 0,
    send = 1,
    publish = 2,
    response = 3,
    error = 4,
    actor_request = 5,
    actor_send = 6
};
enum class dispatch_error_reason_t {
    handler_missing = 0,
    payload_decode_failed = 1,
    handler_exception = 2,
    invalid_frame = 3,
    reply_path_missing = 4,
    unexpected_reply = 5
};
enum class dispatch_error_action_t {
    reply_error = 0,
    drop = 1,
    fail_caller = 2
};
enum class flow_origin_t : std::uint8_t
{ inbound = 1, timer = 2, application = 3, lifecycle = 4 };
struct message_dispatch_error_event_t {
    dispatch_error_surface_t surface;
    dispatch_message_kind_t message_kind;
    dispatch_error_reason_t reason;
    dispatch_error_action_t action;
    std::optional<std::string> packet_name;
    std::optional<std::string> channel_name;
    std::optional<std::string> topic;
    std::optional<std::string> spot_id;
    std::optional<std::string> actor_id;
    std::optional<std::string> source_rid;
    std::optional<std::string> correlation_id;
    std::exception_ptr exception;
    std::optional<std::string> flow_id;
    std::optional<flow_origin_t> flow_origin;
};
struct message_flow_event_t {
    message_flow_outcome_t outcome;
    dispatch_error_surface_t surface;
    dispatch_message_kind_t message_kind;
    std::optional<std::string> packet_name;
    std::optional<std::string> channel_name;
    std::optional<std::string> topic;
    std::optional<std::string> correlation_id;
    std::optional<std::string> source_rid;
    std::optional<std::string> spot_id;
    std::optional<std::string> actor_id;
    std::optional<std::size_t> message_size;
    std::optional<dispatch_error_reason_t> error_reason;
    std::optional<dispatch_error_action_t> error_action;
    std::exception_ptr exception;
    std::optional<std::string> flow_id;
    std::optional<flow_origin_t> flow_origin;
};
class message_flow_observer_t {
public:
    virtual ~message_flow_observer_t() = default;
    virtual void on_message_flow(const message_flow_event_t &event) = 0;
};

class dispatch_diagnostics_options_t {
public:
    message_flow_log_mode_t message_flow() const noexcept;
    double sample_rate() const noexcept;
    bool include_message_sizes() const noexcept;
    const std::optional<std::string> &log_file() const noexcept;
    const std::optional<std::string> &label() const noexcept;
    const std::shared_ptr<std::atomic<message_flow_log_mode_t>> &
      live_mode() const noexcept;
    message_flow_log_mode_t effective_message_flow() const noexcept;
};

struct dispatch_options_t {
    dispatch_diagnostics_options_t diagnostics;
    std::shared_ptr<message_flow_observer_t> message_flow_observer;
    std::function<void(const message_flow_event_t &)> message_flow_callback;
    std::optional<logger_t<>> diagnostics_logger;

    dispatch_options_t &set_message_flow_observer(
      std::shared_ptr<message_flow_observer_t> observer);
    dispatch_options_t &set_message_flow_observer(
      std::function<void(const message_flow_event_t &)> observer);
    dispatch_options_t &message_flow(message_flow_log_mode_t mode);
    dispatch_options_t &trace_sample_rate(double rate);
    dispatch_options_t &include_message_sizes(bool include);
    dispatch_options_t &trace_log_file(std::string path);
    dispatch_options_t &trace_label(std::string id);
    dispatch_options_t &message_flow_live(
      std::shared_ptr<std::atomic<message_flow_log_mode_t>> live);
};
```

의미는 [메시지 흐름 추적](../../../52-message-flow-tracing.ko.md)과
[흐름 상관관계](../../../53-flow-correlation.ko.md)가 소유한다.

## 3. Health

```cpp
enum class health_status_t {
    healthy = 0,
    degraded = 1,
    unhealthy = 2
};
enum class health_check_scope_t {
    readiness = 0,
    liveness = 1,
    readiness_and_liveness = 2
};

struct health_check_result_t
{
    std::string name, component;
    health_status_t     status = health_status_t::healthy;
    health_check_scope_t scope = health_check_scope_t::readiness_and_liveness;
    std::string message;
};

struct health_report_t
{
    health_status_t status = health_status_t::healthy;
    health_status_t readiness = health_status_t::healthy;
    health_status_t liveness = health_status_t::healthy;
    std::vector<health_check_result_t> checks;
    bool ready () const noexcept;   // readiness != unhealthy
    bool live  () const noexcept;   // liveness  != unhealthy
};

class health_builder_t
{
public:
    health_builder_t();
    ~health_builder_t();
    health_builder_t(health_builder_t &&) noexcept;
    health_builder_t &operator=(health_builder_t &&) noexcept;
    health_builder_t(const health_builder_t &) = delete;
    health_builder_t &operator=(const health_builder_t &) = delete;

    health_builder_t &add_zlink_runtime_check (std::string name = "zlink.runtime");
    health_builder_t &add_channel_check        (std::string name);
    health_builder_t &add_location_check       (std::string name);
    health_builder_t &add_stream_endpoint_check(std::string name);
    health_builder_t &add_hosted_service_check (std::string name);
    health_builder_t &set_status(
      std::string name,
      health_status_t status,
      std::string message = {});

    health_report_t report () const;
};
```

**`readiness`와 `liveness`를 분리한다.** 트래픽을 받을 준비(readiness)와 프로세스 생존(liveness)은
다른 질문이다. **`degraded`는 `ready()`·`live()`를 막지 않는다.**

## 4. Runtime event

```cpp
enum class runtime_event_severity_t {
    trace = 0,
    info = 1,
    warning = 2,
    error = 3
};
enum class socket_event_kind_t {
    connected = 0,
    connection_ready = 1,
    disconnected = 2,
    handshake_failed = 3,
    peer_admission_changed = 4,
    closed = 5,
    internal = 6
};
enum class location_event_kind_t {
    status_changed = 0,
    topology_changed = 1,
    service_summary_changed = 2
};
enum class spot_event_kind_t {
    status_changed = 0,
    peers_changed = 1,
    subjects_changed = 2,
    timer_handler_failed = 3,
    timer_stopped_after_unhandled_exception = 4
};
enum class stream_event_kind_t {
    connected = 0,
    disconnected = 1,
    transport_error = 2,
    handler_exception = 3
};
enum class actor_event_kind_t {
    bound = 0,
    unbound = 1,
    relay_failed = 2,
    session_disconnected = 3
};

struct runtime_event_base_t {
    std::string source_name;
    std::chrono::system_clock::time_point timestamp =
      std::chrono::system_clock::now();
    runtime_event_severity_t severity = runtime_event_severity_t::info;
    std::string node_name;
    std::string correlation_id;
    health_status_t health = health_status_t::healthy;
};

enum class drain_state_t {
    serving,
    draining,
    drained,
    force_stopping
};
struct drain_event_t : runtime_event_base_t {
    drain_state_t state = drain_state_t::serving;
};

struct socket_event_payload_t : runtime_event_base_t {
    socket_event_kind_t event = socket_event_kind_t::internal;
    std::string local_address;
    std::string remote_address;
    std::uint32_t native_event = 0;
    std::uint32_t native_value = 0;
};

struct location_event_payload_t : runtime_event_base_t {
    location_event_kind_t event = location_event_kind_t::status_changed;
    std::optional<location_runtime_status_t> status;
    std::vector<location_topology_entry_t> topology;
    std::vector<location_service_summary_t> service_summary;
};

struct spot_timer_diagnostic_t {
    spot_id_t spot_id;
    bool entry_spot = false;
    std::string timer_name;
    std::string handler_type;
    std::uint64_t delivery_index = 0;
    std::uint64_t scheduled_index = 0;
    std::string exception_type;
    std::string exception_message;
};

struct spot_event_payload_t : runtime_event_base_t {
    spot_event_kind_t event = spot_event_kind_t::status_changed;
    std::string spot_node_name;
    std::vector<std::string> peers;
    std::vector<std::string> subjects;
    std::optional<spot_timer_diagnostic_t> timer_diagnostic;
};

struct stream_event_payload_t : runtime_event_base_t {
    stream_event_kind_t event = stream_event_kind_t::connected;
    std::string stream_name;
    std::string session_id;
    std::string message;
};

struct actor_event_payload_t : runtime_event_base_t {
    actor_event_kind_t event = actor_event_kind_t::bound;
    std::string actor_type;
    std::string actor_id;
    std::string session_id;
    std::string message;
};

class runtime_event_publisher_t {
public:
    runtime_event_publisher_t();
    ~runtime_event_publisher_t();
    runtime_event_publisher_t(runtime_event_publisher_t &&) noexcept;
    runtime_event_publisher_t &operator=(runtime_event_publisher_t &&) noexcept;
    runtime_event_publisher_t(const runtime_event_publisher_t &) = default;
    runtime_event_publisher_t &operator=(const runtime_event_publisher_t &) = default;

    template <typename TEvent>
    void publish(TEvent event) const;
};

class monitoring_builder_t {
public:
    monitoring_builder_t();
    ~monitoring_builder_t();
    monitoring_builder_t(monitoring_builder_t &&) noexcept;
    monitoring_builder_t &operator=(monitoring_builder_t &&) noexcept;
    monitoring_builder_t(const monitoring_builder_t &) = delete;
    monitoring_builder_t &operator=(const monitoring_builder_t &) = delete;

    monitoring_builder_t &add_socket_events(std::string source_name);
    monitoring_builder_t &add_socket_events(
      std::string source_name,
      std::initializer_list<socket_event_kind_t> events);
    monitoring_builder_t &add_location_events(
      std::string source_name,
      std::chrono::milliseconds interval);
    monitoring_builder_t &add_spot_events(
      std::string source_name,
      std::chrono::milliseconds interval);
    monitoring_builder_t &add_stream_events(std::string source_name);
    monitoring_builder_t &add_actor_events(std::string source_name);
    monitoring_builder_t &on_trace(
      std::function<void(const runtime_event_base_t &)> hook);
    runtime_event_publisher_t publisher() const;

    template <typename TEvent>
    monitoring_builder_t &on(std::function<void(const TEvent &)> handler);
};
```

**source별로 표면을 나누는 근거는 [runtime-monitoring §2](../../../50-runtime-monitoring.ko.md)가
소유한다.** timer 실패는 **timer 실행을 계속하는 실패**와 **timer가 중단된 실패**를 구분한다.

**metric:**

```cpp
enum class metric_instrument_kind_t {
    counter,
    updown,
    observable,
    histogram
};
enum class metric_temporality_t {
    delta,
    current,
    sample
};
struct metric_event_payload_t : runtime_event_base_t {
    std::string name;
    double value = 0;
    std::string unit;
    metric_instrument_kind_t instrument_kind =
      metric_instrument_kind_t::counter;
    metric_temporality_t temporality = metric_temporality_t::delta;
    std::map<std::string, std::string> tags;
};

class metrics_builder_t {
public:
    metrics_builder_t();
    ~metrics_builder_t();
    metrics_builder_t(metrics_builder_t &&) noexcept;
    metrics_builder_t &operator=(metrics_builder_t &&) noexcept;
    metrics_builder_t(const metrics_builder_t &) = delete;
    metrics_builder_t &operator=(const metrics_builder_t &) = delete;

    metrics_builder_t &add_runtime_metrics();
    bool runtime_metrics_enabled() const noexcept;
    metrics_builder_t &record_runtime_metric(
      std::string name,
      double value,
      std::map<std::string, std::string> tags = {});
};
```

계기 카탈로그는 [runtime-metrics](../../../51-runtime-metrics.ko.md)가 소유한다.
