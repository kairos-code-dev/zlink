# C++ Spot exact interface

[C++ exact interface 목차](README.ko.md)

Session에 bind된 Actor의 physical disconnect는 Framework가 automatic all-settled로 통지한다. Actor
disconnect callback은 destroy·leave·membership 변경이 아니다. Actor relocation은 같은 ObjectGeneration에
대해 owner·membership commit, 필요한 lifecycle callback과 accepted journal replay·logical timer 복원, durable source cleanup,
`Completed` CAS를 차례로 끝낸 뒤 command 44·45로 해당 binding route만 바꾼다. Relocation 자체는
disconnect callback을 실행하지 않는다. 같은 Session의 다른 Actor route와 physical STREAM connection은
유지하며 routed ACK와 steady normalization 전에는 target session packet·push admission을 열지 않는다.

## 1. Spot identity와 relocation 등록

User·Instance Spot factory는 `relocation_policy_t<TSpot>::disabled()`, `recreate()` 또는 `snapshot(...)` 중
하나를 명시한다. Policy를 생략하는 factory overload는 제공하지 않는다.

```cpp
namespace zlink::framework {

template <typename TSpot>
class spot_relocation_adapter_t {
public:
    virtual ~spot_relocation_adapter_t() = default;
    virtual task_t<std::vector<std::byte>> capture(
      TSpot &spot,
      std::stop_token operation_cancellation) = 0;
    virtual task_t<void> restore(
      TSpot &spot,
      std::vector<std::byte> payload,
      std::stop_token operation_cancellation) = 0;
};

} // namespace zlink::framework
```

Snapshot Spot factory의 `relocation_policy_t<TSpot>::snapshot<TAdapter>()`에서 `TAdapter`는
`spot_relocation_adapter_t<TSpot>`를 구현해야 한다. Actor adapter를 전달하거나 Spot factory에 맞지 않는 adapter를
전달하면 socket bind 전에 configuration error로 실패한다. Adapter는 application state를 opaque byte vector로만
주고받으며 typed state, 별도 contract identifier와 message wrapper를 노출하지 않는다.

Factory 등록 member의 exact declaration은
[Channel messaging](03-channel-messaging.ko.md)의 `mesh_node_builder_t`가 소유한다.

## 2. Spot Framework API

Framework Spot 표면은 owner MeshNode와 `zlink::framework::spot_t`를 기반으로 한다.

```cpp
namespace zlink::framework {

enum class spot_kind_t {
    invalid = 0,
    entry = 1,
    user = 2,
    instance = 3
};

enum class spot_close_reason_t {
    explicit_close = 0,
    host_shutdown = 1,
    relocation_out = 2
};

struct spot_closing_context_t final {
    spot_close_reason_t reason;
    std::chrono::system_clock::time_point deadline;
};

using spot_id_t = std::string;

class spot_ref_t final {
public:
    spot_ref_t(spot_id_t spot_id,
      std::uint64_t object_generation,
      std::string mesh_name,
      node_rid_t node_rid);

    const spot_id_t &spot_id() const noexcept;
    std::uint64_t object_generation() const noexcept;
    std::string_view mesh_name() const noexcept;
    const node_rid_t &node_rid() const noexcept;
};

class spot_context_t;
class entry_spot_context_t;
class instance_spot_context_t;
class spot_handler_registry_t;
class instance_spot_handler_registry_t;
struct spot_actor_join_response_t;
struct spot_create_response_t;
struct actor_create_response_t;

template <typename TActor>
class spot_t {
public:
    using actor_type = TActor;

    virtual ~spot_t() = default;
    virtual spot_context_t &context() noexcept = 0;
    virtual const spot_context_t &context() const noexcept = 0;
    virtual void configure() = 0;
    virtual task_t<spot_create_response_t> on_create(
      const message_t &request);
    virtual task_t<void> on_initialize();
    virtual task_t<void> on_closing(
      const spot_closing_context_t &context,
      std::stop_token cleanup_cancellation);
    virtual task_t<spot_actor_join_response_t> on_actor_join(
      std::string_view actor_id,
      const message_t &request) = 0;
    virtual task_t<void> on_actor_joined(TActor &actor) = 0;
    virtual task_t<void> on_leave_actor(TActor &actor) = 0;
    virtual task_t<void> on_disconnect_actor(TActor &actor);
};

template <typename TActor>
class entry_spot_t {
public:
    using actor_type = TActor;

    virtual ~entry_spot_t() = default;
    virtual entry_spot_context_t &context() noexcept = 0;
    virtual const entry_spot_context_t &context() const noexcept = 0;
    virtual void configure() = 0;
    virtual task_t<void> on_initialize();
    virtual task_t<void> on_closing(
      const spot_closing_context_t &context,
      std::stop_token cleanup_cancellation);
    virtual task_t<actor_create_response_t> on_create_actor(
      TActor &actor,
      const message_t &create_request);
    virtual task_t<void> on_actor_relocated(TActor &actor);
    virtual task_t<void> on_actor_joined(TActor &actor) = 0;
    virtual task_t<void> on_leave_actor(TActor &actor) = 0;
    virtual task_t<void> on_disconnect_actor(TActor &actor);
};

class instance_spot_t {
public:
    virtual ~instance_spot_t() = default;
    virtual instance_spot_context_t &context() noexcept = 0;
    virtual const instance_spot_context_t &context() const noexcept = 0;
    virtual void configure() = 0;
    virtual task_t<void> on_initialize();
    virtual task_t<void> on_closing(
      const spot_closing_context_t &context,
      std::stop_token cleanup_cancellation);
};

class spot_common_context_t {
public:
    std::string_view mesh_name() const;
    node_rid_t node_rid() const;
    spot_id_t spot_id() const;
    std::uint64_t object_generation() const;
    channel_client_t outbound() const;

    template <typename TCommand>
    spot_send_call_t send_to_spot(spot_id_t target, TCommand command);

    template <typename TRequest>
    spot_request_call_t request_to_spot(
      spot_id_t target,
      TRequest request);

    template <typename TEvent>
    publish_call_t publish(
      std::string channel_name,
      std::string topic,
      TEvent event);

    template <typename THandler>
    timer_t add_timer(std::string name,
      std::chrono::milliseconds period,
      timer_options_t options = {});

    template <typename TWork>
    auto run_cpu_worker(TWork work);

    template <typename TWork>
    auto run_io_worker(TWork work);

};

class spot_context_t : public spot_common_context_t {
public:
    spot_context_t();
    ~spot_context_t();
    spot_context_t(spot_context_t &&) noexcept;
    spot_context_t &operator=(spot_context_t &&) noexcept;
    spot_context_t(const spot_context_t &) = default;
    spot_context_t &operator=(const spot_context_t &) = default;

    spot_handler_registry_t handlers();

    template <typename TActor>
    task_t<void> leave_actor(TActor &actor);

    task_t<bool> close();
};

class entry_spot_context_t : public spot_common_context_t {
public:
    entry_spot_context_t();
    ~entry_spot_context_t();
    entry_spot_context_t(entry_spot_context_t &&) noexcept;
    entry_spot_context_t &operator=(entry_spot_context_t &&) noexcept;
    entry_spot_context_t(const entry_spot_context_t &) = default;
    entry_spot_context_t &operator=(const entry_spot_context_t &) = default;
    spot_handler_registry_t handlers();

    template <typename TActor>
    task_t<void> destroy_actor(TActor &actor);

    task_t<void> destroy_actor(const actor_ref_t &actor);
};

class instance_spot_context_t : public spot_common_context_t {
public:
    instance_spot_context_t();
    ~instance_spot_context_t();
    instance_spot_context_t(instance_spot_context_t &&) noexcept;
    instance_spot_context_t &operator=(instance_spot_context_t &&) noexcept;
    instance_spot_context_t(const instance_spot_context_t &) = default;
    instance_spot_context_t &operator=(const instance_spot_context_t &) = default;

    instance_spot_handler_registry_t handlers();
    task_t<bool> close();
};

struct spot_actor_join_response_t {
    bool accepted = false;
    std::optional<zlink::framework::message_t> reply;

    static spot_actor_join_response_t accept(
      std::optional<message_t> reply = std::nullopt);

    template <typename TReply>
    static spot_actor_join_response_t accept(TReply reply);

    static spot_actor_join_response_t reject(
      std::optional<message_t> reply = std::nullopt);

    template <typename TReply>
    static spot_actor_join_response_t reject(TReply reply);
};

struct actor_create_response_t {
    bool accepted = true;
    std::optional<zlink::framework::message_t> reply;

    static actor_create_response_t accept(
      std::optional<message_t> reply = std::nullopt);

    template <typename TReply>
    static actor_create_response_t accept(TReply reply);

    static actor_create_response_t reject(
      std::optional<message_t> reply = std::nullopt);

    template <typename TReply>
    static actor_create_response_t reject(TReply reply);
};

enum class spot_create_state_t {
    existing = 0,
    created = 1,
    rejected = 2
};

struct spot_create_response_t {
    bool accepted = true;
    std::optional<zlink::framework::message_t> reply;

    static spot_create_response_t accept(
      std::optional<message_t> reply = std::nullopt);

    template <typename TReply>
    static spot_create_response_t accept(TReply reply);

    static spot_create_response_t reject(
      std::optional<message_t> reply = std::nullopt);

    template <typename TReply>
    static spot_create_response_t reject(TReply reply);
};

struct spot_create_result_t {
    spot_ref_t spot;
    spot_create_state_t state = spot_create_state_t::created;
    std::optional<zlink::framework::message_t> reply;
};

struct spot_actor_message_metadata_t {
    std::optional<std::string_view> find(std::string_view key) const;
    bool contains(std::string_view key) const;
    bool empty() const noexcept;
    std::string content_type = "application/json";
    std::map<std::string, std::string> values;
};

struct spot_packet_context_t {
    std::string packet_name;
    std::string content_type;
    spot_actor_message_metadata_t metadata;
    bool cancellation_requested = false;

    bool is_cancellation_requested() const noexcept;
};

class spot_handler_registry_t {
public:
    template <auto Method>
    spot_handler_registry_t &add_handler(std::string packet_name = {});

    template <auto Method>
    spot_handler_registry_t &add_subscribe(
      std::string channel_name,
      std::string topic);

    template <auto Method>
    spot_handler_registry_t &add_actor_send(std::string packet_name = {});

    template <auto Method>
    spot_handler_registry_t &add_actor_request(std::string packet_name = {});
};

class instance_spot_handler_registry_t {
public:
    template <auto Method>
    instance_spot_handler_registry_t &add_handler(
      std::string packet_name = {});
};

class bound_session_t {
public:
    template <typename TMessage>
    bound_session_send_call_t send(const TMessage &message);

    task_t<void> disconnect();
};

class actor_context_t {
public:
    const actor_ref_t &actor_ref() const noexcept;
    const actor_id_t &actor_id() const noexcept;
    std::uint64_t object_generation() const noexcept;
    std::string_view mesh_name() const noexcept;
    std::optional<spot_id_t> spot_id() const;
    bound_session_t bound_session() const;

    actor_join_call_t join_spot(spot_id_t spot_id);

    actor_join_call_t join_spot(spot_id_t spot_id,
      const zlink::framework::message_t &request);

    actor_join_call_t join_entry_spot();

    actor_join_call_t join_entry_spot(
      const zlink::framework::message_t &request);

    template <typename TRequest>
    actor_join_call_t join_spot(spot_id_t spot_id,
      const TRequest &request);

    template <typename TRequest>
    actor_join_call_t join_entry_spot(const TRequest &request);
};

} // namespace zlink::framework
```

`spot_context_t::publish(...)`는 target ChannelName과 topic을 함께 받는다. publish는
MeshNode ROUTER를 통해 remote MeshNode마다 한 번 제출하고, 수신 node는 node-local
subscription만 검사한다. 각 remote ROUTER와 local mailbox는 대상별로 수락하며, 한 대상의
실패가 앞에서 수락된 전송을 취소하지 않는다. Spot·Actor 등록은 owner
`mesh_node_builder_t`에 속한다.

`mesh_node_socket_config_t::max_message_size`는 startup 전에만 설정하며 실행 중 setter를 제공하지 않는다.
`0`은 binding 또는 transport가 수신할 수 있는 최대 complete message 크기로 정규화한다. Transport가
unlimited이면 service wire의 `uint32` 표현 한계에서 envelope overhead를 뺀 값을 사용한다. 양수는 그
표현 한계를 넘을 수 없으며 넘으면 startup 설정 오류로 거부한다. Peer는 정규화한 값을 내부 handshake로
교환하고 sender와 receiver는 두 값 중 작은 effective bound를 complete message allocation 전에 적용한다.
이 negotiation을 위한 public option은 제공하지 않는다.
`send_timeout`을 지정하지 않으면 framework 기본값 1초를 사용한다. `receive_timeout`을 지정하지 않으면
수신 대기 상한을 따로 두지 않는다. HWM은 0 이상이어야 한다.

Spot Actor Join / Relocation 관련 interface도 이 문서에 기록된 정식 계약이며,
그 동작 의미는 [공통 스펙](../../../23-spot-actor.ko.md)을 따른다. 구현이나 contract test가
이 시그니처와 다르면 계약 불일치로 처리한다.
`join_entry_spot(...)`은 target node RID를 받지 않으며 Framework가 현재 eligible Entry Spot을 선택한다.

`spot_close_reason_t`의 값은 `explicit_close=0`, `host_shutdown=1`, `relocation_out=2`다. Context의
`deadline`은 closing operation의 absolute UTC time이다. Framework는 callback invocation 전에는
`cleanup_cancellation`에 stop을 요청하지 않고 deadline이 끝날 때 요청한다. Entry·User·Instance Spot만
callback을 받고 Actor별 closing callback은 제공하지 않는다. Host Shutdown은 Actor membership과 local instance가
유효한 상태에서 callback을 실행하고 completion 뒤 scope와 authority를 정리한다. Standalone Actor relocation은
Entry Spot을 닫지 않으므로 이 callback을 호출하지 않는다.

`entry_spot_context_t::destroy_actor(...)`는 Entry Spot에서만 호출한다. user Spot에 있는 Actor는
먼저 `leave_actor(...)` 또는 Entry Spot join을 완료해야 한다. Destroy는 membership 이동이 아니므로
`on_leave_actor`를 다시 호출하지 않으며, 같은 Actor instance의 중복 destroy는 lifecycle callback을
추가로 실행하지 않고 성공으로 끝난다. 전체 순서는 [Actor model §6](../../../22-actor-model.ko.md#6-lifecycle)을
따른다.

Actor와 User·Instance Spot의 cross-node materialization 동작은 factory 등록에 연결한
`relocation_policy_t<T>`가 정한다. 별도 relocation adapter registry나 operation별 adapter는 제공하지 않는다.
Snapshot Spot factory만 `spot_relocation_adapter_t<TSpot>`로 Spot application state를 capture·restore한다. Whole
User Spot relocation은 Spot root에 Spot adapter를 사용하고 각 Actor participant에는
`actor_relocation_adapter_t<TActor>`를 사용한다. Same-node operation과 Disabled·Recreate policy는 Spot adapter를
호출하지 않는다. Disabled cross-node operation은 capture 전에 거부한다.

Spot adapter의 `capture(...)` 결과는 최대 64 MiB이며 빈 vector는 유효하다. 반환한 vector의 소유권은
Framework로 이동하고 `restore(...)`에 전달한 vector는 해당 비동기 호출이 소유한다. Capture가 throw하거나
failed task로 끝나면 durable abort와 source normalization 뒤 admission을 복원한다. Restore가 실패한 instance는
폐기하고 새 attempt의 factory가 만든 instance에 같은 immutable payload를 적용한다. Framework가 operation
deadline 때문에 callback을 취소하면 `deadline_exceeded`로 분류한다. Recovery 때문에 두 method가 at-least-once
호출되거나 stale attempt와 successor에서 겹칠 수 있으므로 adapter는 retry-safe해야 한다. Framework는 adapter의
external side effect에 exactly-once를 보장하지 않는다.

User Spot factory에 지정한 `user_spot_execution_mode_t::per_actor`에서는 Spot lane, 모든 member Actor
lane과 timer별 lane이 독립적으로 실행될 수 있다. Close, relocation과 Snapshot capture는 이 lane 전체를
하나의 barrier generation으로 seal하고 active claim이 모두 끝난 뒤에만 시작한다. 일부 lane만 멈춘 상태를
capture하지 않으며 precommit 실패 시 모든 lane과 held ingress를 함께 복원한다.

C++의 일반 Spot packet과 Actor payload handler는 `spot_context_t::handlers()`가 등록한다. Actor handler는
mutable Actor와 읽기 전용 handler context만 받으며 mutable Spot을 함께 받지 않는다. Spot 상태 변경 message는
global `spot_id_t` direct call로 제출한다. Actor lifecycle은 registry 등록 표면이 아니다. User Spot과 Entry
Spot은 actor ID와 join request를 받는 `on_actor_join(...)`에서 accept 또는 reject를 반환한다. Commit 이후
callback은 해당 factory가 만든 concrete Actor reference를 직접 받는다. 따라서 별도 membership DTO를
lifecycle callback에 끼워 넣지 않는다. Joined, leave와 disconnect callback은 `task_t<void>`를 반환하며
task가 완료되어야
lifecycle callback이 완료된 것으로 본다. callback 안에서 channel 왕복을 기다릴 때 `yield()`를 사용하면
`SpotWide` User Spot은 현재 Spot gate를 반납하고, 응답 뒤 같은 gate를 다시 얻어 callback을 재개한다.
Member Actor callback의 Actor FIFO claim은 반납하지 않는다. Instance Spot도 같은 gate 반납을 지원한다.
`PerActor` User Spot과 Entry Spot callback의 `yield()`는 operation을 제출하기 전에
`invalid_configuration`으로 실패한다.
일반 Spot 타입은 concrete Actor type을 지정한 `zlink::framework::spot_t<TActor>`를 상속해야 하고,
Entry Spot 타입은 `zlink::framework::entry_spot_t<TActor>`를 상속해야 한다. 두 base class가 lifecycle
callback의 virtual contract를 고정하며, `add_spot<TSpot>()`와 `add_entry_spot<TEntrySpot>()`가 이 계약을
compile-time으로 확인한다. 이름이나 파일 위치와 method 존재 여부만으로 역할을 추론하지 않는다.

Instance Spot은 `instance_spot_t`를 상속하며 Actor callback을 갖지 않는다. Framework가
`context()`가 반환하는 exact `instance_spot_context_t`, 인자 없는 `configure()`, message를 받지 않는
`on_initialize()`, `on_closing(context, cleanup_cancellation)`을 actor-free lifecycle로 사용한다. `configure()`에서는 direct
packet과 timer handler만 등록할 수 있다. Instance context의 전용 registry에는 Actor handler와 Logical
Multicast subscription 등록 member가 존재하지 않는다. 같은 MeshNode에서 stable
`instance_spot_type`이나 같은 Spot class를 User Spot factory와 Instance factory에 중복 등록해도 socket bind
전에 설정 오류로 실패한다.

Factory는 Instance Spot marker가 있는 direct call의 cold activation 또는 stored creation intent의
reactivation scope에서 `TSpot` instance를 만든 뒤 exact Context를 연결하고 `configure()`와
`on_initialize()`를 순서대로 호출한다. 빈 `message_t`를 `on_create(...)`에 넘기지 않는다. Framework는 첫
업무 message를 durable activation inbox의 첫 record로 확정하고 handler barrier를 유지한 상태에서 recovery
root·cursor를 포함한 Location `Ready`를 commit한다. Runtime은 첫 record를 local queue head로 복원한 뒤
activation barrier를 연다. Close에서는
`on_closing(context, cleanup_cancellation)`을 한 번 호출하고
fencing 조건을 만족하는 location row만 해제한다.

User Spot은 manager의 explicit Create·GetOrCreate가 `Creating` reservation을 시작한다. Instance Spot은
`spot_send_call_t` 또는 `spot_request_call_t`에서 `instance_spot()`을 선택한 direct call만 missing RID의
cold activation을 시작한다. Marker가 없는 일반 send·request에서 RID가 없으면 `spot_route_not_found`로
끝나며 factory를 실행하거나 creation intent를 기록하지 않는다. Source는 Ready authority가 있으면 current
owner에게 일반 message를 보내고, Missing이면 target을 선택해 SpotId, stable type, creation intent와 first
message를 포함한 activation envelope를 보낸다. Source는 creation reservation을 만들지 않는다. 이 envelope는
CAS 전에 target으로 보낼 수 있는 Framework infrastructure message이며 application handler로 dispatch하지 않는다.

Command 39 route kind `1`은 Ready authority의 exact generation fence를 사용한다. Missing cold activation은
route kind `2`로 target Mesh·node RID·lifecycle, Spot ID, stable type, descriptor version과 deadline을
전달하며 authority fence를 포함하지 않는다. Kind `2` route와
`instance-activation-recovery-v1`의 deadline, operation identity와 metadata
presence·frame은 byte 단위로 같아야 한다. Cold activation send와 request는 모두 nonzero operation identity를
사용한다.

Target runtime은 metadata presence·frame을 포함한 complete envelope를 Relocation Store에 immutable recovery root로 먼저 저장한다. Local exact
instance가 없을 때만 자신을 owner로 generic Store Reserve를 수행하며 Pending snapshot은 provider가 발급한
reservation fence와 recovery root receipt를 반환한다. CAS winner가 factory, initialize와 durable inbox first
record 확정을 수행한다. CAS loser는 factory를 만들지 않고 current authority를 읽어 owner에게 reroute하거나
진행 중인 attempt에 합류한다. Commit은 handler barrier를 유지한 채 recovery root·cursor와 `Ready`,
typed capacity bundle 전체의 Reserved→Active 전환을 함께 게시한다. Runtime은 first record를 local queue head로 복원한 뒤 barrier를
열며 source는 `Ready` 뒤 같은 message를 다시 전송하지 않는다. Authority와 일치하지 않는 local-only instance는
message를 처리하지 못하도록 fence한다. 실패는 exact Abort로 authority와 reserved capacity bundle을 함께 정리한다.
Recovery pointer는 첫 handler terminal completion을 durable하게 기록하고 replay cursor를 inbox sequence까지
갱신한 뒤에만 Preserve CAS로 제거한다. Queue admission만으로 제거하지 않는다.

User Spot과 member Actor의 relocation은 generic aggregate로 처리한다. Active membership이 있다는 이유만으로
Retire를 차단하지 않으며 aggregate owner와 membership을 한 commit에서 전환한다. `spot_context_t::close()`와
`instance_spot_context_t::close()`는 context가 보유한 exact current SpotRef를 사용한다.

일반 User Spot close는 active Actor membership이 하나라도 있으면 `false`로 끝나고 admission과 authority를
유지한다. Caller가 member Actor의 leave 또는 destroy를 완료한 뒤에만 close할 수 있으며, Framework가 close를
위해 Actor를 숨겨서 이동하거나 제거하지 않는다. Host Retire는 close와 다르게 User Spot과 current member
Actor 전체를 bounded aggregate로 이전한다.

Instance Spot factory는 actor-free lifecycle만 구현한다. Source runtime은 Instance Spot marker가 있는 direct
call에서만 missing RID의 activation envelope를 target에 보낸다. Target runtime은 envelope를 근거로 자신을
owner로 creation claim을 시작한다. `instance_spot()`은 stable type을 생략한 marker이고,
`instance_spot(stable_type)`은 type을 명시한 marker다. `in_mesh(mesh_name)`,
Caller-defined placement selector는 public call surface에 제공하지 않는다. 이미 `Ready`인
authority row가 있으면 global SpotId로 현재 owner를 찾으므로 marker와 stable type이 없어도 같은 row로
전달한다.

Stable type을 생략한 marker는 선택된 Mesh의 eligible descriptor가 게시한 Instance Spot capability를 비교한다.
서로 다른 stable type이 정확히 하나이면 그 type을 사용한다. 둘 이상이면 caller가
`instance_spot(stable_type)`으로 type을 명시해야 하며 reservation을 만들기 전에 호출 오류로 끝난다. 등록된
type이 없으면 missing 결과로 끝난다. Explicit stable type이 existing row와 다르면 `spot_type_mismatch`이며,
existing row의 type을 caller가 다시 전달할 필요는 없다.

`in_mesh(...)`를 생략했을 때 eligible Object Mesh가 없으면 `object_client_not_configured`, 둘 이상이면
`mesh_selection_required`다. 하나이면 그 Mesh를 선택한다. `in_mesh(...)`가 지정한 Mesh를 찾지 못하면
`mesh_not_found`다. Mesh를 선택한 뒤 stable type을 생략했는데 distinct Instance Spot type이 0개이면
`spot_route_not_found`, 둘 이상이면 `invalid_configuration`이다. 여러 MeshNode가 같은 stable type을
등록한 경우 distinct type 하나로 계산한다.

Cold Instance로 향하는 one-way call은 resolve, reservation, activation과 outbound admission까지 같은 send
deadline에 포함하고 admission 결과에서 완료한다. Request는 activation, handler와 terminal reply까지 기다린다.
Owner loss 뒤에는 authority에 저장된 creation intent를 사용해 같은 instance를 reactivation한다.

`spot_ref_t`는 UTF-8 encoded 크기 1..255 bytes인 case-sensitive exact `std::string` global SpotId,
`1..9223372036854775807` 범위의 ObjectGeneration과 조회 시점
MeshName·NodeRid를 담은 immutable location snapshot이다. 일반 message target으로 사용하지 않으며 별도 handle,
resolver와 address type은 제공하지 않는다.
SpotId와 stable type은 UTF-8 `1..255` byte exact 값이며 trim, case folding과 Unicode normalization을 적용하지
않는다. `spot_id_t`는 UTF-8 encoded 크기 1..255 bytes의 `std::string`이며 case-sensitive exact
byte sequence로 비교한다. Unicode normalization과 case folding은 적용하지 않는다.

```cpp
class player_actor_t;

class bingo_room_spot_t : public zlink::framework::spot_t<player_actor_t>,
                          public bingo_room_t {
public:
    void configure() override;

    zlink::framework::task_t<zlink::framework::spot_actor_join_response_t>
    on_actor_join(
      std::string_view actor_id,
      const zlink::framework::message_t &request) override;

    zlink::framework::task_t<void> on_actor_joined(
      player_actor_t &actor) override;

    zlink::framework::task_t<void> on_leave_actor(
      player_actor_t &actor) override;

    zlink::framework::task_t<void> on_disconnect_actor(
      player_actor_t &actor) override;
};

class player_actor_t : public zlink::framework::actor_t {
public:
    start_bingo_game_res_t start_game(
      const zlink::framework::message_context_t &message_context,
      const start_bingo_game_req_t &request);
};

class bingo_entry_spot_t
  : public zlink::framework::entry_spot_t<player_actor_t> {
public:
    void configure() override;
};
```

`route_mesh_runtime_options_t`는 public DI singleton이다. 등록되지 않은 ChannelName을 조회하면 구성
오류로 실패한다. 실행 중에는 node placement weight와 ChannelName weight만 변경할 수 있다. 최대 메시지 크기는 startup 뒤
변경할 수 없다. Weight는 signed `int` `0..10000`이고 기본값은 `100`이다. 범위 밖 runtime 변경은
configuration error이며, 0은 해당 membership을 새 select-one과 Logical Multicast remote target에서
제외한다.

Spot과 Entry Spot은 activation scope가 수명을 소유한다. 기본 생성 가능한 타입은 타입만
등록한다. 생성자 의존성이 있거나 application이 생성 방법을 결정해야 하는 타입은 factory
overload로 등록한다. factory는 Spot을 활성화할 때 framework가 호출하며, 반환한 instance의
수명도 같은 activation scope에서 관리한다.

일반 Spot packet member와 subscription member는 payload 하나를 받는다.
actor join admission을 처리하는 member는 `std::string_view actor_id`와
`zlink::framework::message_t` request를 받으며,
`spot_actor_join_response_t`로 accepted 여부와 optional reply `zlink::framework::message_t`를 반환한다.
actor type과 source/target Spot 및 node 정보는 framework 내부 routing과 검증에만 사용한다.
accepted가 `true`일 때만 actor 위치를 user Spot으로 commit하고
`on_actor_joined(TActor&)`를 호출한다. accepted가 `false`이면 actor 위치를 바꾸지 않고
post-joined callback도 호출하지 않는다. Commit 이후 결과는 callback 이름으로 구분한다.
Entry Spot은 `on_actor_join(...)`을 제공하지 않는다. User Spot에서 Entry Spot으로 복귀하면 admission 없이
membership을 commit하고 target `on_actor_joined(...)`와 source `on_leave_actor(...)`를 호출한다.
`on_create_actor(...)`는 최초 Actor 생성 승인·거절과 optional reply를 반환하며 최초 생성에서는
`on_actor_joined(...)`를 호출하지 않는다.
`entry_spot_t<TActor>::on_actor_relocated(TActor&)`는 기본 no-op implementation을 가진 maintenance 전용 async
callback이다. Maintenance가 Actor를 target Entry Spot에 materialize할 때 Snapshot은 Actor adapter
`restore(...)`를 먼저 완료하고 Recreate는 payload restore 없이 factory materialization을 완료한다. 그 다음
Location authority·Entry membership commit, target `on_actor_relocated(...)`와 source `on_leave_actor(...)` 완료,
Actor accepted journal replay·logical timer 복원, old Entry membership을 포함한 durable source cleanup,
`Completed` CAS, bound-session route switch·ACK, steady normalization과 dispatch admission 순서로 실행한다.
Source process가 종료되면 exact source fence의 durable cleanup terminal이 source callback 완료를 대신한다.
Journal은 commit 전에 검증해 staging queue에만
준비하고 application handler를 실행하지 않는다. 두 callback 중 하나가 실패해도 authority를 source로 rollback하지 않고
target을 sealed 상태로 유지한 채 exact relocation fence로 retry한다. 두 callback은 at-least-once 호출될 수
있으므로 retry-safe해야 한다. Replay 뒤 `Cleaning` phase가 처리하는 나머지 source resource cleanup은 old Entry
membership gate와 구분한다.

일반 same-node·remote User·Entry Spot join은 기존 `on_actor_join(...)`·`on_actor_joined(...)`와 source
`on_leave_actor(...)` 계약을 사용하며 `on_actor_relocated(...)`를 호출하지 않는다. Maintenance relocation에서는 target의
일반 join callback을 호출하지 않지만 실제 source Entry membership을 끝내므로 source `on_leave_actor(...)`는
commit 뒤 호출한다. Whole User Spot aggregate relocation에서는 membership이 유지되므로 member Actor에 대한 Entry
Spot 또는 User Spot membership callback을 모두 호출하지 않는다. Disabled operation에서도
`on_actor_relocated(...)`를 호출하지 않는다.
actor packet member는 `message_context_t`, mutable Actor와 DTO를 받는다. actor disconnected callback도
같은 concrete Actor reference를 받는다.
Runtime의 private dispatch가 `message_t`를 DTO로 바꾸고 현재 Spot instance와 Actor를 찾아 typed member
function을 호출한다. Application은 invoker, service provider, serializer registry와 descriptor 조회 표면을
받지 않는다. 샘플도 public registration과 call 경로를 통과해야 framework 동작을 확인했다고 볼 수 있다.
Entry Spot membership 상태에서도 actor packet은 일반 Spot packet으로 등록하지 않는다.
`spot_context_t::handlers()`에서 `add_actor_request<Method>()` 또는 `add_actor_send<Method>()`로 등록하며,
member는 handler context, concrete Actor와 DTO를 받는다.
stream header metadata 전체를 actor handler에 그대로 노출하지 않는다. 사용자는
`options.metadata().allow_session_to_actor("trace-id")`처럼 application metadata forwarding 정책을 선언하고,
framework는 허용된 key만 `spot_actor_message_metadata_t`로 project해서 actor context에 넣는다.
handler는 `find(...)` 또는 `contains(...)`로 값을 조회한다. `values`는 단순 반복을 제공하고,
`find(...)`와 `contains(...)`는 handler code가 `std::map` 구조에 직접 묶이지 않게 한다. 빈 metadata
key와 공백만 있는 key는 의미가 모호하므로 두 방향의 allowlist method 모두 이런 key를 거부한다.
이 정책은 stream frame 구조나 ActorGateway 내부 frame을 public handler 표면에 드러내지 않기
위한 경계다.

timer는 native timer handle을 application에 넘기지 않는다. `timer_t`는 Framework timer registration의
lifetime과 취소를 표현하는 public handle이며 callback은 owner Spot mailbox에 제출된다. Entry Spot timer도
서로 다른 Entry Spot instance를 전역 직렬화하지 않는다.

Timer backend 선택은 [비동기 실행 정책](../../../../04-async-execution-policy.ko.md#5-spot-timer)을 따른다.
`timer_tick_t`는 native timer event를 노출하지 않고 공통 timer dispatch metadata만 제공한다.

ActorGateway session relay의 public 표면은 `session_actor_manager_t`, `session_actor_t`,
`actor_context_t`, `bound_session_t`다. MeshNode transport metadata는 이 표면에 노출하지 않는다.
actor context의 `join_spot(...)` request는 DTO 또는 `zlink::framework::message_t`다.
JSON DTO는 기본 serializer를 사용하므로 message type별 codec 설정이 필요 없다. Protobuf,
MessagePack, custom binary payload처럼 기본 JSON으로 표현할 수 없는 타입만 startup/options 에
serializer extension을 연결하고 업무 코드는 같은 join 호출을 유지한다. Join 승인·거절·실패와
optional reply는 나중에 `actor_t::on_join_completed(...)`로 전달한다.
raw payload 처리는 framework 내부 invoker가 맡으며 application public actor context에
별도 raw join overload를 두지 않는다.

호출 실행 표면은 공통 비동기 call 계약을 C++ coroutine 관례로 표현한다. `request(...)`, `send(...)`,
`join_spot(...)`과 `join_entry_spot(...)`은 call object를 반환한다. One-way call의 `submit()`은 send timeout까지
source-local queue admission을 기다리는 `task_t<void>`를 반환한다. Session Actor `relay(...)`는 별도 call
object를 만들지 않고 같은 admission 경계를 `task_t<void>`로 직접 반환한다. Request의 `submit()`과
Join은 handler 안에서 동기 `defer()`로 등록하며 현재 handler에서 결과를 기다리지 않는다.
일반 channel `request_call_t`는 metadata와 request timeout을, `send_call_t`는 metadata만 submit 전에 모으고,
submit 시점에 framework envelope 정책으로 넘긴다. typed packet name은 registration
descriptor가 결정한다. Request의 `submit()`은 terminal reply까지 현재 claim과 gate를 유지한다.
Actor Join의 `defer()`는 gate나 Actor FIFO claim을 반납하지 않으며 Join에는 `submit()`, `async()`와
`yield()`를 제공하지 않는다. Request와 worker의 `yield()`는 `SpotWide` User
Spot과 Instance Spot callback에서만 Spot gate를 반납하며,
`SpotWide` Actor의 FIFO claim은 유지한다. 같은 gate에서만 완료할 수 있는 request를 `submit()`으로
기다리거나 self request를 기다리면 operation을 제출하기 전에 `invalid_configuration`으로 실패한다.
One-way self send는 FIFO로 제출할 수 있지만 inline 또는 reentrant dispatch는 허용하지 않는다.
`SpotWide` member Actor가 현재 User Spot을 떠나는 Join도 `defer()`만 등록하고 handler의 마지막
continuation이 끝난 뒤 실행한다. Join callback을 inline 또는 reentrant하게 호출하지 않는다.
Request 없는 overload는 empty `message_t`를 고정한다. Timeout 기본값은 5초이고 명시 값은 millisecond
올림 기준 finite `1..INT_MAX` ms다. `defer()`에서 monotonic absolute deadline을 고정한다.
Worker call에도 같은 실행 문맥 검사를 적용한다. CPU worker는 동기 작업, I/O worker는 `task_t<TResult>`를 반환하는
작업을 받는다. 한 call object에서 terminator를 두 번 시작하면 protocol error로 완료한다.

```cpp
auto reply = co_await client
  .request("profile", query) // ChannelName만으로 호출 대상을 선택한다.
  .submit<profile_reply_t>();

use_profile(reply);
```

public framework async 표면에 `std::future`를 사용하지 않는다. blocking wait는 handler,
timer, STREAM session callback, actor relay 경로에서 허용하지 않는다.

오류 종류는 `.NET` framework의 `ZLinkFrameworkErrorKind`를 C++ naming으로 투영한다.
`submit()`과 join의 `async()`는 실패 시 같은 정보를 가진 `framework_exception_t`를 throw한다.

## 3. Timer

```cpp
enum class timer_overrun_policy_t {
    skip_late_ticks = 0,
    catch_up_bounded = 1,
    delay_next_tick = 2
};

struct timer_options_t {
    timer_overrun_policy_t overrun_policy =
      timer_overrun_policy_t::skip_late_ticks;
    std::uint64_t max_catch_up_ticks = 1;
    bool stop_on_unhandled_exception = false;
};

struct timer_tick_t {
    std::string name;
    std::uint64_t delivery_index = 0;
    std::uint64_t scheduled_index = 0;
    std::chrono::milliseconds period{0};
    std::chrono::milliseconds scheduled_elapsed{0};
    std::chrono::milliseconds started_elapsed{0};
    std::chrono::milliseconds delay{0};
    std::uint64_t skipped_ticks = 0;
};

struct timer_failure_event_t {
    std::string timer_name;
    std::type_index handler_type;
    std::uint64_t delivery_index = 0;
    bool stopped = false;
    std::string message;
};

class timer_t {
public:
    timer_t();
    ~timer_t();
    timer_t(timer_t &&) noexcept;
    timer_t &operator=(timer_t &&) noexcept;
    timer_t(const timer_t &) = default;
    timer_t &operator=(const timer_t &) = default;

    bool is_disposed() const noexcept;
    void cancel() noexcept;
};
```

timer 등록 검증은 [stage-wrapper §4.1](../../../25-stage-wrapper-on-spot.ko.md)이 소유한다.

Framework timer는 owner Actor·Spot에 속한 logical registration이다. Cross-node relocation에서는 timer 이름,
handler type, period, `timer_options_t`, scheduling cursor와 seal 시점의 pending tick을 relocation payload에
자동으로 포함한다. Application의 relocation adapter는 timer를 capture·restore하거나 target에서 다시 등록하지
않는다. Native timer handle과 backend state는 payload에 포함하지 않고 target runtime이 logical registration으로
다시 만든다. Source는 queue를 seal한 뒤 새 tick을 dispatch하지 않으며 target은 restore와 authority commit을
마치고 dispatch admission이 열린 뒤에만 복원한 pending tick과 다음 tick을 owner mailbox에 제출한다.

## 4. SPOT 표면

```cpp
class spot_create_call_t {
public:
    spot_create_call_t(spot_create_call_t &&) noexcept;
    spot_create_call_t &operator=(spot_create_call_t &&) noexcept;
    spot_create_call_t(const spot_create_call_t &) = delete;
    spot_create_call_t &operator=(const spot_create_call_t &) = delete;

    spot_create_call_t &in_mesh(std::string mesh_name);
    spot_create_call_t &creation_request(message_t request);

    template <typename TRequest>
    spot_create_call_t &creation_request(TRequest request);

    spot_create_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<spot_create_result_t> submit();
    task_t<spot_create_result_t> yield();
};

class spot_manager_t {
public:
    virtual ~spot_manager_t() = default;
    virtual spot_create_call_t create(std::string stable_type) = 0;
    virtual spot_create_call_t get_or_create(
      spot_id_t spot_id,
      std::string stable_type) = 0;

    virtual task_t<std::optional<spot_ref_t>> find(spot_id_t spot_id) = 0;
    virtual task_t<bool> close(spot_ref_t spot) = 0;
};
```

`spot_manager_t`는 User Spot만 생성한다. `Create`는 Framework가 global SpotId를 생성하고,
`GetOrCreate`는 caller가 제공한 global SpotId를 사용한다. Instance Spot create/get-or-create member와 kind
인자는 제공하지 않는다. Call option과 submit은 각각 한 번만 사용할 수 있다. Existing authority가 Instance
kind이거나 stable type이 다르면 `spot_type_mismatch`, eligible capacity가 없으면
`placement_capacity_exhausted`다.
`Create`의 RID는 UUID v4 random identity다. 첫 active authority 충돌은 기존 record를 변경하지 않고
`routing_id_conflict`로 즉시 끝나며 UUID 생성과 reservation은 각각 1건, factory 실행은 0건이다.
두 번째 UUID나 reservation을 만들지 않는다.

`<diagnostic-prefix>-entry-<uuid-v4>` 형식은 Framework가 발급하는 Entry Spot ID용으로 예약한다.
`<uuid-v4>`는 RFC 4122 UUID v4의 lowercase canonical 36-character `8-4-4-4-12` 표현이다.
`get_or_create(...)`의 caller RID 또는 Instance Spot marker를 사용한 direct call의 target RID가 이 형식이면
Location Store read·reservation, target 선택과 factory 실행 전에 `invalid_configuration`으로 완료한다.
Entry Spot을 선택할 때는 `mesh_node_descriptor_t::entry_spot_id`와 lifecycle generation의 exact mapping을
사용하며 RID 문자열을 parse하지 않는다.

Terminal `submit()`과 `yield()`는 exact `spot_ref_t`, `existing`·`created`·`rejected` state와 creation
callback reply를 같은 `spot_create_result_t`로 반환한다. Call object는 single-use이며 두 terminal 가운데
하나만 한 번 호출할 수 있다. `yield()`는 `spot_wide` User Spot 또는 Instance Spot application callback에서만
현재 Spot gate를 반납한다. 다른 문맥에서는 reservation과 factory 실행 전에 `invalid_configuration`으로
완료한다.

`Find`는 current Ready User SpotRef만 반환하고 생성하지 않는다. Instance authority는 manager의 `Find` 결과에
포함하지 않는다. `Close`는 User Spot의 exact SpotRef만 변경한다. Instance Spot은
`instance_spot_context_t::close()`가 context에 보관한 exact current SpotRef로 local close를 수행한다. 같은 User
Spot incarnation이 없으면 manager `Close`는 `false`, 다른 generation이면 `spot_generation_stale`, 이동 중이면
`spot_moving`이다. Public list, resolver와 handle은 제공하지 않는다.

## 5. Public trace category

이 문서의 declaration은 public trace의 `spot-instance`와 `actor-relocation` category에 속한다. 공통 의미는
[Spot address와 messaging](../../../24-spot-address-messaging.ko.md)과
[Spot·Actor membership](../../../23-spot-actor.ko.md)이 소유한다.

**lifecycle callback의 호출 순서는 [MeshNode §7](../../../21-mesh-node.ko.md)가 소유한다** —
handler 구성 → 생성 callback → **수락된 경우에만** 초기화 → 종료는 한 번.
