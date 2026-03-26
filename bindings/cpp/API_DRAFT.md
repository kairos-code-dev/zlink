# C++ Wrapper API Draft

## 공통 정책

- header-only thin wrapper를 유지한다.
- 기본 오류 모델은 반환 코드다.
- 모든 핸들은 move-only다.
- payload 변환은 `message_t`가 담당한다.
- `socket_t`는 `message_t` / `std::vector<message_t>` 기반
  `send`/`recv` overload만 제공한다.

## 핵심 표면

```cpp
namespace zlink {

class context_t;
class message_t;
class socket_t;
class monitor_handle_t;
class service_monitor_handle_t;
class poller_t;

namespace service {
class registry_t;
class registry_query_client_t;
class discovery_t;
class spot_node_t;
class spot_t;
} // namespace service

} // namespace zlink
```

## `message_t`

```cpp
class message_t {
public:
    message_t();
    explicit message_t(size_t size);

    static message_t from_bytes(const void *data, size_t size);
    static message_t from_string(const std::string &text);
    static message_t from_external(
      void *data, size_t size, zlink_free_fn *ffn = NULL, void *hint = NULL);

    std::vector<uint8_t> to_bytes() const;
    std::string to_string() const;
};
```

## `socket_t`

```cpp
class socket_t {
public:
    socket_t(context_t &ctx, socket_type type);

    int send(message_t &part, send_flag flags = send_flag::none);
    int send(std::vector<message_t> &parts, send_flag flags = send_flag::none);
    int send(
      const zlink_routing_id_t &rid, message_t &part,
      send_flag flags = send_flag::none);
    int send(
      const zlink_routing_id_t &rid, std::vector<message_t> &parts,
      send_flag flags = send_flag::none);

    int recv(message_t &part, recv_flag flags = recv_flag::none);
    int recv(std::vector<message_t> &parts, recv_flag flags = recv_flag::none);
    int recv(
      zlink_routing_id_t &rid, message_t &part,
      recv_flag flags = recv_flag::none);
    int recv(
      zlink_routing_id_t &rid, std::vector<message_t> &parts,
      recv_flag flags = recv_flag::none);

    int recv_handler(zlink_socket_msg_handler_fn, void *userdata = NULL);
    int subscribe_handler(zlink_subscribe_handler_fn, void *userdata = NULL);
    int send_ready_handler(zlink_send_ready_handler_fn, void *userdata = NULL);
};
```

제거한 convenience:

- `recv(void *, size_t)`
- `send(const void *, size_t)`
- `send(const std::string &)`
- STREAM 전용 `stream_attach*` / `stream_send*`

## 서비스 계층

```cpp
namespace zlink {
namespace service {

class registry_t {
public:
    int bind(const std::string &pub_endpoint, const std::string &router_endpoint);
    int status_snapshot(zlink_registry_status_t &out) const;
    int topology_snapshot(zlink_registry_topology_entry_t *entries, size_t *count) const;
};

class registry_query_client_t {
public:
    int connect(const std::string &endpoint);
    int snapshot(
      zlink_registry_topology_entry_t *entries, size_t *count,
      const zlink_registry_topology_filter_t *filter = NULL) const;
};

class discovery_t {
public:
    discovery_t(context_t &ctx, service_type type, const std::string &service_name);
    int connect_registry(const std::string &endpoint);
    int set_value(int64_t value);
    int set_metadata(const std::string &text);
};

class spot_node_t {
public:
    int bind(const std::string &endpoint);
    int connect_peer(const std::string &endpoint);
    int attach_discovery(discovery_t &discovery);
};

class spot_t {
public:
    int publish(const std::string &topic, message_t &part, send_flag flags = send_flag::none);
    int publish(const std::string &topic, std::vector<message_t> &parts, send_flag flags = send_flag::none);
    int recv(message_t &part, std::string &topic, recv_flag flags = recv_flag::none);
    int subscribe(const std::string &filter);
    int subscribe_handler(zlink_subscribe_handler_fn, void *userdata = NULL);
};

} // namespace service
} // namespace zlink
```

명시적으로 제외하는 구형 개념:

- `receiver_t`
- `spot_pub_t`
- `spot_sub_t`

## 검증 진입점

```bash
./bindings/cpp/build.sh ON ON
ctest --test-dir core/build --output-on-failure -L contract
ctest --test-dir core/build --output-on-failure -L sample-smoke -j1
```
