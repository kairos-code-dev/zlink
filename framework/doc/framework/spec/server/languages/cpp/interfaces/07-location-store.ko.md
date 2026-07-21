# C++ Location Store·Redis exact interface

[C++ exact interface 목차](README.ko.md) · [Location Runtime](../../../40-location-runtime.ko.md) ·
[Redis Location Store](../../../41-location-store-redis.ko.md)

## 1. Root 등록과 option

```cpp
namespace zlink::framework {

struct location_options_t {
    std::chrono::milliseconds owner_lease_renew_interval{5000};
    std::chrono::milliseconds owner_lease_ttl{15000};
    std::chrono::milliseconds polling_interval{1000};
    std::chrono::milliseconds store_failure_grace{30000};
    std::chrono::milliseconds owner_lease_fencing_margin{5000};
    std::chrono::milliseconds owner_lease_renew_timeout{3000};
};

} // namespace zlink::framework
```

Store 등록과 location option member는
[Configuration과 host](02-configuration-host.ko.md)의 `zlink_framework_options_t`가 소유한다.

Store는 host마다 하나만 등록한다. 자동 discovery, fanout publisher descriptor 게시, Instance Spot address,
remote Spot·Actor 위치, routing ID 자동 할당 또는 Actor transfer를 설정했는데 필요한 capability가 없으면 host는 socket
bind 전에 구성 오류로 종료한다. Store를 등록하지 않은 fanout publisher는 manual endpoint 대상으로
동작한다. Manual peer, manual fanout publisher·subscriber와 process-local Spot·Actor만 사용하는 host는
store 없이 시작할 수 있다.

Store가 없는 Actor는 runtime-local opaque authority와 handle만 사용하며 같은 process 안에서만 메시지를
주고받는다. Remote directory·resolve, distributed join과 session binding, transfer·relocation을 구성하거나
호출하면 configuration 또는 operation error로 거부한다. Durable authority와 owner generation은 Store-backed
Actor에만 요구한다. 이 제한을 바꾸는 caller option은 없다.

Duration option은 모두 양수여야 한다. Location Store와 owner lease runtime을 사용하는 모든 host는 routing ID
자동 할당 여부와 관계없이 `owner_lease_renew_interval + owner_lease_renew_timeout < owner_lease_ttl -
owner_lease_fencing_margin`을 만족해야 한다. 위 기본값은 이 관계를 만족한다. Routing slot도 같은 host token과
deadline을 사용하며 별도 fencing margin 의미를 만들지 않는다.

`store_failure_grace`는 discovery reconcile과 새 outbound connect에만 적용한다. Store failure 동안 마지막 stable
desired set을 grace까지 고정하고 existing admitted transport에는 service liveness를 계속 적용한다. Grace 뒤에는
stable store snapshot을 다시 얻기 전까지 새 connection을 만들지 않는다. 이 값은 owner·coordinator lease나 local
authority deadline을 연장하지 않으며 stateful message, timer, factory와 CAS admission은 마지막 valid monotonic
lease deadline에서 닫힌다. Recovery는 exact owner token과 stable page set을 재검증한 뒤 diff와 connect를 수행한다.

## 2. Store-neutral record와 capability

```cpp
namespace zlink::framework {

enum class location_write_intent_t {
    new_claim = 1,
    renew = 2,
    takeover = 3
};
enum class location_write_status_t {
    stored = 1,
    ignored_stale = 2,
    rejected_conflict = 3
};

struct location_write_result_t {
    location_write_status_t status = location_write_status_t::ignored_stale;
    std::int64_t generation = 0;
    std::chrono::system_clock::time_point updated_at{};

    static location_write_result_t stored(
      std::int64_t generation,
      std::chrono::system_clock::time_point updated_at);
};

struct location_owner_token_t {
    std::string owner_id;
    std::int64_t lease_generation = 0;
};
struct owner_lease_claimed_t {
    location_owner_token_t token;
    std::chrono::system_clock::time_point lease_expires_at{};
    std::chrono::system_clock::time_point store_now{};
};
struct owner_lease_conflict_t {};
struct owner_lease_generation_exhausted_t {};
using owner_lease_claim_result_t =
  std::variant<owner_lease_claimed_t, owner_lease_conflict_t,
               owner_lease_generation_exhausted_t>;

struct owner_lease_renewed_t {
    std::chrono::system_clock::time_point lease_expires_at{};
    std::chrono::system_clock::time_point store_now{};
};
struct owner_lease_stale_t {};
using owner_lease_renew_result_t =
  std::variant<owner_lease_renewed_t, owner_lease_stale_t>;

struct owner_lease_released_t {};
using owner_lease_release_result_t =
  std::variant<owner_lease_released_t, owner_lease_stale_t>;

struct owner_lease_found_t {
    location_owner_token_t token;
    std::chrono::system_clock::time_point lease_expires_at{};
    std::chrono::system_clock::time_point store_now{};
};
struct owner_lease_missing_t {};
using owner_lease_read_result_t =
  std::variant<owner_lease_found_t, owner_lease_missing_t>;
enum class location_auto_connect_type_t {
    invalid = 0,
    route_mesh = 1,
    client_server = 2,
    dealer_mesh = 3,
    fanout = 4,
    spot_mesh = 5
};
enum class location_role_t : std::uint16_t {
    invalid = 0,
    spot = 2,
    router = 3,
    dealer = 4,
    pub = 5,
    sub = 6
};
enum class route_kind_t {
    invalid = 0,
    actor_session = 1,
    spot_name = 2,
    framework_route = 3
};
enum class location_kind_t {
    invalid = 0,
    peer = 1,
    spot = 2,
    actor = 3,
    route = 4
};

struct peer_location_t {
    location_auto_connect_type_t auto_connect_type =
      location_auto_connect_type_t::invalid;
    std::string mesh_name;
    std::optional<zlink::routing_id_t> node_rid;
    location_role_t role = location_role_t::invalid;
    std::string endpoint;
    std::uint32_t weight = 0;
    std::int64_t value = 0;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> capabilities;
    std::string owner_id;
    std::int64_t lease_generation = 0;
    std::int64_t generation = 0;
    std::chrono::system_clock::time_point updated_at{};
    bool draining = false;
};

struct peer_location_key_t {
    location_auto_connect_type_t auto_connect_type =
      location_auto_connect_type_t::invalid;
    std::string mesh_name;
    location_role_t role = location_role_t::invalid;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<std::string> endpoint;
};

struct peer_location_filter_t {
    std::optional<location_auto_connect_type_t> auto_connect_type;
    std::optional<std::string> mesh_name;
    std::optional<location_role_t> role;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<std::string> endpoint;
};

enum class placement_object_kind_t { actor = 1, instance_spot = 2 };
enum class maintenance_policy_kind_t {
    disabled = 1,
    recreate = 2,
    snapshot = 3
};

struct object_capability_t {
    placement_object_kind_t object_kind;
    std::string type;
    maintenance_policy_kind_t policy;
    std::set<std::string> readable_state_contract_ids;
    std::uint64_t available;
};

struct mesh_node_descriptor_t {
    std::string mesh_name;
    zlink::routing_id_t rid;
    std::uint64_t lifecycle_generation;
    std::uint64_t descriptor_revision;
    std::string endpoint;
    std::map<std::string, int> channel_weights;
    std::set<std::string> spot_types;
    std::int64_t application_version;
    std::vector<object_capability_t> object_capabilities;
    std::optional<std::string> maintenance_wave;
    framework_runtime_state_t state;
    std::string security_identity;
    std::string owner_id;
    std::int64_t lease_generation;
    std::chrono::system_clock::time_point updated_at;
};
struct mesh_node_descriptor_key_t { std::string mesh_name; zlink::routing_id_t rid; };

struct client_server_server_descriptor_t {
    std::string channel_name;
    zlink::routing_id_t server_rid;
    std::uint64_t lifecycle_generation;
    std::uint64_t descriptor_revision;
    std::string endpoint;
    int weight;
    framework_runtime_state_t state;
    std::string security_identity;
    std::string owner_id;
    std::int64_t lease_generation;
    std::chrono::system_clock::time_point updated_at;
};
struct client_server_server_descriptor_key_t {
    std::string channel_name;
    zlink::routing_id_t server_rid;
};

struct fanout_publisher_descriptor_t {
    std::string channel_name;
    zlink::routing_id_t publisher_rid;
    std::uint64_t lifecycle_generation;
    std::uint64_t descriptor_revision;
    std::string endpoint;
    framework_runtime_state_t state;
    std::string security_identity;
    std::string owner_id;
    std::int64_t lease_generation;
    std::chrono::system_clock::time_point updated_at;
};
struct fanout_publisher_descriptor_key_t {
    std::string channel_name;
    zlink::routing_id_t publisher_rid;
};

struct spot_location_t {
    std::string mesh_name;
    zlink::routing_id_t spot_rid =
      zlink::routing_id_t::from(std::uint32_t{0});
    std::uint64_t spot_generation = 0;
    zlink::routing_id_t owner_node_rid =
      zlink::routing_id_t::from(std::uint32_t{0});
    std::uint64_t owner_node_generation = 0;
    zlink::spot_kind spot_kind = zlink::spot_kind::invalid;
    std::string spot_type;
    std::string owner_id;
    std::int64_t lease_generation = 0;
    std::chrono::system_clock::time_point updated_at{};
};
struct spot_location_key_t {
    std::string mesh_name;
    zlink::routing_id_t spot_rid =
      zlink::routing_id_t::from(std::uint32_t{0});
};

struct spot_location_filter_t {
    std::optional<std::string> mesh_name;
    std::optional<std::string> spot_type;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<zlink::spot_kind> spot_kind;
};

struct actor_location_t {
    std::string mesh_name;
    std::string actor_id;
    std::string actor_type;
    actor_ref_t actor_ref;
    zlink::routing_id_t owner_node_rid =
      zlink::routing_id_t::from(std::uint32_t{0});
    std::uint64_t owner_node_generation = 0;
    zlink::routing_id_t spot_rid =
      zlink::routing_id_t::from(std::uint32_t{0});
    std::uint64_t spot_generation = 0;
    zlink::spot_kind spot_kind = zlink::spot_kind::invalid;
    std::string owner_id;
    std::int64_t lease_generation = 0;
    std::chrono::system_clock::time_point updated_at{};
};
struct actor_location_key_t { std::string mesh_name; std::string actor_id; };

struct actor_location_filter_t {
    std::optional<std::string> mesh_name;
    std::optional<std::string> actor_type;
    std::optional<zlink::routing_id_t> owner_node_rid;
    std::optional<zlink::routing_id_t> spot_rid;
    std::optional<zlink::spot_kind> spot_kind;
};

struct route_location_t {
    route_kind_t route_kind = route_kind_t::invalid;
    std::string route_key;
    zlink::routing_id_t owner_node_rid =
      zlink::routing_id_t::from(std::uint32_t{0});
    std::string owner_id;
    std::int64_t lease_generation = 0;
    std::int64_t generation = 0;
    std::vector<std::uint8_t> value;
    std::chrono::system_clock::time_point updated_at{};
};
struct route_location_key_t {
    route_kind_t route_kind = route_kind_t::invalid;
    std::string route_key;
};
struct route_location_filter_t {
    std::optional<route_kind_t> route_kind;
    std::optional<zlink::routing_id_t> owner_node_rid;
    std::optional<std::string> owner_id;
};

using location_key_t = std::variant<
  peer_location_key_t,
  spot_location_key_t,
  actor_location_key_t,
  route_location_key_t>;

struct location_page_request_t {
    int page_size = 100;
    std::optional<std::string> continuation_token;
};

template <typename T>
struct location_page_t {
    std::vector<T> items;
    std::optional<std::string> continuation_token;
};

class mesh_node_location_store_t {
public:
    virtual task_t<location_write_result_t> update_mesh_node(
      mesh_node_descriptor_t descriptor, location_write_intent_t intent) = 0;
    virtual task_t<location_write_status_t> remove_mesh_node(
      mesh_node_descriptor_key_t key, location_owner_token_t owner) = 0;
    virtual task_t<location_page_t<mesh_node_descriptor_t>> list_mesh_nodes(
      std::string mesh_name,
      location_page_request_t page = {}) = 0;
};

class client_server_location_store_t {
public:
    virtual task_t<location_write_result_t> update_client_server(
      client_server_server_descriptor_t descriptor,
      location_write_intent_t intent) = 0;
    virtual task_t<location_write_status_t> remove_client_server(
      client_server_server_descriptor_key_t key,
      location_owner_token_t owner) = 0;
    virtual task_t<location_page_t<client_server_server_descriptor_t>>
      list_client_servers(
        std::string channel_name,
        location_page_request_t page = {}) = 0;
};

class fanout_location_store_t {
public:
    virtual task_t<location_write_result_t> update_fanout_publisher(
      fanout_publisher_descriptor_t descriptor,
      location_write_intent_t intent) = 0;
    virtual task_t<location_write_status_t> remove_fanout_publisher(
      fanout_publisher_descriptor_key_t key,
      location_owner_token_t owner) = 0;
    virtual task_t<location_page_t<fanout_publisher_descriptor_t>>
      list_fanout_publishers(
        std::string channel_name,
        location_page_request_t page = {}) = 0;
};

class peer_location_store_t {
public:
    virtual ~peer_location_store_t() = default;
    virtual task_t<location_write_result_t> update_peer(
      peer_location_t peer,
      location_write_intent_t intent) = 0;
    virtual task_t<location_write_result_t> remove_peer(
      peer_location_key_t key,
      location_owner_token_t owner) = 0;
    virtual task_t<location_page_t<peer_location_t>> list_peers(
      peer_location_filter_t filter,
      location_page_request_t page = {}) = 0;
};

class route_location_store_t {
public:
    virtual ~route_location_store_t() = default;
    virtual task_t<location_write_result_t> update_route(
      route_location_t route,
      location_write_intent_t intent) = 0;
    virtual task_t<location_write_result_t> remove_route(
      route_location_key_t key,
      location_owner_token_t owner) = 0;
    virtual task_t<std::optional<route_location_t>> resolve_route(
      route_location_key_t key) = 0;
    virtual task_t<location_page_t<route_location_t>> list_routes(
      route_location_filter_t filter,
      location_page_request_t page = {}) = 0;
};

class owner_lease_store_t {
public:
    virtual ~owner_lease_store_t() = default;
    virtual task_t<owner_lease_claim_result_t> claim_owner_lease(
      std::string owner_id, std::chrono::milliseconds lease_ttl) = 0;
    virtual task_t<owner_lease_read_result_t> read_owner_lease(
      std::string owner_id) = 0;
    virtual task_t<owner_lease_renew_result_t> renew_owner_lease(
      location_owner_token_t token,
      std::chrono::milliseconds lease_ttl) = 0;
    virtual task_t<owner_lease_release_result_t> release_owner_lease(
      location_owner_token_t token) = 0;
};

} // namespace zlink::framework
```

Framework는 host process runtime lifecycle마다 새 owner ID를 만들며 application이 값을 설정하거나
이전 lifecycle의 ID를 재사용하게 하지 않는다. 한 host의 모든 MeshNode·ClientServer·fanout
descriptor와 authority는 같은 host token을 참조하고 각 descriptor가 자신의 RID를 갖는다.
`claim_owner_lease(...)`만 owner token을 발급한다. Provider domain은 영구적인 global
`lease_generation` counter 하나를 유지하고 claim이 성공할 때마다 증가시켜
`1..9223372036854775807`의 token을 발급한다. Active owner ID의 중복 claim은
`owner_lease_conflict_t`다. Expiry·release는 active row를 삭제하며 같은 owner ID로 다시 claim하면 더 큰
global generation을 받는다. Renew와 release는 token 전체가 current claim과 같을 때만 성공하며
만료되었거나 교체된 token은 `owner_lease_stale_t`다. Descriptor와 page item의 `lease_generation`은 current
lease token의 `lease_generation`과 같아야 한다.
Claim 또는 expired row takeover에 새 lease generation이 필요한데 counter가 최댓값이면 non-retriable
`owner_lease_generation_exhausted_t`를 반환하고 row, index와 counter를 바꾸거나 값을 소비하지 않는다.
같은 상태의 retry도 같은 결과를 반환한다. Renew와 release에는 exhaustion 결과가 없다.
Target admission 직전에
`read_owner_lease(owner_id)`로 exact token을 다시 확인한다. Owner lease 전체 목록과 snapshot type은 public
surface에 제공하지 않는다.

Descriptor와 peer enumeration은 `location_page_request_t`와 `location_page_t<T>`를 사용한다. Effective
`page_size`는 `1..1000`이며 continuation token은 provider만 해석하는 opaque value다. Framework reconciler는
provider가 encoded page 4 MiB에 먼저 도달하면 요청보다 적은 item과 다음 token을 반환하며 byte limit public
option은 제공하지 않는다. Framework reconciler는 scope change stamp를 읽고 모든 page를 조립한 뒤 stamp를 다시 읽는다. 두 stamp가 같을 때만 full snapshot을
적용하고 다르면 부분 결과를 버리고 first page부터 다시 읽는다. Page 조립과 retry는 Framework 내부 동작이며
application에 별도 reconciliation API를 제공하지 않는다.

Entry·User·Instance Spot owner state는 `(MeshName, SpotRid)`에서 파생한 하나의 opaque authority key를
공유한다. User Spot create와 Instance cold claim은 같은 row에 `new_object` compare-exchange를 수행하므로
kind conflict와 object generation 증가가 원자적으로 결정된다. `spot_location_t`는 Framework가 authority
payload와 page를 decode해서 만드는 운영 조회 projection이며 provider write·remove·resolve interface가 아니다.
Provider는 Spot kind, type, owner state와 Actor transfer phase를 해석하지 않는다.
`spot_location_t::spot_generation`, Spot handle generation과 `actor_ref_t::generation`은 provider의
`object_generation`을 그대로 사용한다. Authority envelope의 `authority_owner_generation`은 authority owner
이관 fence이고 descriptor·projection의 `lease_generation`은 host lease fence다. 두 generation을 합치거나
Framework 계산값으로 만들지 않는다.
Maintenance owner 이관은 `new_owner`로 owner generation만 바꾸고 object generation을 유지한다.
기존 handle은 유효하며 이전 owner route를 사용하면 runtime이 current authority를 재조회하여 forwarding
또는 retry한다. Explicit close 후 cold recreate만 `new_object`로 새 object generation을 발급하며,
이때 이전 handle은 영구적으로 stale다.

## 2.1 Watch, resolve와 runtime query

```cpp
struct location_watch_filter_t {
    location_kind_t kind = location_kind_t::peer;
    std::optional<std::string> mesh_name;
    std::optional<route_kind_t> route_kind;
};

enum class location_change_type_t {
    upserted = 1,
    removed = 2,
    expired = 3
};

struct location_changed_t {
    location_kind_t kind = location_kind_t::peer;
    location_key_t key;
    location_change_type_t change_type = location_change_type_t::upserted;
    std::int64_t generation = 0;
    std::chrono::system_clock::time_point updated_at{};
};

struct location_change_stamp_scope_t {
    location_kind_t kind = location_kind_t::peer;
    std::optional<std::string> mesh_name;
};

using location_watch_callback_t =
  std::function<void(location_changed_t)>;

class location_watch_store_t {
public:
    virtual ~location_watch_store_t() = default;
    virtual task_t<void> watch_locations(
      location_watch_filter_t filter,
      location_watch_callback_t callback) = 0;
};

class location_change_stamp_store_t {
public:
    virtual ~location_change_stamp_store_t() = default;
    virtual task_t<std::int64_t> get_change_stamp(
      location_change_stamp_scope_t scope) = 0;
};

class peer_location_resolver_t {
public:
    virtual ~peer_location_resolver_t() = default;
    virtual task_t<std::vector<peer_location_t>> list_live_peers(
      peer_location_filter_t filter) = 0;
};

class spot_handle_t final {
public:
    spot_rid_t spot_rid() const noexcept;
};

class spot_handle_resolver_t {
public:
    virtual ~spot_handle_resolver_t() = default;
    virtual task_t<std::optional<spot_handle_t>> resolve_spot_handle(
      std::string mesh_name,
      spot_rid_t spot_rid) = 0;
};

class actor_spot_handle_resolver_t {
public:
    virtual ~actor_spot_handle_resolver_t() = default;
    virtual task_t<std::optional<spot_handle_t>> resolve_actor_spot_handle(
      std::string mesh_name,
      std::string actor_id) = 0;
};

class location_readiness_t {
public:
    virtual ~location_readiness_t() = default;
    virtual task_t<bool> is_peer_ready(
      std::string mesh_name,
      location_role_t role,
      std::optional<zlink::routing_id_t> node_rid = std::nullopt) = 0;
};

struct location_runtime_status_t {
    bool store_healthy = false;
    bool watch_enabled = false;
    std::chrono::milliseconds polling_interval{0};
    std::optional<std::chrono::system_clock::time_point> last_refresh_at;
    std::optional<std::string> last_error;
    bool owner_lease_healthy = false;
    std::optional<std::chrono::system_clock::time_point> owner_lease_renewed_at;
};

enum class location_topology_state_t {
    discovered = 1,
    connecting = 2,
    ready = 3,
    lost = 4,
    error = 5,
    stopped = 6
};

struct location_topology_filter_t {
    std::optional<location_kind_t> kind;
    std::optional<std::string> mesh_name;
    std::optional<location_role_t> role;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<location_topology_state_t> state;
};

struct location_topology_entry_t {
    location_kind_t kind = location_kind_t::peer;
    std::optional<std::string> mesh_name;
    std::optional<location_role_t> role;
    std::optional<zlink::routing_id_t> node_rid;
    std::optional<zlink::routing_id_t> spot_rid;
    std::optional<std::string> actor_id;
    std::optional<std::string> endpoint;
    location_topology_state_t state = location_topology_state_t::discovered;
    std::uint32_t desired_count = 0;
    std::uint32_t ready_count = 0;
    int error_code = 0;
    std::chrono::system_clock::time_point updated_at{};
};

struct location_service_summary_filter_t {
    std::optional<std::string> mesh_name;
    std::optional<location_auto_connect_type_t> auto_connect_type;
    std::optional<location_role_t> role;
};

struct location_service_summary_t {
    std::string mesh_name;
    location_auto_connect_type_t auto_connect_type =
      location_auto_connect_type_t::invalid;
    location_role_t role = location_role_t::invalid;
    std::uint32_t total_count = 0;
    std::uint32_t ready_count = 0;
    std::uint32_t lost_count = 0;
    std::uint32_t error_count = 0;
};

class location_runtime_query_t {
public:
    virtual ~location_runtime_query_t() = default;
    virtual task_t<location_runtime_status_t> get_status() = 0;
    virtual task_t<std::vector<peer_location_t>> list_peer_locations(
      peer_location_filter_t filter) = 0;
    virtual task_t<location_page_t<spot_location_t>> list_spot_locations(
      spot_location_filter_t filter,
      location_page_request_t page = {}) = 0;
    virtual task_t<location_page_t<actor_location_t>> list_actor_locations(
      actor_location_filter_t filter,
      location_page_request_t page = {}) = 0;
    virtual task_t<location_page_t<route_location_t>> list_route_locations(
      route_location_filter_t filter,
      location_page_request_t page = {}) = 0;
    virtual task_t<location_page_t<location_topology_entry_t>> list_topology(
      location_topology_filter_t filter,
      location_page_request_t page = {}) = 0;
    virtual task_t<std::vector<location_service_summary_t>>
      list_service_summaries(location_service_summary_filter_t filter) = 0;
};
```

## 3. Location Store 구성

```cpp
namespace zlink::framework {

class location_store_t : public mesh_node_location_store_t,
                         public owner_lease_store_t,
                         public authority_store_t {
public:
    ~location_store_t() override = default;
    virtual task_t<std::int64_t> remove_all_by_owner(
      location_owner_token_t owner) = 0;
};

} // namespace zlink::framework
```

`location_store_t`는 descriptor, owner lease와 opaque authority CAS를 하나의
등록 capability로 제공한다. 여기서 필수 descriptor는 MeshNode다. ClientServer, fanout, generic peer와 route
capability는 해당 기능을 구성할 때 provider가 추가로 구현한다. Entry·User·Instance Spot owner와 Actor
transfer state machine은 Framework 내부 payload다. Provider는 payload 형식, Spot kind, phase와 recovery
cursor를 해석하지 않는다.
Checkpoint payload 보관은 별도 `checkpoint_store_t` capability이며 Snapshot policy나 accepted journal을
사용하는 host만 함께 등록한다.

## 4. 공식 Redis package

```cpp
namespace zlink::framework {

enum class routing_id_allocation_member_kind_t {
    mesh_node,
    fanout_publisher
};
struct routing_id_allocation_member_key_t {
    routing_id_allocation_member_kind_t kind;
    std::string name;
    auto operator<=>(const routing_id_allocation_member_key_t &) const = default;
};
struct routing_id_slot_allocation_member_t {
    routing_id_allocation_member_key_t key;
    std::string routing_id_prefix;
};
struct routing_id_slot_acquire_request_t {
    std::string group_name;
    std::vector<routing_id_slot_allocation_member_t> members;
    std::size_t slot_count;
    location_owner_token_t owner;
};

struct routing_id_slot_allocation_t {
    std::size_t slot;
    location_owner_token_t owner;
    std::chrono::system_clock::time_point store_now;
};
struct routing_id_slot_acquired_t { routing_id_slot_allocation_t allocation; };
struct routing_id_slot_group_exhausted_t {};
struct routing_id_slot_group_configuration_mismatch_t {
    std::vector<routing_id_slot_allocation_member_t> expected_members;
    std::size_t expected_slot_count;
    std::vector<routing_id_slot_allocation_member_t> actual_members;
    std::size_t actual_slot_count;
};
struct routing_id_slot_identity_mode_conflict_t {};

using routing_id_slot_acquire_result_t = std::variant<
  routing_id_slot_acquired_t,
  routing_id_slot_group_exhausted_t,
  routing_id_slot_group_configuration_mismatch_t,
  routing_id_slot_identity_mode_conflict_t>;

enum class routing_id_slot_release_result_t { released, ignored_stale };

struct routing_id_slot_allocation_snapshot_t {
    std::string group_name;
    std::vector<routing_id_slot_allocation_member_t> members;
    std::size_t slot_count;
    std::vector<routing_id_slot_allocation_t> allocations;
    std::chrono::system_clock::time_point store_now;
};

struct allocated_routing_id_t {
    std::string group_name;
    std::size_t slot;
    std::map<routing_id_allocation_member_key_t, zlink::routing_id_t> routing_ids;
};

class allocated_routing_id_provider_t {
public:
    virtual ~allocated_routing_id_provider_t() = default;
    virtual task_t<allocated_routing_id_t> wait_for_ready_allocation(
      std::string group_name) = 0;
};

class routing_id_slot_allocation_store_t {
public:
    virtual task_t<routing_id_slot_acquire_result_t> acquire_routing_id_slot(
      routing_id_slot_acquire_request_t request) = 0;
    virtual task_t<routing_id_slot_release_result_t> release_routing_id_slot(
      std::string group_name, std::size_t slot,
      location_owner_token_t owner) = 0;
    virtual task_t<routing_id_slot_allocation_snapshot_t> list_routing_id_slots(
      std::string group_name) = 0;
};

} // namespace zlink::framework

namespace zlink::framework::locations::redis {

struct redis_location_options_t {
    std::string connection_string;
    std::string key_prefix;

    redis_location_options_t &set_connection_string(std::string value);
    redis_location_options_t &set_key_prefix(std::string value);
};

class redis_location_store_t final : public location_store_t,
                                     public client_server_location_store_t,
                                     public fanout_location_store_t,
                                     public peer_location_store_t,
                                     public route_location_store_t,
                                     public checkpoint_store_t,
                                     public routing_id_slot_allocation_store_t,
                                     public location_change_stamp_store_t {
public:
    explicit redis_location_store_t(redis_location_options_t options);
    ~redis_location_store_t();
};

} // namespace zlink::framework::locations::redis
```

Acquire 결과는 `acquired`, `group exhausted`, `group configuration mismatch`와 `identity mode conflict`
네 variant로 닫혀 있다. 같은 owner token의 멱등 acquire는 같은 slot과 token을 반환한다. Release는
group, slot과 owner token이 모두 일치할 때만 `released`이며 stale token은 `ignored_stale`로 현재 claim을
유지한다. Host는 lifecycle 시작에서 owner lease를 한 번 claim한 뒤 같은 token을 모든 slot acquire
request에 넘긴다. Provider는 active host token을 slot 배정과 같은 원자 operation에서 확인한다.
Slot은 별도 TTL이나 token을 발급하지 않고 성공 result에는 입력과 같은 token만 반환한다.
Startup rollback은 확보한 slot을 먼저 release한 뒤 host lease를 마지막에 release한다. Member 목록은
kind, name과 routing ID prefix 순서로 정규화한다. MeshNode member의 name은
MeshName이고 fanout publisher member의 name은 ChannelName이다. 같은 이름도 kind가 다르면 서로 다른
member다. `slot_count`와 allocation slot은 `1..65535`이고 group member 수는 `1..255`다. 범위를 벗어난
builder 설정과 acquire request는 startup 또는 provider validation에서 거부한다. Group allocation 목록은 이
상한 안의 coherent snapshot이므로 page로 나누지 않는다. Readiness provider는 모든 listener bind와 MeshNode 또는 fanout publisher 전용 descriptor
게시가 끝난 뒤 결과를 반환한다.

`connection_string`과 비어 있지 않은 `key_prefix`는 필수다. Store가 Redis connection을 소유하며 소멸이
시작된 뒤 새 operation은 closed-store 오류로 완료된다.

Redis의 ClientServer descriptor kind는 `channel-server`이다. Key는 ChannelName과 ServerRid를
함께 사용하고 endpoint와 weight, generation, revision, runtime state, security identity, owner 정보를
전용 descriptor field로 저장한다. RouteMesh `mesh_node_descriptor_t`로 대체하지 않는다.

Fanout publisher descriptor kind는 `fanout-publisher`이다. Key는 ChannelName과 PublisherRid를 함께
사용하며 subscriber와 weight는 저장하지 않는다. MeshNode, ClientServer 또는 generic peer record와 조회
API를 재사용하지 않는다. Automatic subscriber는 같은 ChannelName의 유효하고 drain 중이 아닌 publisher
descriptor를 모두 연결한다.

MeshNode descriptor의 `spot_types`와 `object_capabilities`는 startup 전에 등록한 stable ID를 UTF-8 byte
순서로 정렬해 기록한다. Actor와 Instance Spot capability는 object kind와 type별 maintenance policy,
읽을 수 있는 state contract ID 집합과 현재 capacity를 한 항목에 함께 둔다. 서로 무관한 type과 state
contract를 flat set의 cross-product로 해석할 수 없다. `application_version`은 0 이상인 signed 64-bit
deployment ordinal이다. Object capacity, maintenance wave와 runtime state는
descriptor revision을 증가시켜 갱신한다. `spot_location_t`는 authority row를 Framework가 decode한
projection이며 endpoint를 복제하지 않고 owner node RID와 object generation을 보존한다. Redis provider는
`location_store_t`의 authority CAS와 별도 `checkpoint_store_t`를 모두 구현한다. Spot kind별 write와 Actor
transfer phase별 Redis operation은 public interface로 제공하지 않는다.

Descriptor의 key, RID, lifecycle generation, endpoint, security identity, owner token, application version,
ChannelName key set, Spot type set와 object capability의 kind·type·policy·readable contract set은 첫 admission 뒤
해당 lifecycle에서 바뀌지 않는다. Channel weight 값, capability capacity, maintenance wave와 runtime state만
mutable하다. Mutable update는 current owner token과 같은 lifecycle generation을 제시하고
`descriptor_revision`을 strictly 증가시켜야 한다. Provider는 stale revision이나 immutable field 변경을 원자적으로
거부하며 일부 field만 적용하지 않는다. ClientServer와 fanout descriptor도 같은 identity·revision fence를
적용한다.

`lifecycle_generation`은 0이 아닌 opaque equality token이다. 크기를 비교하거나 증가 순서를 추론하지 않는다.
Store-backed descriptor에서는 exact current owner lease와 descriptor lifecycle token을 사용한다. Manual
descriptor에서는 runtime이 CSPRNG nonce를 만들고 current connection handover로 이전 lifetime을 fence한다.
Caller가 값을 선택하는 option은 없다. Source lifetime 종류를 합치는 union은 runtime 내부에만 두며 public
interface로 노출하지 않는다. 수치 순서를 사용하는 field는 `descriptor_revision`뿐이다. 새 revision이 필요한데
값이 `9223372036854775807`이면 wrap하지 않고 host를 `error`로 seal한다. 이 exhaustion은 authority generation
exhaustion이나 routing ID `group_exhausted`와 서로 다른 실패다.
