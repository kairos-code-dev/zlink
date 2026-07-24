# C++ Actor exact interface

[C++ exact interface 목차](README.ko.md) · [Actor model](../../../22-actor-model.ko.md) ·
[Spot·Actor membership](../../../23-spot-actor.ko.md)

## 1. Identity와 maintenance policy

Actor가 사용하는 `actor_context_t`의 exact declaration은
[Spot interface](04-spots.ko.md)가 소유한다.

```cpp
namespace zlink::framework {

class actor_id_t final {
public:
    explicit actor_id_t(std::string value);
    std::string_view value() const noexcept;
    auto operator<=>(const actor_id_t &) const = default;
};

class actor_ref_t final {
public:
    actor_ref_t(actor_id_t actor_id,
      std::uint64_t object_generation,
      std::string mesh_name,
      node_rid_t node_rid);

    const actor_id_t &actor_id() const noexcept;
    std::uint64_t object_generation() const noexcept;
    std::string_view mesh_name() const noexcept;
    const node_rid_t &node_rid() const noexcept;
};

class actor_t {
public:
    virtual ~actor_t() = default;
    const actor_id_t &actor_id() const noexcept;
    actor_context_t &context() noexcept;
    const actor_context_t &context() const noexcept;
    virtual void configure() {}
};

template <typename TActor>
  requires std::derived_from<TActor, actor_t>
class actor_factory_t {
public:
    virtual ~actor_factory_t() = default;
    virtual task_t<std::shared_ptr<TActor>> create(
      actor_id_t actor_id,
      actor_context_t &context,
      std::stop_token operation_cancellation) = 0;
};

template <typename TActor>
class actor_relocation_adapter_t {
public:
    virtual ~actor_relocation_adapter_t() = default;
    virtual task_t<std::vector<std::byte>> capture(
      TActor &actor,
      std::stop_token operation_cancellation) = 0;
    virtual task_t<void> restore(
      TActor &actor,
      std::vector<std::byte> payload,
      std::stop_token operation_cancellation) = 0;
};

template <typename TInstance>
class relocation_policy_t {
public:
    static relocation_policy_t disabled();
    static relocation_policy_t recreate();

    template <typename TAdapter>
    static relocation_policy_t snapshot();
};

} // namespace zlink::framework
```

`actor_id_t`는 UTF-8 `1..255` byte exact global identity다. Constructor는 invalid 값을
`std::invalid_argument`로 거부하고 trim, case folding과 Unicode normalization을 적용하지 않는다.
`actor_ref_t`는 global ActorId, non-zero `1..9223372036854775807` ObjectGeneration과 조회 시점의
MeshName·NodeRid를 담는 immutable location snapshot이다. 일반 message target으로 사용하지 않는다. 별도
`actor_ref_snapshot_t`는 제공하지 않는다.

`actor_t`는 ActorId와 Framework가 연결한 `actor_context_t`를 소유하는 typed lifecycle base다. Framework는
`actor_factory_t<TActor>::create(...)`로 concrete Actor를 만든 뒤 `configure()`를 호출한다. Factory는 전달받은
ActorId·context와 cancellation을 사용하며 다른 owner RID, relocation phase 또는 Store token을 받지 않는다.

모든 Actor factory는 `relocation_policy_t<TActor>`를 명시한다. `snapshot<TAdapter>()`의 `TAdapter`는
`actor_relocation_adapter_t<TActor>`를 구현해야 하며 다른 adapter type이면 socket bind 전에 configuration error로
실패한다. Adapter는 application state를 opaque byte vector로만 주고받으며 typed state, 별도 contract identifier,
message wrapper, authority, relocation reference, relocation phase와 operation ID를 받지 않는다.

Framework는 Snapshot policy의 cross-node Actor materialization에서만 adapter를 호출한다. 여기에는 maintenance
이관, remote User·Entry Spot join과 whole User Spot relocation의 각 Actor participant가 포함된다. Same-node join과
relocation에서는 adapter를 호출하지 않으며 Disabled cross-node operation은 `capture(...)` 전에 거부한다. Recreate
policy도 application payload를 capture하거나 restore하지 않는다. Whole User Spot relocation에서는 Spot root에
`spot_relocation_adapter_t<TSpot>`를 사용하고 각 Actor participant에는 이 Actor adapter를 사용한다.

`capture(...)` 결과는 최대 64 MiB이며 빈 vector는 유효하다. 반환한 byte vector의 소유권은 Framework로 이동하고,
`restore(...)`에 전달한 byte vector는 해당 비동기 호출이 소유한다. Capture가 throw하거나 failed task로 끝나면
durable abort와 source normalization 뒤 admission을 복원한다. Restore가 실패한 instance는 폐기하고 새 attempt의
factory가 만든 instance에 같은 immutable payload를 적용한다. Framework가 operation deadline 때문에 callback을
취소하면 `deadline_exceeded`로 분류한다. Target replacement와 response loss 때문에 두 method는 at-least-once
호출될 수 있고 stale attempt와 successor 호출이 겹칠 수 있으므로 구현은 retry-safe해야 한다. Framework는
adapter의 external side effect에 exactly-once를 보장하지 않는다.

## 2. ID-only messaging

```cpp
namespace zlink::framework {

class actor_send_call_t {
public:
    actor_send_call_t &metadata(std::string key, std::string value);
    task_t<submit_result_t> submit();
};

class actor_request_call_t {
public:
    actor_request_call_t &timeout(std::chrono::milliseconds timeout);
    actor_request_call_t &metadata(std::string key, std::string value);

    template <typename TReply>
    task_t<TReply> async();

    template <typename TReply>
    task_t<TReply> yield();

    task_t<message_t> async_message();
    task_t<message_t> yield_message();
};

class actor_client_t {
public:
    virtual ~actor_client_t() = default;

    template <typename TMessage>
    actor_send_call_t send(actor_id_t actor_id, TMessage message);

    template <typename TRequest>
    actor_request_call_t request(actor_id_t actor_id, TRequest request);
};

} // namespace zlink::framework
```

Actor send와 request는 global `actor_id_t`만 target으로 받는다. MeshName, ActorRef, owner NodeRid와 current
SpotId를 받는 overload는 없다. Runtime은 positive Ready route만 cache하고 negative cache를 두지 않는다.
Stale route는 `actor_location_stale`, exact-ref generation mismatch는 `actor_generation_stale`로 구분한다.

## 3. Single-use manager operation

```cpp
namespace zlink::framework {

struct actor_create_existing_t {
    actor_ref_t actor;
};

struct actor_create_created_t {
    actor_ref_t actor;
    std::optional<message_t> reply;
};

struct actor_create_rejected_t {
    std::optional<message_t> reply;
};

using actor_create_result_t = std::variant<
  actor_create_existing_t,
  actor_create_created_t,
  actor_create_rejected_t>;

class actor_create_call_t {
public:
    actor_create_call_t(actor_create_call_t &&) noexcept;
    actor_create_call_t &operator=(actor_create_call_t &&) noexcept;
    actor_create_call_t(const actor_create_call_t &) = delete;
    actor_create_call_t &operator=(const actor_create_call_t &) = delete;

    actor_create_call_t &in_mesh(std::string mesh_name);
    actor_create_call_t &creation_request(message_t request);

    template <typename TCreation>
    actor_create_call_t &creation_request(TCreation request);

    actor_create_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<actor_create_result_t> submit();
};

class actor_manager_t {
public:
    virtual ~actor_manager_t() = default;
    virtual actor_create_call_t create(
      actor_id_t actor_id, std::string stable_type) = 0;
    virtual actor_create_call_t get_or_create(
      actor_id_t actor_id, std::string stable_type) = 0;
    virtual task_t<std::optional<actor_ref_t>> find(actor_id_t actor_id) = 0;
    virtual task_t<std::optional<spot_ref_t>> find_spot(
      actor_id_t actor_id) = 0;
    virtual task_t<bool> destroy(actor_ref_t actor) = 0;
};

} // namespace zlink::framework
```

Call object는 option마다 최대 한 번 설정하고 `submit()`도 한 번만 호출한다. Duplicate option은
`invalid_configuration`, 두 번째 submit은 `already_submitted`다. `in_mesh`를 생략했을 때 object role Mesh가
하나면 자동 선택하고, 0개면 `object_client_not_configured`, 여러 개면 `mesh_selection_required`다. Unknown
Mesh는 `mesh_not_found`다.

`Create`는 existing identity에 `actor_already_exists`를 반환하고 새 attempt에서는
`actor_create_created_t` 또는 `actor_create_rejected_t`를 반환한다. `GetOrCreate`는 같은 stable type의 Ready
Actor에서 factory와 creation callback을 호출하지 않고 `actor_create_existing_t`를 반환하며, Creating
attempt가 있으면 authority 변경을 기다린다. Ready면 `actor_create_existing_t`, rejection cleanup이면 새
reservation으로 자신의 request를 실행한다. 서로 다른 operation은 앞선 rejected reply를 공유하지 않고
동일한 operation ID retry만 terminal result를 재사용한다. Type이 다르면 `actor_type_mismatch`다. Deadline은 resolve, 대기, reservation, factory와
Ready 전체에 적용한다. `Find`는 Ready ref만 반환하며 생성하지 않는다. `FindSpot`은 current User Spot
membership의 Ready `spot_ref_t`만 반환하고 Entry membership 또는 Missing Actor에는 빈 optional을 반환한다.
`Destroy`는 exact ActorRef만 변경한다.
같은 incarnation이 없으면 `false`, 다른 generation은 `actor_generation_stale`, 이동 중이면 `actor_moving`이다.
Public Actor directory와 local Actor bind overload는 제공하지 않는다.

Actor creation은 selected owner MeshNode의 Entry Spot membership을 Ready barrier 안에서 함께 확정한다. Actor
업무 payload는 membership 종류와 관계없이 Actor queue로 직접 전달하며 Entry Spot callback을 경유하지 않는다.
각 Actor는 payload를 처리하는 FIFO claim을 하나만 가진다. `SpotWide` User Spot의 member Actor는 이
claim과 공유 Spot gate를 모두 얻은 뒤 handler를 실행하고, `PerActor` User Spot과 Entry Spot의
Actor는 Actor별 lane에서 실행한다. 따라서 `SpotWide` Actor callback이 `yield()`를 호출해도 Spot gate만
반납하며 같은 Actor의 다음 payload는 continuation이 끝날 때까지 시작하지 않는다. `PerActor`와 Entry Spot의
Actor callback에서 `yield()`를 호출하면 operation을 제출하기 전에 `invalid_configuration`으로 실패한다.

Actor callback이 자신에게 보낸 request를 기다리거나 현재 보유한 Spot gate에서만 완료할 수 있는 request를
`async()`로 기다리면 admission 전에 `invalid_configuration`으로 실패한다. 자신에게 보내는 one-way operation은
Actor FIFO queue에 제출할 수 있지만 현재 handler에서 inline 또는 reentrant 방식으로 실행하지 않는다.
Original creation payload와 일반 message는 다른 owner나 새 incarnation으로 hidden retry하지 않는다. Caller가
timeout, cancellation 또는 moving 결과를 받으면 새 operation을 명시적으로 시작해야 한다.

## 4. STREAM exact-ref binding

```cpp
namespace zlink::framework {

class session_actor_t {
public:
    const actor_ref_t &ref() const noexcept;
    task_t<submit_result_t> relay(const message_t &payload);
    task_t<submit_result_t> relay(
      const stream_dispatch_context_t &dispatch,
      const message_t &payload);
    task_t<void> notify_disconnected();
};

class session_actor_manager_t {
public:
    std::vector<session_actor_t> bound() const;
    std::optional<session_actor_t> find(actor_id_t actor_id) const;
    request_call_t<session_actor_t> bind(actor_ref_t actor_ref);
    request_call_t<session_actor_t> bind_or_get(actor_ref_t actor_ref);
};

} // namespace zlink::framework
```

Bind는 caller가 제출한 exact ActorRef 위치로 한 번만 control request를 보낸다. Stale·moving 결과에서 global
ActorId를 다시 lookup하거나 fresh incarnation으로 자동 bind하지 않는다. `find(...)`는 해당 STREAM session에
이미 bind된 Actor만 조회하며 global Actor directory가 아니다.

현재 STREAM binding을 통한 one-way push는 connection-bound operation이다. 유효한 binding이 없거나 connection
generation이 바뀌면 session-not-bound 또는 stale 결과로 끝나며, Framework가 다른 session을 찾아 다시
제출하지 않는다. Connection 종료는 Actor의 Spot membership을 바꾸거나 Actor를 자동 종료하지 않는다.

## 5. Public trace category

이 문서의 declaration은 public trace의 `actor-relocation` category에 속한다. 공통 의미는
[Actor model](../../../22-actor-model.ko.md), [Spot·Actor membership](../../../23-spot-actor.ko.md)과
[Session Actor dispatch](../../../31-session-actor-dispatch.ko.md)가 소유한다.
