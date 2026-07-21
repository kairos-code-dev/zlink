# C++ Spot exact interface

[C++ exact interface 목차](README.ko.md)

## 1. Instance Spot transfer 등록

Policy를 생략한 factory 등록은 `transfer_policy_t<TSpot>::disabled()`와 같은 의미다.

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

struct instance_spot_address_t {
    std::string mesh_name;
    std::string instance_spot_type;
    spot_rid_t spot_rid;
};

class spot_context_t;
class entry_spot_context_t;
class instance_spot_context_t;
class spot_handler_registry_t;
class instance_spot_handler_registry_t;
struct spot_actor_join_response_t;
struct spot_create_response_t;

template <typename TActor>
class spot_t {
public:
    using actor_type = TActor;

    virtual ~spot_t() = default;
    virtual void configure(spot_context_t &context) = 0;
    virtual task_t<spot_create_response_t> on_create(
      const message_t &request);
    virtual task_t<void> on_initialize();
    virtual task_t<void> on_closing();
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
    virtual void configure(entry_spot_context_t &context) = 0;
    virtual task_t<void> on_initialize();
    virtual task_t<void> on_closing();
    virtual task_t<void> on_create_actor(
      TActor &actor,
      const message_t &create_request);
    virtual task_t<spot_actor_join_response_t> on_actor_join(
      std::string_view actor_id,
      const message_t &request) = 0;
    virtual task_t<void> on_actor_joined(TActor &actor) = 0;
    virtual task_t<void> on_leave_actor(TActor &actor) = 0;
    virtual task_t<void> on_disconnect_actor(TActor &actor);
};

class instance_spot_t {
public:
    virtual ~instance_spot_t() = default;
    virtual void configure(instance_spot_context_t &context) = 0;
    virtual task_t<void> on_initialize();
    virtual task_t<void> on_closing();
};

class spot_common_context_t {
public:
    std::string_view mesh_name() const;
    node_rid_t node_rid() const;
    spot_rid_t spot_rid() const;
    std::string spot_name() const;
    channel_client_t outbound() const;

    template <typename TCommand>
    send_call_t send_to_spot(const spot_handle_t &target, TCommand command);

    template <typename TReply, typename TRequest>
    request_call_t<TReply> request_to_spot(
      const spot_handle_t &target,
      TRequest request);

    template <typename TMessage>
    send_call_t send_to_spot(
      const instance_spot_address_t &target,
      TMessage message);

    template <typename TReply, typename TRequest>
    request_call_t<TReply> request_to_spot(
      const instance_spot_address_t &target,
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
    spot_rid_t spot_rid;
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

class spot_actor_reply_options_t {
public:
    spot_actor_reply_options_t &metadata(std::string key, std::string value);
    spot_actor_reply_options_t &compress(bool enabled = true);

    spot_actor_message_metadata_t metadata_values;
    bool compress_payload = false;
};

struct spot_actor_send_context_t {
    std::string packet_name;
    std::string content_type;
    spot_actor_message_metadata_t metadata;
};

struct spot_actor_request_context_t {
    std::string packet_name;
    std::string content_type;
    spot_actor_message_metadata_t metadata;
    spot_actor_reply_options_t reply;
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
    std::string_view mesh_name() const noexcept;
    std::optional<spot_rid_t> spot_rid() const;
    bound_session_t bound_session() const;

    actor_join_call_t join_spot(spot_rid_t spot_rid,
      const zlink::framework::message_t &request);

    actor_join_call_t join_entry_spot(node_rid_t mesh_node_rid,
      const zlink::framework::message_t &request);

    template <typename TRequest>
    actor_join_call_t join_spot(spot_rid_t spot_rid,
      const TRequest &request);

    template <typename TRequest>
    actor_join_call_t join_entry_spot(node_rid_t mesh_node_rid,
      const TRequest &request);
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

Spot Actor Join / Transfer 관련 interface도 이 문서에 기록된 정식 계약이며,
그 동작 의미는 [공통 스펙](../../../23-spot-actor.ko.md)을 따른다. 구현이나 contract test가
이 시그니처와 다르면 계약 불일치로 처리한다.

`entry_spot_context_t::destroy_actor(...)`는 Entry Spot에서만 호출한다. user Spot에 있는 Actor는
먼저 `leave_actor(...)` 또는 Entry Spot join을 완료해야 한다. Destroy는 membership 이동이 아니므로
`on_leave_actor`를 다시 호출하지 않으며, 같은 Actor instance의 중복 destroy는 lifecycle callback을
추가로 실행하지 않고 성공으로 끝난다. 전체 순서는 [Actor model §6](../../../22-actor-model.ko.md#6-lifecycle)을
따른다.

Actor와 Instance Spot의 maintenance 동작은 factory 등록에 연결한 `transfer_policy_t<T>`가 정한다.
별도 Actor transfer adapter registry나 operation별 state adapter는 제공하지 않는다. `snapshot<TState>()`를
선택한 factory만 typed `transfer_state_adapter_t<TInstance, TState>`로 application state를 capture하고
restore한다.

C++의 일반 Spot packet과 Actor payload handler는 `spot_context_t::handlers()`가 등록한다. Actor handler는
mutable Actor와 읽기 전용 handler context만 받으며 mutable Spot을 함께 받지 않는다. Spot 상태 변경은
`spot_handle_t` direct call로 제출한다. Actor lifecycle은 registry 등록 표면이 아니다. user Spot과 Entry
Spot은 actor ID와 join request를 받는 `on_actor_join(...)`에서 accept 또는 reject를 반환한다. Commit 이후
callback은 해당 factory가 만든 concrete Actor reference를 직접 받는다. 따라서 별도 membership DTO를
lifecycle callback에 끼워 넣지 않는다. Joined, leave와 disconnect callback은 `task_t<void>`를 반환하며
task가 완료되어야
lifecycle callback이 완료된 것으로 본다. callback 안에서 channel 왕복을 기다릴 때 `yield()`를 사용하면
현재 Spot 실행 turn을 반납하고, 응답 뒤 같은 Spot 실행 줄에서 callback을 재개한다.
일반 Spot 타입은 concrete Actor type을 지정한 `zlink::framework::spot_t<TActor>`를 상속해야 하고,
Entry Spot 타입은 `zlink::framework::entry_spot_t<TActor>`를 상속해야 한다. 두 base class가 lifecycle
callback의 virtual contract를 고정하며, `add_spot<TSpot>()`와 `add_entry_spot<TEntrySpot>()`가 이 계약을
compile-time으로 확인한다. 이름이나 파일 위치와 method 존재 여부만으로 역할을 추론하지 않는다.

Instance Spot은 `instance_spot_t`를 상속하며 Actor callback을 갖지 않는다. Framework가
`instance_spot_context_t` 인자를 전달하는 `configure(...)`, message를 받지 않는 `on_initialize()`,
`on_closing()`을 actor-free lifecycle로 사용한다. `configure(...)`에서는 direct
packet과 timer handler만 등록할 수 있다. Instance context의 전용 registry에는 Actor handler와 Logical
Multicast subscription 등록 member가 존재하지 않는다. 같은 MeshNode에서 stable
`instance_spot_type`이나 같은 Spot class를 User Spot factory와 Instance factory에 중복 등록해도 socket bind
전에 설정 오류로 실패한다.

Factory는 activation scope에서 `TSpot` instance를 만든 뒤 `configure(instance_spot_context_t&)`와
`on_initialize()`를 순서대로 호출한다. 빈 `message_t`를 `on_create(...)`에 넘기지 않는다. Location
`Ready` commit이 성공한 뒤 Framework activation barrier를 열고
첫 업무 message를 일반 packet handler에 한 번 전달한다. Close에서는 `on_closing()`을 한 번 호출하고
fencing 조건을 만족하는 location row만 해제한다.

Store-backed dynamic User Spot도 authority 내부 `Creating` row를 `new_object` CAS로 만든 뒤 factory,
`configure(...)`, `on_initialize()`를 완료하고 `Ready` CAS를 수행한다. Resolve와 remote messaging은
`Ready`만 사용한다. 실패하면 exact owner fence로 delete하고 결과를 read해 reconcile한다. Delete가 확인될
때까지 같은 typed failure를 반환하고 숨은 retry를 수행하지 않으며, `Missing`이 확인된 뒤 다음 caller만 새
create를 시작한다. 이 barrier를 제어하는 public API는 없다.

User Spot의 `close()`는 active Actor membership이 있으면 `false`를 반환한다. Spot state, admission과 authority는
바꾸지 않고 `on_closing()`을 호출하거나 Actor를 자동 leave·destroy하지 않는다. Caller는 Actor를 명시적으로
leave 또는 destroy한 뒤 다시 close한다. Manager에서 Spot이 missing인 경우도 `false`이므로 caller는 사전
read 없이 두 경우를 구분하지 않는다. Host Shutdown·Retire는 Actor barrier를 끝낸 뒤 Spot cleanup을 수행한다.

`instance_spot_factory_options_t{}`는 member initializer를 통해 type별 `4096`과 `3000ms` timeout을
적용한다. 명시한 두 값은 모두 0보다 커야 하며 `0`을 기본값 sentinel이나 무제한으로
해석하지 않는다. Activation pending
message·byte budget과 watchdog은 MeshNode startup configuration이 소유하므로 이 구조체에
반복하지 않는다.

`instance_spot_address_t`의 MeshName과 Instance Spot type은 비어 있을 수 없고 UTF-8로 각각 255 byte
이하여야 하며 Spot RID는 빈 값일 수 없다. Equality와 hash는 세 값을 모두 사용하지만 location uniqueness는
`(mesh_name, spot_rid)`가 소유한다. Address call은 location I/O와 activation이 필요할 수 있으므로 cache
상태와 관계없이 `submit()` 또는 request의 `async()`·`yield()`만 사용한다. 동기 즉시 제출 terminator는
추가하지 않는다. `spot_handle_t`, `spot_manager_t`와 handle overload는 이미 존재하는 Spot만 대상으로 하며 missing
Spot을 원격에서 만들지 않는다.

Cold address call의 source는 location resolve, eligible target 선택과 `ColdActivating` CAS claim을 outbound보다
먼저 같은 send deadline 안에서 완료한다. Target은 source가 확정한 token과 generation을 다시 검증하고 factory
activation과 `Ready` CAS만 수행하며 target-side claim을 시작하지 않는다. One-way `submit()` 완료는 source
outbound admission까지 기다리지만 target factory 실행, activation queue 수락과 `Ready`는 기다리지 않는다.

Cold activation coordinator와 barrier는 C++ Framework runtime 내부 aggregate다. Core service driver나
application-facing activation wrapper를 두지 않는다. 내부 구조는
[C++ runtime architecture](../../../../../cpp/internals/runtime-architecture.ko.md)를 따른다.

Cold Instance factory·initialize가 실패하면 durable public `Failed` state를 게시하지 않는다. Runtime은 local
failed barrier를 유지하고 exact authority fence로 delete한 뒤 read해 reconcile한다. Delete 확인 전 같은 address
호출은 같은 typed failure를 반환하며 hidden retry 횟수는 0이다. `Missing` 확인 뒤 다음 caller만 새
`ColdActivating` claim을 시작한다. 이 recovery 상태를 조작하는 public API는 없다.

```cpp
class player_actor_t;

class bingo_room_spot_t : public zlink::framework::spot_t<player_actor_t>,
                          public bingo_room_t {
public:
    void configure(zlink::framework::spot_context_t &context) override;

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
      const zlink::framework::spot_actor_request_context_t &context,
      const start_bingo_game_req_t &request);
};

class bingo_entry_spot_t
  : public zlink::framework::entry_spot_t<player_actor_t> {
public:
    void configure(zlink::framework::entry_spot_context_t &context) override;
};
```

`route_mesh_runtime_options_t`는 public DI singleton이다. 등록되지 않은 ChannelName을 조회하면 구성
오류로 실패한다. 실행 중에는 ChannelName weight만 변경할 수 있다. 최대 메시지 크기는 startup 뒤
변경할 수 없다. Weight는 0부터 100까지이며, 0은 해당 membership을 새
select-one과 Logical Multicast remote target에서 제외한다.

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
actor packet member는 `spot_actor_request_context_t` 또는 `spot_actor_send_context_t`, mutable Actor와 DTO를
받는다. actor disconnected callback도 같은 concrete Actor reference를 받는다.
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
actor context의 `join_spot(...)` request와 reply는 DTO 또는 `zlink::framework::message_t`다.
JSON DTO는 기본 serializer를 사용하므로 message type별 codec 설정이 필요 없다. Protobuf,
MessagePack, custom binary payload처럼 기본 JSON으로 표현할 수 없는 타입만 startup/options 에
serializer extension을 연결하고 업무 코드는 같은 join 호출을 유지한다. join 결과는
승인과 거절 `variant`다. 승인 값만 join 이후 actor ref를 가지며 두 값 모두 reply
`zlink::framework::message_t`를 담는다. typed reply가 필요하면
`async<TReply>()`가 같은 serializer registry로 decode한다. Entry Spot join도 같은 결과 타입을 반환한다.
raw payload 처리는 framework 내부 invoker가 맡으며 application public actor context에
별도 raw join overload를 두지 않는다.

호출 실행 표면은 공통 비동기 call 계약을 C++ coroutine 관례로 표현한다. `request(...)`, `send(...)`,
`join_spot(...)`과 `join_entry_spot(...)`은 call object를 반환한다. One-way call의 `submit()`은 send timeout까지
bounded admission 결과를 담은 `task_t`를 반환한다. Session Actor `relay(...)`는 별도 call object를 만들지 않고
같은 admission 결과를 `task_t<submit_result_t>`로 직접 반환한다. Request와 join은 `async()`가 reply 완료를
기다리는 지점이다.
일반 channel `request_call_t`는 metadata와 request timeout을, `send_call_t`는 metadata만 submit 전에 모으고,
submit 시점에 framework envelope 정책으로 넘긴다. typed packet name은 registration
descriptor가 결정한다. Request와 join의 `async()`는 terminal reply 또는 결과까지 현재 owner turn을
유지하고 `yield()`는 현재 turn을 반납한다. Worker call은 `async()`와 `yield()`로
결과를 기다린다. CPU worker는 동기 작업, I/O worker는 `task_t<TResult>`를 반환하는
작업을 받는다. 한 call object에서 terminator를 두 번 시작하면 protocol error로 완료한다.

```cpp
auto reply = co_await client
  .request("profile", query) // ChannelName만으로 호출 대상을 선택한다.
  .async<profile_reply_t>();

use_profile(reply);
```

public framework async 표면에 `std::future`를 사용하지 않는다. blocking wait는 handler,
timer, STREAM session callback, actor relay 경로에서 허용하지 않는다.

오류 종류는 `.NET` framework의 `ZLinkFrameworkErrorKind`를 C++ naming으로 투영한다.
`async()`는 실패 시 같은 정보를 가진 `framework_exception_t`를 throw한다.

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

## 4. SPOT 표면

```cpp
struct spot_info_t {
    spot_rid_t spot_rid;
    std::string spot_name;
};
class spot_manager_t
{
public:
    spot_create_result_t create_spot(std::string mesh_name, std::string spot_name);
    spot_create_result_t create_spot(
      std::string mesh_name,
      std::string spot_name,
      const message_t &request);

    template <typename TRequest>
    spot_create_result_t create_spot(
      std::string mesh_name,
      std::string spot_name,
      const TRequest &request);

    spot_create_result_t get_or_create_spot(
      std::string mesh_name,
      std::string spot_name,
      spot_rid_t spot_rid);
    spot_create_result_t get_or_create_spot(
      std::string mesh_name,
      std::string spot_name,
      spot_rid_t spot_rid,
      const message_t &request);

    template <typename TRequest>
    spot_create_result_t get_or_create_spot(
      std::string mesh_name,
      std::string spot_name,
      spot_rid_t spot_rid,
      const TRequest &request);

    task_t<std::optional<spot_info_t>> find_spot(
      std::string mesh_name,
      spot_rid_t spot_rid) const;
    task_t<std::vector<spot_info_t>> list_spots(std::string mesh_name) const;
    task_t<bool> close_spot(std::string mesh_name, spot_rid_t spot_rid);
};
```

**lifecycle callback의 호출 순서는 [MeshNode §7](../../../21-mesh-node.ko.md)가 소유한다** —
handler 구성 → 생성 callback → **수락된 경우에만** 초기화 → 종료는 한 번.
