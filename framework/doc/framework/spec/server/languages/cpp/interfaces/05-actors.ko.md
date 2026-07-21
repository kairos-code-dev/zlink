# C++ Actor exact interface

[C++ exact interface 목차](README.ko.md)

## 1. Typed maintenance

Actor의 stateful host maintenance는 factory 등록에 typed transfer policy를 연결한다. Application은
maintenance transaction 내부 단계나 checkpoint payload 형식을 직접 다루지 않는다.

```cpp
template <typename TInstance, typename TState>
class transfer_state_adapter_t {
public:
    virtual ~transfer_state_adapter_t() = default;
    virtual task_t<TState> capture(
      TInstance &instance,
      std::stop_token operation_cancellation) = 0;
    virtual task_t<void> restore(
      TInstance &instance,
      TState state,
      std::stop_token operation_cancellation) = 0;
};

template <typename TInstance>
class transfer_policy_t {
public:
    static transfer_policy_t disabled();
    static transfer_policy_t recreate();

    template <typename TState, typename TAdapter>
      requires std::derived_from<
        TAdapter, transfer_state_adapter_t<TInstance, TState>>
    static transfer_policy_t snapshot(std::string state_contract_id);
};

```

Factory 등록 member의 exact declaration은
[Channel messaging](03-channel-messaging.ko.md)의 `mesh_node_builder_t`가 소유한다.

Application adapter는 typed 업무 상태만 capture·restore한다. Authority payload, checkpoint reference,
retention, transfer phase와 mailbox sequence는 받지 않는다.

같은 `state_contract_id`를 사용하는 source와 target adapter는 `frameworkJsonV1` semantic profile로 호환되어야
한다. 이 profile은 enum을 string, 64-bit integer를 decimal string, binary를 padded base64로 표현하고 unknown
field는 무시한다. Duplicate field와 required field 누락은 거부한다. Application state의 JSON byte 배열 자체는
canonical하지 않으며 Checkpoint Store에는 opaque bytes로 보관한다. Canonical byte identity는 Framework 내부
root manifest, chunk와 envelope에만 적용한다. Message별 codec 등록이나 transfer 전용 codec API는 제공하지
않는다.

Target이 `Activated`에 도달해도 application과 session ingress는 sealed 상태를 유지하고 restore, accepted
journal replay와 bound-session route는 staged 상태로만 준비한다. Source cleanup이 terminal 상태에 도달하고
authority의 `Completed` CAS가 성공한 뒤에만 target을 `Ready`로 열고 checkpoint fence를 해제한다. `Completed`
뒤의 target failure는 ordinary owner loss로 처리하며 이전 checkpoint를 transparent replay하지 않는다. 이
barrier를 조작하는 public phase API는 제공하지 않는다.

Target replacement가 발생하면 stable transfer 안의 각 attempt가 factory와 `restore(...)`를 at-least-once
호출할 수 있고 중단된 stale attempt callback이 successor와 겹칠 수 있다. `capture(...)`도 immutable checkpoint
root가 authority에 연결되기 전까지 반복될 수 있다. Current exact owner와 attempt fence만 completion을 commit하고
admission을 열 수 있다. Callback에는 transfer ID를 추가하지 않으므로 application restore와 capture는 retry-safe해야
하며 exactly-once external side effect를 보장하지 않는다.

Transferred terminal reply accounting은 internal command ID 46 `replyRelayAck`를 사용한다. 이 command는 stable
transfer ID, operation ID, exact request-source fence(owner ID, lease generation, node RID, node generation)와
status만 가지며 payload와 metadata를 싣지 않는다. Physical connection close는 terminal 증거가 아니다. ACK 또는
accepted record에 저장한 exact request-source lease expiry만 terminal accounting을 완료하며 public ACK API는 없다.

Source는 connection-bound one-way를 포함해 admission한 모든 connection-bound work가 terminal accounting에
도달한 뒤에만 `Captured`를 commit한다. Durable accepted journal은 exact owner lease가 있는 source에서만
사용한다. Pre-`Captured` drain이 deadline 안에 끝나지 않으면 transfer를 abort하고 host Retire를
`blocked/transfer_disabled`로 끝낸다. Connection-bound one-way를 미완료 상태로 capture하는 예외는 없다.

Transferable Actor는 source Entry Spot member여야 한다. User Spot member가 하나라도 남아 있으면 Retire
preflight는 `blocked/transfer_disabled`이고 source authority와 admission을 바꾸지 않는다. `new_owner` CAS는
owner, authority owner generation과 current Spot을 target Entry identity로 원자적으로 바꾼다. Target factory와
restore, target `on_actor_joined`, journal replay 뒤에 source `on_leave_actor`와 old Entry membership 제거를
durable cleanup으로 수행한다. Lifecycle callback은 retry-safe해야 하며 at-least-once 호출될 수 있다. 이
순서를 제어하는 public phase API는 없다.

새 distributed Actor는 authority 내부 `Creating` row를 `new_object` CAS로 만들고 최종 `actor_ref_t`
generation, factory 실행, initial Entry membership과 initialize를 완료한 뒤 `Ready` CAS를 수행한다. Resolver와
remote messaging은 `Ready`만 사용한다. Factory나 initialize가 실패하면 exact owner fence로 delete하고 결과를
read해 reconcile한다. Delete가 확인될 때까지 같은 typed failure를 반환하고 숨은 retry를 수행하지 않으며,
`Missing`이 확인된 뒤 다음 caller만 새 `Creating`을 시작한다. Entry Spot initialization도 Host `Serving`
publication보다 먼저 완료한다. 이 barrier를 위한 public API는 없다.

## 2. Actor 표면

```cpp
struct actor_ref_snapshot_t
{
    node_rid_t    node_rid;
    std::string   actor_id;
    std::uint64_t generation = 0;

    static actor_ref_snapshot_t from (const actor_ref_t &);
    actor_ref_t to_actor_ref() const;
};

struct actor_join_reply_t;                // join 결과
class actor_send_call_t
{
public:
    using metadata_map_t = std::map<std::string, std::string>;

    actor_send_call_t &metadata(std::string key, std::string value);
    task_t<submit_result_t> submit();
};

class actor_request_call_t
{
public:
    actor_request_call_t &timeout(std::chrono::milliseconds timeout);

    template <typename TReply>
    task_t<TReply> async();

    template <typename TReply>
    task_t<TReply> yield();

    task_t<message_t> async_message();
    task_t<message_t> yield_message();
};

class actor_client_t
{
public:
    virtual ~actor_client_t() = default;

    template <typename TMessage>
    actor_send_call_t send_to_actor(
      std::string mesh_name,
      actor_ref_t actor_ref,
      TMessage message);

    template <typename TRequest>
    actor_request_call_t request_to_actor(
      std::string mesh_name,
      actor_ref_t actor_ref,
      TRequest request);
};

class actor_directory_t
{
public:
    virtual ~actor_directory_t() = default;
    virtual task_t<std::optional<actor_ref_t>> find(
      std::string mesh_name,
      std::string actor_id) = 0;
};

class actor_manager_t
{
public:
    virtual ~actor_manager_t() = default;
    virtual task_t<actor_ref_t> create(
      std::string mesh_name,
      std::string actor_id,
      std::string actor_type) = 0;
    virtual task_t<actor_ref_t> create(
      std::string mesh_name,
      std::string actor_id,
      std::string actor_type,
      message_t create_request) = 0;

    template <typename TCreation>
    task_t<actor_ref_t> create(
      std::string mesh_name,
      std::string actor_id,
      std::string actor_type,
      TCreation create_request);

    virtual task_t<actor_ref_t> get_or_create(
      std::string mesh_name,
      std::string actor_id,
      std::string actor_type) = 0;
    virtual task_t<actor_ref_t> get_or_create(
      std::string mesh_name,
      std::string actor_id,
      std::string actor_type,
      message_t create_request) = 0;

    template <typename TCreation>
    task_t<actor_ref_t> get_or_create(
      std::string mesh_name,
      std::string actor_id,
      std::string actor_type,
      TCreation create_request);
};
class session_actor_t {
public:
    ~session_actor_t();
    session_actor_t(session_actor_t &&) noexcept;
    session_actor_t &operator=(session_actor_t &&) noexcept;
    session_actor_t(const session_actor_t &) = default;
    session_actor_t &operator=(const session_actor_t &) = default;

    const actor_ref_t &ref() const noexcept;
    std::string_view actor_id() const noexcept;
    task_t<submit_result_t> relay(const zlink::message_t &payload);
    task_t<submit_result_t> relay(
      const stream_dispatch_context_t &dispatch,
      const zlink::message_t &payload);
    task_t<void> notify_disconnected();
};

class session_actor_manager_t {
public:
    ~session_actor_manager_t();
    session_actor_manager_t(session_actor_manager_t &&) noexcept;
    session_actor_manager_t &operator=(session_actor_manager_t &&) noexcept;
    session_actor_manager_t(const session_actor_manager_t &) = default;
    session_actor_manager_t &operator=(const session_actor_manager_t &) = default;

    std::vector<session_actor_t> bound() const;
    std::optional<session_actor_t> find(std::string actor_id) const;
    request_call_t<session_actor_t> bind(actor_ref_t actor_ref);
    request_call_t<session_actor_t> bind_or_get(actor_ref_t actor_ref);
};
```

**`generation`이 stale actor ref를 걸러낸다.** identity와 authority의 의미는
[spot-actor §1](../../../23-spot-actor.ko.md#1-identity와-authority)이 소유한다. Forwarding window가
끝난 old ref의 실패 의미는
[spot-actor §8](../../../23-spot-actor.ko.md#8-stale-route와-forwarding)이 소유한다.

Canonical logical identity는 `(MeshName, ActorId)`다. Actor type은 create에서 factory를 선택한 뒤 authority
payload에 고정하는 immutable lifecycle attribute이며 `actor_ref_t`나 directory key에 반복하지 않는다. 같은
MeshName과 Actor ID에는 active type 하나만 존재한다. Get-or-create에 전달한 type이 existing authority의 type과
다르면 type conflict로 실패한다.

`actor_directory_t`는 MeshName과 Actor ID로 이미 존재하는 logical Actor만 조회한다. Missing Actor를
생성하거나 remote MeshNode를 선택하지 않는다. Local create와 get-or-create는 `actor_manager_t`가 소유하며
반드시 actor type을 받는다. `mesh_name`은 현재 host에 등록된 local MeshNode를 선택한다.
Existing Actor가 remote owner에 있으면 `find(...)`와 get-or-create가 그 `actor_ref_t`를 반환할 수 있지만,
missing Actor를 remote owner에 생성하거나 hidden forwarding으로 만들지 않는다.

`session_actor_manager_t`는 현재 session에 bind된 Actor 목록·조회와 bind만 제공한다. Actor create·
get-or-create는 `actor_manager_t`가 소유하고 Actor에서 session으로 send·close하는 표면은
Actor context의 `bound_session_t`가 소유한다. `relay(dispatch, payload)`는 explicit current STREAM
dispatch context를 받고 즉시 request reply capability를 runtime에 이전한다. Submitted면 Actor의 typed
reply가 original STREAM correlation을 terminal-once로 완료하고 admission failure면 Framework가 같은
correlation을 typed failure로 완료한다. Caller는 별도 reply·retry를 하지 않는다. One-way
dispatch context는 reply capability가 없으므로 admission만 반환한다.
