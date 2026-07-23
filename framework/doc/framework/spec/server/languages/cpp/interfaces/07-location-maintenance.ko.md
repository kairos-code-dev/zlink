# C++ maintenance provider exact interface

[C++ exact interface 목차](README.ko.md)

## 1. Authority와 Relocation provider capability

`location_store_t`는 아래 opaque authority CAS를 필수 capability로 제공한다. `Recreate` 또는 `Snapshot`
factory가 하나라도 있는 host는 opaque state, accepted journal, full inventory와 replay payload를 보존하기 위해
Relocation Store를 정확히 하나 별도로 등록한다. `Disabled` factory만 있고 same-node lifecycle만 사용하는 host에는
Relocation payload가 필요하지 않다. 필요한 Store가 없거나 중복 등록되면 socket bind 전에 configuration error로
종료한다. Application service code는 두 provider operation을 직접 호출하지 않는다.

완료 가능한 모든 cross-node Actor·Spot 이동은 Relocation Store를 사용한다. `Recreate`도 accepted journal과
recovery payload를 저장하며 `Snapshot`은 application state를 추가로 저장한다. Same-node Actor join은 Relocation
payload를 만들지 않고, `Disabled` cross-node 이동은 capture 전에 거부한다.

```cpp
struct authority_key_t { std::string value; };
struct object_creation_target_t {
    std::string mesh_name;
    node_rid_t node_rid;
    std::uint64_t node_lifecycle_generation;
    location_owner_token_t owner;
};
enum class placement_allocation_state_t : std::uint8_t {
    pending = 1,
    active = 2
};
struct placement_allocation_t {
    placement_allocation_state_t state;
    placement_object_kind_t object_kind;
    std::string stable_type;
    object_creation_target_t target;
    std::uint32_t capacity_delta;
};
struct authority_snapshot_t {
    std::string store_version;
    std::vector<std::byte> payload;
    std::uint64_t object_generation;
    std::uint64_t authority_owner_generation;
    location_owner_token_t owner;
    placement_allocation_t allocation;
    std::chrono::system_clock::time_point store_now;
};

struct authority_missing_t {
    std::chrono::system_clock::time_point store_now;
};
using authority_read_result_t =
  std::variant<authority_missing_t, authority_snapshot_t>;

enum class authority_generation_transition_t {
    preserve = 1,
    new_owner = 2
};

struct authority_entry_t {
    authority_key_t key;
    authority_snapshot_t snapshot;
};
class authority_scan_cursor_t final {
public:
    explicit authority_scan_cursor_t(std::string encoded);
    std::string_view encoded() const noexcept;

private:
    std::string encoded_;
};
struct authority_page_t {
    std::vector<authority_entry_t> items;
    std::optional<authority_scan_cursor_t> next_cursor;
};
struct authority_scan_expired_t {};
using authority_scan_result_t =
  std::variant<authority_page_t, authority_scan_expired_t>;

struct relocation_capacity_fence_t {
    std::string value;
};

struct authority_put_t {
    std::vector<std::byte> payload;
    authority_generation_transition_t generation_transition;
    std::optional<location_owner_token_t> target_owner;
    std::optional<relocation_capacity_fence_t> relocation_capacity_fence;
};
struct authority_delete_t {};
using authority_mutation_t = std::variant<authority_put_t, authority_delete_t>;

struct authority_stored_t { authority_snapshot_t snapshot; };
struct authority_deleted_t {
    std::string store_version;
    std::chrono::system_clock::time_point store_now;
};
struct authority_conflict_t { authority_read_result_t current; };
struct authority_generation_exhausted_t {};
using authority_compare_exchange_result_t = std::variant<
  authority_stored_t, authority_deleted_t, authority_conflict_t,
  authority_generation_exhausted_t>;

class authority_store_t {
public:
    virtual ~authority_store_t() = default;
    virtual task_t<authority_read_result_t> read_authority(
      authority_key_t key,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<authority_compare_exchange_result_t> compare_exchange_authority(
      authority_key_t key,
      std::string expected_store_version,
      authority_mutation_t mutation,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<authority_scan_result_t> list_authorities(
      std::string prefix,
      std::optional<authority_scan_cursor_t> cursor,
      std::size_t limit,
      std::stop_token cancellation = {}) = 0;
};

struct object_creation_key_t {
    placement_object_kind_t kind;
    std::string global_id;
};

struct object_creation_intent_t {
    std::string stable_type;
    std::optional<placement_profile_t> placement_profile;
    std::optional<affinity_key_t> affinity_key;
    std::string request_content_reference;
    std::array<std::byte, 32> request_sha256;
    std::uint64_t request_encoded_size;
};

struct object_reserve_request_t {
    object_creation_key_t key;
    object_creation_intent_t intent;
    object_creation_target_t target;
    std::vector<std::byte> creating_payload;
    std::uint32_t pending_capacity_delta = 1;
};

struct object_reservation_fence_t {
    std::string reservation_id;
    std::string expected_store_version;
    std::uint64_t object_generation;
    std::uint64_t authority_owner_generation;
    object_creation_target_t target;
    std::uint32_t pending_capacity_delta;
};

struct object_reserved_t {
    object_reservation_fence_t fence;
    authority_snapshot_t creating;
};
struct object_already_exists_t { authority_snapshot_t current; };
struct object_type_mismatch_t { authority_snapshot_t current; };
struct object_placement_capacity_exhausted_t {};
struct object_reserve_conflict_t { authority_read_result_t current; };
using object_reserve_result_t = std::variant<
  object_reserved_t,
  object_already_exists_t,
  object_type_mismatch_t,
  object_placement_capacity_exhausted_t,
  object_reserve_conflict_t,
  authority_generation_exhausted_t>;

struct object_commit_request_t {
    object_creation_key_t key;
    object_reservation_fence_t fence;
    std::vector<std::byte> ready_payload;
};
struct object_committed_t { authority_snapshot_t ready; };
struct object_already_committed_t { authority_snapshot_t ready; };
struct object_commit_stale_t {};
struct object_commit_conflict_t { authority_read_result_t current; };
using object_commit_result_t = std::variant<
  object_committed_t,
  object_already_committed_t,
  object_commit_stale_t,
  object_commit_conflict_t,
  authority_generation_exhausted_t>;

struct object_abort_request_t {
    object_creation_key_t key;
    object_reservation_fence_t fence;
};
struct object_aborted_t {};
struct object_already_aborted_t {};
struct object_abort_stale_t {};
struct object_abort_conflict_t { authority_read_result_t current; };
using object_abort_result_t = std::variant<
  object_aborted_t,
  object_already_aborted_t,
  object_abort_stale_t,
  object_abort_conflict_t,
  authority_generation_exhausted_t>;

struct relocation_capacity_reserve_request_t {
    std::array<std::byte, 16> reservation_id;
    authority_key_t key;
    std::string expected_store_version;
    placement_object_kind_t object_kind;
    std::string stable_type;
    object_creation_target_t source;
    object_creation_target_t target;
    std::uint32_t capacity_delta = 1;
};
struct relocation_capacity_reserved_t {
    relocation_capacity_fence_t fence;
};
struct relocation_capacity_already_reserved_t {
    relocation_capacity_fence_t fence;
};
struct relocation_capacity_conflict_t {
    authority_read_result_t current;
};
struct relocation_capacity_target_unavailable_t {};
struct relocation_capacity_exhausted_t {};
using relocation_capacity_reserve_result_t = std::variant<
  relocation_capacity_reserved_t,
  relocation_capacity_already_reserved_t,
  relocation_capacity_conflict_t,
  relocation_capacity_target_unavailable_t,
  relocation_capacity_exhausted_t>;

enum class relocation_capacity_abort_result_t : std::uint8_t {
    aborted = 1,
    already_aborted = 2,
    already_committed = 3,
    stale = 4
};

class relocation_capacity_store_t {
public:
    virtual ~relocation_capacity_store_t() = default;
    virtual task_t<relocation_capacity_reserve_result_t>
    reserve_relocation_capacity(
      relocation_capacity_reserve_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<relocation_capacity_abort_result_t>
    abort_relocation_capacity(
      relocation_capacity_fence_t fence,
      std::stop_token cancellation = {}) = 0;
};

struct aggregate_id_t {
    std::array<std::byte, 16> value;
};

struct inventory_digest_t {
    std::array<std::byte, 32> value;
};

struct aggregate_participant_t {
    authority_key_t key;
    std::string expected_store_version;
    authority_generation_transition_t owner_transition;
    std::vector<std::byte> authority_payload;
    std::vector<std::byte> membership_mutation;
};

struct aggregate_prepare_request_t {
    aggregate_id_t aggregate_id;
    std::uint64_t aggregate_generation;
    std::vector<aggregate_participant_t> participants;
    inventory_digest_t inventory_digest;
    std::vector<relocation_capacity_fence_t> target_reservations;
    location_owner_token_t target_owner;
};

struct aggregate_fence_t {
    aggregate_id_t aggregate_id;
    std::uint64_t aggregate_generation;
};

struct aggregate_prepared_t { aggregate_fence_t fence; };
struct aggregate_already_prepared_t { aggregate_fence_t fence; };
struct aggregate_prepare_conflict_t {};
struct aggregate_prepare_stale_t {};
using aggregate_prepare_result_t = std::variant<
  aggregate_prepared_t,
  aggregate_already_prepared_t,
  aggregate_prepare_conflict_t,
  aggregate_prepare_stale_t,
  authority_generation_exhausted_t>;

enum class aggregate_commit_result_t : std::uint8_t {
    committed = 1,
    already_committed = 2,
    stale = 3,
    generation_exhausted = 4
};

enum class aggregate_abort_result_t : std::uint8_t {
    aborted = 1,
    already_aborted = 2,
    stale = 3
};

class object_creation_store_t {
public:
    virtual ~object_creation_store_t() = default;
    virtual task_t<object_reserve_result_t> reserve(
      object_reserve_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<object_commit_result_t> commit(
      object_commit_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<object_abort_result_t> abort(
      object_abort_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<aggregate_prepare_result_t> prepare_aggregate(
      aggregate_prepare_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<aggregate_commit_result_t> commit_aggregate(
      aggregate_fence_t fence,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<aggregate_abort_result_t> abort_aggregate(
      aggregate_fence_t fence,
      std::stop_token cancellation = {}) = 0;
};

struct relocation_stored_t {
    std::string reference;
    std::uint32_t checksum_crc32c;
    std::chrono::system_clock::time_point expires_at;
    std::chrono::system_clock::time_point store_now;
};
struct relocation_found_t { std::vector<std::byte> payload; };
struct relocation_missing_t {};
using relocation_read_result_t =
  std::variant<relocation_found_t, relocation_missing_t>;
enum class relocation_delete_result_t { deleted = 0, missing = 1 };
struct relocation_renewed_t {
    std::chrono::system_clock::time_point expires_at;
    std::chrono::system_clock::time_point store_now;
};
struct relocation_renew_missing_t {};
using relocation_renew_result_t =
  std::variant<relocation_renewed_t, relocation_renew_missing_t>;

class relocation_store_t {
public:
    virtual ~relocation_store_t() = default;
    virtual task_t<relocation_stored_t> put_relocation(
      std::vector<std::byte> payload,
      std::chrono::hours retention,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<relocation_read_result_t> get_relocation(
      std::string reference,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<relocation_renew_result_t> renew_relocation(
      std::string reference,
      std::chrono::hours retention,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<relocation_delete_result_t> delete_relocation(
      std::string reference,
      std::stop_token cancellation = {}) = 0;
};

```

`checksum_crc32c`는 저장된 immutable root bytes의 CRC32C(Castagnoli)를 나타내는 exact unsigned 32-bit 값이다.
Runtime은 이 값과 Location authority에 publish할 checksum이 정확히 같은지 검증한다.

`reserve(...)`는 Framework가 encode한 `creating_payload`를 해석하지 않고 Missing authority를 Creating으로
바꾸며 final object·owner generation, durable creation intent와
target pending capacity를 하나의 atomic operation에서 확정한다. Request content는 최대 1 MiB이며 reference와
SHA-256이 일치해야 한다. Reservation에는 TTL을 두지 않는다. `commit(...)`은 exact reservation에서
target descriptor lifecycle과 owner lease를 다시 확인하고 Framework가 encode한 `ready_payload`를 해석하지
않은 채 Creating→Ready와 pending 감소·active 증가를 함께 처리한다. Stale이면 mutation 0으로 reservation을
유지한다. `abort(...)`는 current lifecycle·lease를 요구하지 않고 reservation에 고정한 exact Creating authority와
이전 target pending capacity만 해제한다. 같은 fence의 Commit과 Abort는 각각 `object_already_committed_t`와
`object_already_aborted_t`를 반환하고 다른 reservation generation은 stale 결과다. Counter가 소진되면 mutation과
counter 소비 없이 `authority_generation_exhausted_t`를 반환한다. Reserve의 결과는 Reserved, AlreadyExists,
TypeMismatch, PlacementCapacityExhausted, Conflict와 GenerationExhausted로 닫혀 있다. Provider는 object kind를
해석하지 않는다.

Existing object relocation은 creation reservation을 재사용하지 않는다. `reserve_relocation_capacity`는
non-zero 128-bit reservation ID, current authority version, kind·stable type, source·target descriptor와 exact
owner token을 검증하고 양수인 `capacity_delta`만큼 target pending capacity만 예약한다. 같은 ID와 exact request는
`relocation_capacity_already_reserved_t`, 다른 내용은 conflict다. Standalone Actor의 `new_owner` CAS와 aggregate
commit만 fence를 소비하며 source active 감소와 target pending-to-active를 authority mutation과 같은 transaction에서
처리한다. Commit 전 abort는 pending을 해제하고 reservation은 TTL로 만료시키지 않는다.
Standalone `new_owner` fence가 reserved 상태가 아니거나 authority key·expected StoreVersion·source·target
owner와 일치하지 않거나 이미 committed·aborted됐으면 current authority read를 담은
`authority_conflict_t`다. CAS transaction은 request source를 durable current Active allocation과 다시
비교하고 target descriptor lifecycle과 target owner lease만 live/exact로 확인한다. Source descriptor row나
source owner lease가 stale·missing이어도 allocation match가 유지되면 commit할 수 있다. Target이 stale이면
authority row, capacity와 fence state의 mutation은 0이다.

`aggregate_id_t`는 all-zero가 아닌 128-bit 값이고 aggregate generation은
`1..9223372036854775807`이다. `participants`는 authority key의 canonical byte order로 정렬하며 중복이 없는
bounded canonical participant set이다. Participant는 최대 1024개이며 prepare request와 durable aggregate
record의 encoded 크기는 각각 최대 1 MiB다. `inventory_digest`는 participant set과 mutation 전체를 canonical
encode한 bytes의 SHA-256이다. Provider는 participant payload와 membership mutation을 해석하지 않는다.
`target_reservations`는 `new_owner` participant와 정확히 일대일이어야 한다. Fence reservation의 authority
key·expected StoreVersion·source owner·target owner는 participant expectation, current authority owner와
aggregate `target_owner`에 정확히 일치해야 한다. Request source는 durable Active allocation과 exact match해야
하고 target descriptor lifecycle·owner lease만 live/exact로 확인한다. 누락·중복·추가 fence와 값 불일치는
conflict이며 mutation은 0이다.
`prepare_aggregate(...)`는 같은 transaction에서 Reserved fence를 aggregate ID·generation에 bind해 Prepared로
바꾸고 durable
prepared record를 만든다. `commit_aggregate(...)`는 모든 owner, AuthorityOwnerGeneration과 membership visibility를
한 transaction에서 전환한다. Bind된 fence의 direct abort는 stale이고 다른 aggregate prepare는 conflict다.
Exact duplicate prepare만 already-prepared다. Commit은 source Active allocation match와 target descriptor
lifecycle·owner lease를 다시 확인한다. Source descriptor row·lease는 stale·missing이어도 allocation match가
유지되면 허용하고 target이 stale이면 mutation 없이 fence를 bind 상태로 유지한다.
`abort_aggregate(...)`는 commit 전 prepared record와 bind된
fence의 target pending을 정리하고 fence를 aborted로 닫는다. 같은
fence는 idempotent하고 다른 generation은 stale이며 expectation 하나가 다르면 participant, reservation, index와
counter를 하나도 변경하지 않는다.

`compare_exchange_authority`는 Active `Found`의 current StoreVersion 문자열을 직접 받는다. Missing authority를
만드는 public transition은 없다. `authority_put_t::target_owner`와 `relocation_capacity_fence`는 `preserve`에서
`std::nullopt`, `new_owner`에서 필수다. Provider는 exact target owner
lease를 CAS와 같은 transaction에서 검증하고 성공한 snapshot의 `owner`로 기록하며 opaque payload에서 owner
metadata를 해석하지 않는다. `preserve`는 stored current owner lease, `new_owner`는 `target_owner` lease를
검증한다. Missing·stale lease는 current authority read를 가진
`authority_conflict_t`로 끝나고 mutation은 0이다. Invalid `target_owner` 조합은 provider 호출 전에
`std::invalid_argument`로 거부한다. Missing result에 fake StoreVersion을 넣지 않는다.
`list_authorities`의 first page는 `cursor=nullopt`로 요청한다. Provider는 한 snapshot을 만들고 이어지는
page에 필요한 모든 상태를 하나의 `authority_scan_cursor_t`에 담는다. 다음 page는 직전 page의
`next_cursor` 객체를 해석하거나 다시 조립하지 않고 그대로 넘긴다. Cursor의 UTF-8 encoded 크기는
`1..4096` bytes이며 empty cursor는 허용하지 않는다. Constructor는 범위를 검증하고 값을 복사하므로
만든 뒤에는 바뀌지 않는다. Provider는 snapshot에 포함된 key incarnation을 scan 전체에서 각각 한 번만
반환한다. Concurrent delete는 Framework의 exact read에서 missing으로 제거되고 snapshot 뒤의
create·recreate는 다음 scan에서 반환된다. Framework는
각 candidate를 exact read한 뒤 current StoreVersion으로 CAS한다. 등록한 MeshName scope의 initial scan이
완료되기 전에는 Serving을 게시하지 않고, 이후 scan은 background recovery로 반복한다.
Provider가 cursor가 가리키는 scan을 만료시켰으면 이어지는 page 요청은 `authority_scan_expired_t`를
반환한다. Framework는 부분 결과를 사용하지 않고 first page부터 새 scan을 시작한다.
한 authority opaque payload의 encoded 크기는 최대 1 MiB다. Scan `limit`은 `1..1000`이고 provider는
encoded page 4 MiB에 먼저 도달하면 요청보다 적은 entry와 `next_cursor`를 반환한다. 이 byte limit을
바꾸는 public option은 없다. Hot authority row는 compact metadata와 replay cursor만 보관하며 complete terminal
reply bytes는 relocation stream에 저장한다.
Page는 opaque key와 payload를 반환하며 provider는 payload를 해석하지 않는다. Framework operational
query가 Actor projection을 decode하며 이 목록은 routing authority로 사용하지 않는다. Provider domain은
영구적인 global object generation, authority owner generation과 Store revision counter를 각각 하나씩 유지한다.
Generic Reserve만 object와 initial owner generation, Pending allocation을 발급한다. Commit은 같은 allocation을
Active로 바꾸고 Abort는 Pending allocation을 제거한다. `new_owner`는 owner generation만 증가시키고 target
Active allocation으로 바꾸며 `preserve`는 generation과 allocation을 유지한다. Delete는 Active allocation의
capacity delta를 감소시키고 row를 제거한다. Pending row의 generic CAS는 conflict와 mutation 0이다. Stored
mutation과 delete는 global Store revision으로 fence하고 per-key counter나 version tombstone을 유지하지 않는다.
Scan lease가 활성화된 동안만 scan snapshot을 유지하기 위한 tombstone을 bounded로
유지할 수 있다. Payload에 generation을 중복 encode하지 않는다. Authority row는
TTL을 갖지 않고 explicit fenced delete가 성공할 때까지 유지된다. Owner·coordinator lease는 별도 token row에
저장하며 lease 만료나 reclaim이 authority row를 삭제하거나 수정하지 않는다. Framework는
relocation put과 renew의 retention에 24시간을 전달한다. Authority의 current relocation reference를 확인한
owner 또는 recovery coordinator만 renew를 호출한다. Renew 성공은 provider clock의 새 expiry와 Store time을
반환한다. Missing reference는 `relocation_renew_missing_t` 정상 결과이며 retention은 application option이
아니다. Framework는 logical relocation payload를 immutable 64 MiB chunk 최대 4096개와 root manifest로 내부에서
나누므로 logical state ceiling은 256 GiB다. Relocation Store의 opaque put/get interface는 바꾸지 않으며
chunk 크기, 개수와 manifest를 설정하는 public option도 제공하지 않는다. Capture가 ceiling을 넘으면 seal을
되돌려 normal messaging을 다시 허용하고 Retire 결과를 `blocked`로 종료한다. 일반 message의 negotiated
effective bound는 relocation chunk 크기 때문에 줄이지 않는다. Provider는 key, authority payload와 relocation
payload를 해석하지 않는다.

Location Store는 phase, relocation reference와 checksum, canonical participant set, participant mutation,
aggregate generation, membership·aggregate count와 inventory digest를 authority로 소유한다. Relocation manifest는
opaque state, accepted journal, full inventory와 replay payload를 찾는 용도일 뿐 authority가 아니다. Framework는
manifest를 먼저 저장하고 digest가 Location Store의 canonical inventory digest와 일치하는지 확인한 뒤 authority
CAS로 reference를 공개한다. Root를 교체할 때도 새 root 저장과 digest 검증을 먼저 수행하고, CAS가 성공한 뒤
이전 reference를 release한 다음 이전 payload를 삭제한다. Relocation payload 사용을 끝낼 때는 Location Store에서
reference 사용 종료를 CAS한 뒤 Relocation Store에서 payload를 삭제한다. 두 Store 사이 transaction은 요구하지 않는다.
Authority가 참조하는 Relocation payload가 없거나 digest가 일치하지 않으면 `RelocationDataLost`로 종료하며 이전
owner로 rollback하지 않는다. Restore와 accepted journal replay는 manifest digest와 `inventory_digest`가 exact
match인 경우에만 시작한다.

세 counter는 `1..9223372036854775807` 범위이며 wrap하거나 재사용하지 않는다. CAS 성공에 새
StoreVersion, object generation 또는 authority owner generation이 필요한데 해당 global counter가 최댓값이면
provider는 non-retriable `authority_generation_exhausted_t`를 반환한다. 이 결과는 row, index와 모든 counter를
바꾸거나 값을 소비하지 않는다. 외부 상태가 바뀌지 않은 채 같은 expectation을 다시 제출하면 같은 결과를
반환한다. Transport 또는 provider exception은 이 닫힌 결과와 구분한다. Framework는 기존 lifecycle failure로
operation을 닫으며 application용 error enum을 추가하지 않는다.

Framework가 provider에 넘긴 authority와 relocation 입력 buffer는 asynchronous operation이 끝날 때까지
유효하며 바뀌지 않는다. Provider가 완료 뒤에도 buffer를 보관하려면 먼저 복사해야 한다. Provider가 성공
result로 반환한 payload storage는 result가 사용되는 동안 안정적이어야 하며 provider는 반환 뒤 그 storage를
수정하거나 다른 result에 재사용하지 않는다. Mutable buffer 기반 adapter는 provider boundary에서 snapshot을
만든다.

Cancellation이 provider 호출 전에 이미 요청되었으면 Framework는 provider operation을 시작하지 않으므로 I/O와
commit이 없다. Provider operation을 시작한 뒤 waiter가 취소되거나 오류로 끝나면 commit 여부는 알 수 없다.
Authority CAS는 같은 exact key와 expectation의 StoreVersion을 다시 읽어 결과를 reconcile한 뒤 retry한다.
Relocation put은 content-addressed reference를 확인한 뒤 idempotent하게 retry한다. Authority에 연결되지 않은
committed put은 orphan이며 고정 retention과 cleanup으로 제거한다. 이 의미를 표현하는 public result는 추가하지
않는다.

Location record, owner lease, watch와 Redis provider의 exact declaration은
[Location Store·Redis](07-location-store.ko.md)가 소유한다. 이 문서는 stateful maintenance에서 추가로
필요한 authority와 Relocation provider capability만 소유한다. Authority Store를 root에 별도로 등록하는
member는 제공하지 않는다. Location Store와 Relocation Store 등록 member는
[Configuration과 host](02-configuration-host.ko.md)의 `zlink_framework_options_t`가 소유한다.
