# C++ STREAM session exact interface

[C++ exact interface 목차](README.ko.md) · [Session Actor dispatch](../../../../31-session-actor-dispatch.ko.md)

## 1. Public session surface

Wire header, heartbeat control packet과 native STREAM connection ID는 runtime 내부에 둔다.

```cpp
enum class stream_codec_t : std::uint8_t {
    raw = 0,
    json = 1,
    message_pack = 2,
    protobuf = 3
};

enum class stream_session_error_t {
    internal = 0,
    transport_error = 1
};

enum class stream_close_reason_t : std::uint8_t {
    client_close = 1,
    idle_timeout = 2,
    heartbeat_timeout = 3,
    server_drain = 4,
    protocol_error = 5,
    transport_error = 6
};

class stream_compression_codec_t {
public:
    virtual ~stream_compression_codec_t() = default;
    virtual zlink::framework::message_t compress(
      const zlink::framework::message_t &payload) const = 0;
    virtual zlink::framework::message_t decompress(
      const zlink::framework::message_t &payload,
      std::size_t max_decompressed_size) const = 0;
};

std::shared_ptr<const stream_compression_codec_t>
lz4_stream_compression_codec();

class stream_error_t {
public:
    stream_error_t() = default;
    stream_error_t(
      stream_session_error_t error,
      int native_code,
      std::string message);

    stream_session_error_t error() const noexcept;
    int native_code() const noexcept;
    std::string_view message() const noexcept;
};

class stream_metadata_t {
public:
    stream_metadata_t() = default;
    explicit stream_metadata_t(
      std::map<std::string, std::string> values);

    stream_metadata_t &with(std::string key, std::string value);
    std::optional<std::string_view> find(std::string_view key) const;
    bool empty() const noexcept;
    const std::map<std::string, std::string> &values() const noexcept;
};

class stream_dispatch_context_t {
public:
    stream_dispatch_context_t();
    std::string_view packet_name() const noexcept;
    const stream_metadata_t &metadata() const noexcept;
    bool can_reply() const noexcept;
};

class stream_t {
public:
    stream_t();
    ~stream_t();
    stream_t(stream_t &&) noexcept;
    stream_t &operator=(stream_t &&) noexcept;
    stream_t(const stream_t &) = default;
    stream_t &operator=(const stream_t &) = default;

    std::string session_id() const;
    std::optional<zlink::routing_id_t> routing_id() const;
    std::optional<std::string> local_address() const;
    std::optional<std::string> remote_address() const;
    session_actor_manager_t &actors();
    task_t<void> close();
    stream_send_call_t write_packet(
      const zlink::framework::message_t &payload);
    stream_write_call_t reply_packet(
      const zlink::framework::message_t &payload);
};

class packet_stream_session_t {
public:
    virtual ~packet_stream_session_t() = default;
    virtual task_t<void> on_connected(stream_t &stream) = 0;
    virtual task_t<void> on_disconnected(stream_t &stream) = 0;
    virtual task_t<void> on_error(
      stream_t &stream,
      const stream_error_t &error) = 0;
    virtual task_t<void> on_packet(
      stream_t &stream,
      const stream_dispatch_context_t &dispatch,
      const zlink::framework::message_t &payload);
};

class stream_builder_t {
public:
    stream_builder_t();
    ~stream_builder_t();
    stream_builder_t(stream_builder_t &&) noexcept;
    stream_builder_t &operator=(stream_builder_t &&) noexcept;
    stream_builder_t(const stream_builder_t &) = default;
    stream_builder_t &operator=(const stream_builder_t &) = default;

    stream_builder_t &bind(std::string endpoint);
    stream_builder_t &bind(std::uint16_t port = 0);
    stream_builder_t &set_bind_host(std::string host);
    stream_builder_t &set_advertise_host(std::string host);
    stream_builder_t &set_tls_server(
      std::string certificate_file,
      std::string private_key_file);
    stream_builder_t &register_session(std::string session_name);
    stream_snapshot_t snapshot() const;
};

struct stream_snapshot_t {
    std::string name;
    std::string bind_endpoint;
    std::string packet_session_name;
    std::string tls_certificate_file;
    std::string tls_private_key_file;
};

class stream_compression_options_builder_t {
public:
    stream_compression_options_builder_t &use_default();
    stream_compression_options_builder_t &use_lz4();
    stream_compression_options_builder_t &use(
      std::shared_ptr<const stream_compression_codec_t> codec);
    stream_compression_options_builder_t &disable();
};

class stream_node_options_builder_t {
public:
    stream_node_options_builder_t &bind(std::string endpoint);
    stream_node_options_builder_t &bind(std::uint16_t port = 0);
    stream_node_options_builder_t &set_bind_host(std::string host);
    stream_node_options_builder_t &set_advertise_host(std::string host);
    stream_node_options_builder_t &set_tls_server(
      std::string certificate_file,
      std::string private_key_file);
    stream_node_options_builder_t &set_tls_server(
      std::string certificate_file,
      std::string private_key_file,
      bool require_client_certificate);
    stream_node_options_builder_t &enable_actor_dispatch();
    stream_node_options_builder_t &register_session(std::string session_name);

    template <typename TSession>
      requires std::derived_from<TSession, packet_stream_session_t>
    stream_node_options_builder_t &register_session();
};
```

Bind 뒤 relay·request relay와 `notify_disconnected()`는 Actor별 저장 route를 사용하며 message마다 Location
Store를 조회하지 않는다. Physical disconnect는 Framework가 current binding 전체에 automatic all-settled
통지를 수행하고 exact binding identity마다 Spot callback을 최대 한 번 실행한다.
`notify_disconnected()`는 connection이 유지된 상태의 logical notification이며 callback terminal까지
기다린다. Relocation route update는 같은 ObjectGeneration에만 허용하고 callback·journal replay,
durable source cleanup과 `Completed` 뒤 해당 Actor route만 바꾼다. Command 44·45 routed ACK와 steady
normalization 전에는 target session packet·push admission을 열지 않으며 같은 Session의 다른 Actor
route와 physical STREAM connection은 유지한다.

`bound_session_t`, `session_actor_t`와 `session_actor_manager_t`의 exact Actor 연동 member는
[Actor interface](05-actors.ko.md)가 소유한다. `stream_send_call_t`와 `stream_write_call_t`의
metadata·compression·`submit()` member는 [Channel messaging](03-channel-messaging.ko.md)의 call family와
같은 admission 계약을 유지한다.
STREAM application callback, send·reply와 compression extension은 binding message가 아니라
`zlink::framework::message_t`를 사용한다. Framework codec registry가 typed payload와 이 message 경계를 변환한다.
`stream_t`의 optional routing ID와 local·remote address는 handshake가 확인한 session identity snapshot이며 packet
dispatch까지 보존한다.
Session callback은 받은 `stream_t`의 `actors()`로 해당 session의 Actor binding manager에 접근한다.
Actor dispatch는 global ActorId lookup과 exact `actor_ref_t` bind를 사용하므로 target MeshName 또는 local Actor
overload를 등록하지 않는다. Object role `client`·`server`와 Location Store가 없으면 startup이
`object_client_not_configured`로 실패한다.
Handshake failure는 session이 만들어지기 전 runtime monitoring에만 기록되며 `on_error(...)`에
전달하지 않는다.

**wire 값이 계약이다.** `stream_close_reason_t`의 1~6은
[Stream Connector §4.6](../../../../stream-connector/32-stream-connector.ko.md)의 `session-closing` payload와 같은 값이다.
**enum을 정수로 cast해 wire 값으로 쓰지 않는다** — codec이 명시적으로 변환한다.
