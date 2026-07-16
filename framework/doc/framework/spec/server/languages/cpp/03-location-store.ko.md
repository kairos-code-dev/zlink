# C++ Location Store·Redis 공개 인터페이스

[C++ 계약 목차](README.ko.md) · [Location Runtime](../../40-location-runtime.ko.md) ·
[Redis Location Store](../../41-location-store-redis.ko.md)

## 1. Root 등록과 option

```cpp
namespace zlink::framework {

struct location_options_t {
    std::chrono::milliseconds heartbeat_interval{10000};
    std::chrono::milliseconds owner_lease_ttl{30000};
    std::chrono::milliseconds polling_interval{1000};
    std::chrono::milliseconds store_failure_grace{30000};
    std::chrono::milliseconds routing_id_fencing_margin{5000};
    std::chrono::milliseconds owner_lease_renew_timeout{3000};
};

class zlink_builder_t {
public:
    zlink_builder_t &add_location_store(std::shared_ptr<location_store_t> store);
    location_options_t &configure_locations();
};

} // namespace zlink::framework
```

Store는 host마다 하나만 등록한다. 자동 discovery, remote Spot·Actor 위치, routing ID 자동 할당 또는
Actor transfer를 설정했는데 필요한 capability가 없으면 host는 socket bind 전에 구성 오류로 종료한다.
Manual peer와 process-local Spot·Actor만 사용하는 host는 store 없이 시작할 수 있다.

여섯 duration은 모두 양수여야 한다. Routing ID 자동 할당을 사용할 때 heartbeat, renew timeout, lease
TTL과 fencing margin은 [Location Runtime §2.4](../../40-location-runtime.ko.md#24-owner-lease)의 시간 관계를
만족해야 하며 위 기본값은 그 관계를 만족한다.

## 2. Store-neutral record와 capability

```cpp
namespace zlink::framework {

enum class location_write_intent_t { new_claim, renew, takeover };
enum class location_write_status_t { stored, ignored_stale, rejected_conflict };

struct location_write_result_t {
    location_write_status_t status;
    std::uint64_t generation;
    std::chrono::system_clock::time_point updated_at;
};

struct location_owner_token_t { std::string owner_id; std::uint64_t generation; };
struct owner_lease_t {
    std::string owner_id;
    zlink::routing_id_t node_rid;
    std::chrono::system_clock::time_point lease_expires_at;
    std::chrono::system_clock::time_point updated_at;
};
struct owner_lease_renewal_t {
    std::chrono::system_clock::time_point lease_expires_at;
    std::chrono::system_clock::time_point store_now;
};
struct owner_lease_snapshot_t {
    std::vector<owner_lease_t> leases;
    std::chrono::system_clock::time_point store_now;
};

struct mesh_node_descriptor_t {
    std::string mesh_name;
    zlink::routing_id_t rid;
    std::uint64_t lifecycle_generation;
    std::uint64_t descriptor_revision;
    std::string endpoint;
    std::map<std::string, int> channel_weights;
    bool draining;
    std::string security_identity;
    std::string owner_id;
    std::chrono::system_clock::time_point updated_at;
};
struct mesh_node_descriptor_key_t { std::string mesh_name; zlink::routing_id_t rid; };

struct spot_location_t {
    std::string mesh_name;
    zlink::routing_id_t spot_rid;
    std::uint64_t spot_generation;
    zlink::routing_id_t owner_node_rid;
    std::uint64_t owner_node_generation;
    spot_kind_t spot_kind;
    std::string spot_type;
    std::string owner_id;
    std::chrono::system_clock::time_point updated_at;
};
struct spot_location_key_t { std::string mesh_name; zlink::routing_id_t spot_rid; };

struct actor_location_t {
    std::string mesh_name;
    std::string actor_id;
    std::string actor_type;
    actor_ref_t actor_ref;
    zlink::routing_id_t owner_node_rid;
    std::uint64_t owner_node_generation;
    zlink::routing_id_t spot_rid;
    spot_kind_t spot_kind;
    std::uint64_t membership_epoch;
    std::string owner_id;
    std::chrono::system_clock::time_point updated_at;
};
struct actor_location_key_t { std::string mesh_name; std::string actor_id; };

class mesh_node_location_store_t {
public:
    virtual task_t<location_write_result_t> update_mesh_node(
      mesh_node_descriptor_t descriptor, location_write_intent_t intent) = 0;
    virtual task_t<location_write_status_t> remove_mesh_node(
      mesh_node_descriptor_key_t key, location_owner_token_t owner) = 0;
    virtual task_t<std::vector<mesh_node_descriptor_t>> list_mesh_nodes(
      std::string mesh_name) = 0;
};

class spot_location_store_t {
public:
    virtual task_t<location_write_result_t> update_spot(
      spot_location_t location, location_write_intent_t intent) = 0;
    virtual task_t<location_write_status_t> remove_spot(
      spot_location_key_t key, location_owner_token_t owner) = 0;
    virtual task_t<std::optional<spot_location_t>> resolve_spot(
      spot_location_key_t key) = 0;
};

class actor_location_store_t {
public:
    virtual task_t<location_write_result_t> update_actor(
      actor_location_t location, location_write_intent_t intent) = 0;
    virtual task_t<location_write_status_t> remove_actor(
      actor_location_key_t key, location_owner_token_t owner) = 0;
    virtual task_t<std::optional<actor_location_t>> resolve_actor(
      actor_location_key_t key) = 0;
};

class owner_lease_store_t {
public:
    virtual task_t<owner_lease_renewal_t> renew_owner_lease(
      std::string owner_id, zlink::routing_id_t node_rid,
      std::chrono::milliseconds lease_ttl) = 0;
    virtual task_t<bool> remove_owner_lease(std::string owner_id) = 0;
    virtual task_t<owner_lease_snapshot_t> list_owner_leases() = 0;
};

} // namespace zlink::framework
```

## 3. Actor transfer authority

```cpp
namespace zlink::framework {

enum class actor_transfer_state_t { prepared, committed, activated, aborted };
enum class actor_transfer_write_status_t {
    stored, not_found, ignored_stale, rejected_conflict, invalid_state
};
// UUID 128-bit를 network byte order로 보관한다.
using transfer_id_t = std::array<std::byte, 16>;

struct actor_transfer_record_t {
    std::string mesh_name;
    std::string actor_id;
    transfer_id_t transfer_id;
    actor_ref_t source;
    actor_ref_t target;
    std::uint64_t expected_actor_generation;
    std::uint64_t expected_membership_epoch;
    std::set<zlink::routing_id_t> participants;
    actor_transfer_state_t state;
    std::string recovery_owner_id;
    std::chrono::system_clock::time_point recovery_lease_expires_at;
    std::chrono::system_clock::time_point updated_at;
};

struct actor_transfer_prepare_request_t {
    std::string mesh_name;
    std::string actor_id;
    transfer_id_t transfer_id;
    actor_ref_t source;
    actor_ref_t target;
    std::uint64_t expected_actor_generation;
    std::uint64_t expected_membership_epoch;
    std::set<zlink::routing_id_t> participants;
    std::string recovery_owner_id;
    std::chrono::milliseconds recovery_lease_ttl;
};

struct actor_transfer_write_result_t {
    actor_transfer_write_status_t status;
    std::optional<actor_transfer_record_t> record;
};

class actor_transfer_store_t {
public:
    virtual task_t<actor_transfer_write_result_t> prepare_actor_transfer(
      actor_transfer_prepare_request_t request) = 0;
    virtual task_t<actor_transfer_write_result_t> commit_actor_transfer(
      std::string mesh_name, std::string actor_id, transfer_id_t transfer_id,
      std::string recovery_owner_id) = 0;
    virtual task_t<actor_transfer_write_result_t> activate_actor_transfer(
      std::string mesh_name, std::string actor_id, transfer_id_t transfer_id,
      std::string recovery_owner_id) = 0;
    virtual task_t<actor_transfer_write_result_t> abort_actor_transfer(
      std::string mesh_name, std::string actor_id, transfer_id_t transfer_id,
      std::string recovery_owner_id) = 0;
    virtual task_t<actor_transfer_write_result_t> take_over_actor_transfer(
      std::string mesh_name, std::string actor_id, transfer_id_t transfer_id,
      std::string successor_owner_id, std::chrono::milliseconds recovery_lease_ttl) = 0;
    virtual task_t<std::optional<actor_transfer_record_t>> resolve_actor_transfer(
      std::string mesh_name, std::string actor_id) = 0;
};

class location_store_t : public mesh_node_location_store_t,
                         public spot_location_store_t,
                         public actor_location_store_t,
                         public owner_lease_store_t,
                         public actor_transfer_store_t {
public:
    virtual task_t<std::int64_t> remove_all_by_owner(std::string owner_id) = 0;
};

} // namespace zlink::framework
```

Prepare는 active transfer 부재, Actor generation과 membership epoch를 한 원자 operation에서 비교한다.
Commit은 target owner와 정확히 다음 membership epoch를 함께 기록한다. Takeover는 recovery lease 만료,
participant set과 현재 Actor location을 같은 operation에서 확인한다.

`transfer_id_t`의 16 bytes는 UUID network byte order다. Redis extension은 이 값을 소문자
`8-4-4-4-12` UUID UTF-8 문자열로 변환해 transfer key와 active-transfer value에 기록하고, 읽을 때
같은 byte order로 복원한다. 정확한 key, field와 byte fixture는
[Redis Location Store §3.1](../../41-location-store-redis.ko.md#31-actor-transfer-authority)이 소유한다.

## 4. 공식 Redis package

```cpp
namespace zlink::framework {

struct routing_id_slot_allocation_member_t {
    std::string mesh_name;
    std::string routing_id_prefix;
};
struct routing_id_slot_acquire_request_t {
    std::string group_name;
    std::vector<routing_id_slot_allocation_member_t> members;
    std::size_t slot_count;
    std::string owner_id;
    std::chrono::milliseconds lease_ttl;
};

struct routing_id_slot_allocation_t {
    std::size_t slot;
    location_owner_token_t owner;
    std::chrono::system_clock::time_point lease_expires_at;
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
    std::map<std::string, zlink::routing_id_t> mesh_node_routing_ids;
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

enum class location_change_scope_kind_t {
    mesh_node, spot, actor, owner_lease, actor_transfer
};
struct location_change_stamp_scope_t {
    location_change_scope_kind_t kind;
    std::optional<std::string> mesh_name;
};

class location_change_stamp_store_t {
public:
    virtual task_t<std::uint64_t> get_change_stamp(
      location_change_stamp_scope_t scope) = 0;
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
                                     public routing_id_slot_allocation_store_t,
                                     public location_change_stamp_store_t {
public:
    explicit redis_location_store_t(redis_location_options_t options);
    ~redis_location_store_t();
};

} // namespace zlink::framework::locations::redis
```

Acquire 결과는 `acquired`, `group exhausted`, `group configuration mismatch`와 `identity mode conflict`
네 variant로 닫혀 있다. 같은 owner의 멱등 acquire는 같은 slot과 owner generation을 반환한다. Release는
group, slot과 owner token이 모두 일치할 때만 `released`이며 stale token은 `ignored_stale`로 현재 claim을
유지한다. Member 목록은 MeshName의 UTF-8 byte 순으로 정규화한다.

`connection_string`과 비어 있지 않은 `key_prefix`는 필수다. Store가 Redis connection을 소유하며 소멸이
시작된 뒤 새 operation은 closed-store 오류로 완료된다.
