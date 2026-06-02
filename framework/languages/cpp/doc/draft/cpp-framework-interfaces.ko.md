<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ Channel Messaging](./cpp-channel-messaging.ko.md) | [다음: Draft -- ZLink Framework C++ Policy](./cpp-framework-policy.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [channel](./cpp-channel-messaging.ko.md) | [SPOT](./cpp-spot.ko.md) | [STREAM](./cpp-stream.ko.md)

# Draft -- ZLink Framework C++ Interface Design

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` framework의 전반적인 public interface 방향을
> 정리한다.
> 이 문서는 `framework/doc/spec` 아래 공통 framework 정책을 상위 기준으로 따르고,
> C++ binding의 public 라이브러리 표면을 기반으로 framework 계층을 설계한다.

## 1. 설계 기준

`C++` framework는 기존 C++ binding을 대체하지 않는다. framework는 C++ binding 위에
올라가며, binding이 제공하는 typed public API를 내부 runtime substrate로 사용한다.

기능과 사용성 개념은 `.NET` framework를 기준으로 맞춘다. 즉 app/host, DI scope,
handler registry, channel messaging, `STREAM`, `SPOT`, ActorGateway session relay,
monitoring, graceful shutdown은 같은 모델을 제공하고, C++ public API는 C++20 coroutine,
callback, RAII ownership에 맞게 표현만 바꾼다.

binding 기준은 아래 문서를 따른다.

- [C++ Binding Specification](/home/hep7/project/kairos/zlink/doc/spec/bindings/cpp/README.md)
- [C++ Codec Extension Specification](/home/hep7/project/kairos/zlink/doc/spec/bindings/cpp/codec.md)

framework public API는 `zlink::framework` namespace 아래에 둔다. 물리 구조는
`.NET` framework의 `Contracts/*`와 `Runtime/*` 분리를 따른다. C++에서 contract는
설치되는 public header이고, runtime은 `src/runtime/*` 안의 구현이다. binding의 public
타입은 framework 내부 구현과 일부 고급 extension point에서 사용할 수 있지만, 일반
사용자는 raw socket이나 poller를 직접 만지지 않아도 앱을 만들 수 있어야 한다.

## 2. Binding 대응표

framework 구현은 아래 C++ binding 타입을 기준으로 삼는다.

| Framework 개념 | Binding 기준 타입 | Framework에서의 역할 |
|----------------|------------------|----------------------|
| runtime context | `zlink::context_t` | app lifecycle 안에서 생성하고 종료한다. |
| message buffer | `zlink::message_t`, `zlink::multipart_t` | serializer가 typed payload를 변환하는 내부 메시지 단위다. |
| request/reply channel | `zlink::router_socket_t`, `zlink::dealer_socket_t` | channel server/client capability 구현에 사용한다. |
| pub/sub channel | `zlink::pub_socket_t`, `zlink::sub_socket_t` | topic publish/subscribe capability 구현에 사용한다. |
| stream ingress | `zlink::stream_socket_t` | STREAM packet/session capability 구현에 사용한다. |
| discovery | `zlink::service::discovery_t` | registry 기반 channel/spot 연결에 사용한다. |
| registry | `zlink::service::registry_t`, `zlink::service::registry_query_client_t` | embedded registry와 topology query에 사용한다. |
| spot node | `zlink::service::spot_node_t` | spot lifecycle과 channel attach를 관리한다. |
| spot | `zlink::service::spot_t` | spot publish, subscribe, direct routing, channel request/send에 사용한다. |
| async request | `zlink::async_result_t<T>` | framework call object와 pending submit 구현의 내부 기반이다. |
| codec | `message_t` 중심 codec API | JSON serializer 기본 구현에 사용한다. 기존 함수형 codec helper는 message 중심 표면으로 정렬한다. |

framework는 binding의 native handle, raw callback userdata, raw option key를 public
API로 올리지 않는다. 사용자가 필요한 것은 channel name, topic, typed payload,
handler, service lifetime, timeout 같은 framework 개념이다.

## 3. Header 와 Namespace

권장 public header layout은 아래와 같다. `contracts/*` 아래 header가 `.NET`
`Contracts/*`에 대응하는 실제 public contract owner이고, `zlink/framework/*.hpp`
header는 사용자가 편하게 include할 수 있는 facade다.

```text
zlink/framework.hpp
zlink/framework/app.hpp
zlink/framework/channels.hpp
zlink/framework/handlers.hpp
zlink/framework/spots.hpp
zlink/framework/streams.hpp
zlink/framework/timers.hpp
zlink/framework/contracts/actors/*.hpp
zlink/framework/contracts/channels/*.hpp
zlink/framework/contracts/codecs/*.hpp
zlink/framework/contracts/configuration/*.hpp
zlink/framework/contracts/dispatch/*.hpp
zlink/framework/contracts/errors/*.hpp
zlink/framework/contracts/eventing/*.hpp
zlink/framework/contracts/handlers/*.hpp
zlink/framework/contracts/registry/*.hpp
zlink/framework/contracts/spots/*.hpp
zlink/framework/contracts/streams/*.hpp
zlink/framework/contracts/timers/*.hpp
```

`zlink/framework/runtime.hpp` 같은 public header는 만들지 않는다. runtime이라는 이름은
구현 디렉토리 `framework/src/runtime/*`에만 사용한다. public API에서 runtime 객체가
필요하면 `app_t`, `host_t`, `channel_client_t`, `spot_context_t`처럼 사용자가 이해하는
계약 이름으로 노출한다.

`bindings/cpp`보다 framework 쪽의 분리를 더 강하게 잡는다. binding은 zlink core의
native 개념을 C++로 안전하게 감싸는 계층이지만, framework는 application contract를
제공하는 계층이다. 그래서 framework contract header가 binding public 타입을 내부
substrate로 참조할 수는 있어도, native socket owner, CAPI dispatch callback, raw recv
순서, frame codec 구현을 public contract로 끌어올리면 안 된다.

public header에 template 구현이 필요한 경우에는 `contracts/detail/*`만 사용한다.
이 detail 영역은 type trait, concept check, facade forwarding을 위한 곳이며,
runtime 구현을 숨겨 넣는 장소가 아니다.

이 구조는 `.NET`의 public interface를 C++ pure virtual class로 모두 옮긴다는 뜻이
아니다. C++ public API는 concrete facade와 value type을 적극적으로 사용할 수 있다.
다만 facade의 멤버, 생성자, method signature가 runtime 구현 타입을 노출하지 않아야 한다.
runtime 객체를 가리켜야 하는 public facade는 PIMPL, type-erased state, shared internal
state 같은 방식으로 구현을 숨긴다. 사용자 확장점만 abstract interface 또는 concept
contract로 둔다.

인터페이스 분리는 `.NET`보다 약하게 적용하지 않는다. `.NET`에서는 `Contracts/*`가
사용자와 extension author가 보는 타입이고, `Runtime/*`는 `internal` 구현이다. C++에는
assembly-level `internal`이 없으므로 물리적인 include 경계와 CMake install 경계로 같은
효과를 만든다. 설치되는 header는 contract와 facade뿐이며, runtime header는 build tree
안에서만 사용한다.

public type을 만들 때는 아래 질문에 모두 답해야 한다.

| 질문 | public contract에 둘 수 있는 경우 | runtime에 숨겨야 하는 경우 |
|------|----------------------------------|-----------------------------|
| 사용자가 직접 구현하는가? | handler, filter, serializer, hosted service처럼 구현 대상이면 둔다. | framework가 내부에서만 구현하면 숨긴다. |
| 사용자가 값을 조합하는가? | option, builder, typed result처럼 조합 대상이면 둔다. | queue node, dispatch token, recv state처럼 조합하지 않으면 숨긴다. |
| `.NET` Contracts에 대응하는가? | 같은 기능 축의 public 계약이면 C++ contract로 둔다. | `.NET` Runtime 또는 Runtime/Backend에 대응하면 숨긴다. |
| native 실행 순서를 드러내는가? | 드러내지 않으면 facade로 둘 수 있다. | poll/recv/drain 순서가 보이면 숨긴다. |

### 3.1 기능별 Contract/Runtime Owner

인터페이스 분리는 파일 이름만 맞추는 작업이 아니다. 기능을 구현하기 전에 어느 타입이
사용자 계약이고 어느 타입이 runtime 구현인지 먼저 닫아야 한다. 아래 표는 `.NET`
framework의 `Contracts/*`와 `Runtime/*` 구조를 C++ framework에 옮길 때의 기준이다.

| 기능 축 | C++ public contract owner | C++ runtime implementation owner | public에 두지 않는 것 |
|---------|---------------------------|----------------------------------|-----------------------|
| assembly/module discovery | `contracts/assembly/*` | `src/runtime/host/*` | module scan cache, startup ordering |
| app/host/config | `contracts/configuration/*`, `app.hpp` | `src/runtime/host/*`, `src/runtime/configuration/*` | native context owner, signal backend, startup graph |
| DI/scope | `contracts/configuration/services.hpp` | `src/runtime/configuration/services.*` | service cache, destruction stack, scope registry |
| error/result/call | `contracts/errors/*`, `contracts/channels/call.hpp`, `contracts/dispatch/task.hpp` | `src/runtime/messaging/*`, 기능별 runtime submitter | pending operation node, queue slot, completion token |
| handler | `contracts/handlers/*` | `src/runtime/handlers/*` | descriptor map, DI resolve order, reflection/shape cache |
| serializer/codec | `contracts/codecs/*` | `src/runtime/codecs/*`, 선택 codec target | JSON backend wiring, type-erased serializer map |
| channel | `contracts/channels/*` | `src/runtime/channels/*` | socket set, recv pump, reply correlation table, send-ready queue |
| SPOT | `contracts/spots/*` | `src/runtime/spots/*` | Spot activation table, native dispatch router, subscription pump |
| timer | `contracts/timers/*` | `src/runtime/timers/*` | native timer token, `fire_count` drain loop, timer registry |
| STREAM | `contracts/streams/*` | `src/runtime/streams/*` | frame codec, session table, session serial executor, transport loop |
| actor relay | `contracts/actors/*`, 필요한 stream contract | `src/runtime/actors/*`, `src/runtime/streams/*` | actor mailbox, join coordinator, relay packet dispatcher |
| registry/monitoring | `contracts/registry/*`, `contracts/eventing/*` | `src/runtime/registry/*`, `src/runtime/diagnostics/*` | topology cache, snapshot diff cache, backend query client owner |
| execution/offload | `contracts/dispatch/*` | `src/runtime/dispatch/*`, `src/runtime/execution/*` | thread pool queue, work item storage, shutdown drain state |
| backend substrate | 없음 | `src/runtime/backend/*`, `src/runtime/backend/contracts/*` | zlink binding adapter, backend private contract |

각 행은 구현 단계의 최소 파일 구조다. 예를 들어 channel 기능을 만들 때
`channel_builder_t`, `message_bus_t`, `request_client_t`는 public contract에 둘 수 있지만,
`dealer_socket_t` owner, pending request table, send-ready drain queue는 runtime owner에
둔다. public 타입이 내부 state를 가리켜야 하면 public header에는 전방 선언과
`shared_ptr`/PIMPL만 두고 state 정의는 runtime owner 파일에 둔다.

`src/runtime/channels/channel_pending_requests.*`는 `.NET`의
`ZLinkDealerMeshPendingRequests`에 대응한다. request sequence 발급, pending 등록,
reply completion, drain은 이 모듈이 맡고 `message_bus_t`나 public call object는 pending
table을 직접 알지 않는다.

`src/runtime/channels/channel_reply_writer.*`는 `.NET`의 `ZLinkChannelReplyWriter`에
대응한다. request envelope에서 correlation id와 message name을 보존한 response/error
header를 만들고, error code는 stable string으로 기록한다.

`src/runtime/channels/channel_packet_dispatcher.*`는 `.NET`의
`ZLinkChannelPacketDispatcher`에 대응한다. server ingress envelope를 해석하고 request는
handler result를 response envelope로 감싸며, command/send는 reply 없이 dispatch한다.

`src/runtime/channels/channel_bundle_factory.*`는 `.NET`의
`ZLinkChannelBundleFactory`에 대응한다. channel capability snapshot에서 client, server,
publisher, subscriber runtime bundle을 만들고 manual endpoint attachment를 bundle 내부로
옮긴다.

`src/runtime/channels/channel_runtime_manager.*`는 `.NET`의
`ZLinkChannelRuntimeManager`에 대응한다. capability bundle lazy creation, inbound/client/
publisher 초기화, route channel lookup, monitoring source parsing을 담당한다.

`src/runtime/channels/channel_runtime_bundle.*`는 `.NET`의
`ZLinkChannelRuntimeBundle`에 대응한다. manual connection set, receive gate,
dealer-mesh pending request owner를 capability 내부 상태로 묶고 public contract에는
노출하지 않는다.

`src/runtime/channels/channel_message_pump.*`와
`src/runtime/channels/channel_receive_loop.*`는 `.NET`의 `ZLinkChannelMessagePump`,
`ZLinkChannelReceiveLoop`에 대응한다. receive loop는 수신 queue를 drain하고 재진입을
막으며, message pump는 envelope dispatch를 `channel_packet_dispatcher_t`로 보낸다.

`src/runtime/channels/route_connection_set.*`는 `.NET`의 `ZLinkRouteConnectionSet`에
대응한다. route channel의 manual connection 목록과 중복 제거, 정렬 snapshot을 담당한다.

`src/runtime/channels/route_channel_registration.*`는 `.NET`의
`ZLinkRouteChannelRegistration`, `ZLinkRouteChannelBuilder`,
`ZLinkRouteChannelInitializer`에 대응한다. C++에는 런타임 reflection scanner가 없으므로
typed handler installer를 registration에 모으고 initializer가 route runtime과
`route_handler_registry_t`를 생성한다. public 표면은 `route_channel_builder_t`가 맡고,
template handler registration은 public contract에서 typed invoker value로 접힌 뒤 runtime
registration으로 전달된다.

`src/runtime/channels/route_channel_runtime.*`는 `.NET`의
`ZLinkRouteChannelRuntime`에 대응한다. route channel id, connection set, outbound
command/request envelope 작성, SPOT routed parts 전송, request sequence correlation을
소유한다. native router socket adapter는 `src/runtime/backend/native_route_backend.*`가
담당하고 public contract에 올리지 않는다.

public `route_client_t`, `route_send_call_t`, `route_request_call_t`,
`typed_route_request_call_t<TReply>`는 `.NET`의 `IZLinkRouteClient`와 `ZLinkRouteClient`에
대응한다. 사용자는 router channel id, target node routing id, typed payload만 넘기고,
route channel runtime lookup, envelope 작성, serializer 호출은 runtime owner가 처리한다.
C++는 낮은 수준 검증을 위해 request sequence submission call도 유지하지만, 일반 사용 표면은
`request<TRequest, TReply>(...).packet_name(...).timeout(...).submit()`으로 typed reply를
받는다. typed reply completion은 route runtime backend seam을 통해 검증되고,
`native_route_backend_t`가 C++ binding `router_socket_t::send/request`로 이 seam에 붙는다.
남은 작업은 runtime manager가 실제 router socket lifecycle과 discovery attach를 만들 때
이 adapter를 자동으로 연결하는 것이다.

`src/runtime/channels/route_packet_dispatcher.*`와
`src/runtime/channels/route_receive_pump.*`는 `.NET`의 `ZLinkRoutePacketDispatcher`,
`ZLinkRouteReceivePump`에 대응한다. route receive pump는 routed packet queue를 drain하고,
dispatcher는 envelope header를 해석한다. command는 routed handler나 internal dispatcher로
보내고, request는 handler reply 또는 error envelope를 만든다.

`src/runtime/channels/route_handler_registry.*`와
`src/runtime/channels/route_handler_invoker.*`는 `.NET`의 `ZLinkRouteHandlerRegistry`,
`ZLinkRouteHandlerInvoker`에 대응한다. routed handler descriptor, duplicate detection,
source routing id가 포함된 route context, typed payload deserialize/serialize를 소유한다.

`src/runtime/channels/route_internal_packet_dispatcher.*`는 `.NET`의
`IZLinkRouteInternalPacketDispatcher`, `ZLinkNoRouteInternalPacketDispatcher`,
`ZLinkCompositeRouteInternalPacketDispatcher`에 대응한다. framework 내부 routed packet은
사용자 route handler보다 먼저 처리하고, 여러 internal dispatcher를 composite로 묶을 수
있다.

`backend substrate` 행은 의도적으로 public owner가 없다. `.NET`의
`Runtime/Backend/Contracts`처럼 backend 내부 계약이 필요할 수는 있지만, 이 계약은
framework와 zlink binding substrate 사이의 private seam이다. 사용자가 보는 extension
point가 필요하면 `contracts/*` 아래 별도 public 타입을 만든다. backend 내부 타입을
이름만 바꿔 public header에 올리는 방식은 허용하지 않는다.

#### Runtime/Messaging 상태 분리

`pending_operation_t`는 callback 기반 `submit(callback)`의 추적 핸들이다. 사용자는 이
객체로 작업이 유효한지, 완료됐는지, 취소됐는지를 확인할 수 있다. 상태 저장소, 실패
예외, queue slot은 public header에 두지 않고 `src/runtime/messaging`의 private state에
둔다.

`src/runtime/messaging/pending_submit.*`는 `.NET`의 `PendingSubmit`에 대응한다. C++에서는
public cancellation token을 두지 않으므로 cancellation registration은 만들지 않는다.
대신 command submit의 accepted 완료, request submit의 별도 응답 완료, deadline 만료,
wake callback은 같은 책임으로 유지한다.

`src/runtime/messaging/submit_queue.*`는 `.NET`의 `ZLinkSubmitQueue`에 대응한다. 큐는
bounded FIFO이며 capacity 초과와 disposed 상태를 내부에서 막는다. public channel call
object는 큐 구현을 알 필요가 없고 `pending_operation_t`만 받는다.

`src/runtime/messaging/envelope_codec.*`는 `.NET`의 `ZLinkEnvelopeCodec`에 대응한다.
header/body 2-part envelope, `application/json` content type, error envelope header,
body part 누락 검사는 이 모듈이 맡는다. 사용자는 envelope JSON 구조를 직접 만들지 않는다.

`src/runtime/messaging/client_call_codec.*`는 `.NET`의 `ZLinkClientCallCodec`에 대응한다.
request/command/publish header 생성, correlation id, deadline 문자열, typed body encode,
reply body decode, error reply 해석을 한 곳에 둔다.

`src/runtime/messaging/request_failure_mapper.*`는 `.NET`의 `ZLinkRequestFailureMapper`에
대응한다. native request result나 error envelope code를 `framework_error_kind_t`와
retriable 여부로 사상한다. 이 매핑은 handler, channel, connector sample에 흩어져 있으면
안 된다.

`ZLinkMessageNameResolver`에 해당하는 C++ 정책은 DTO의
`static constexpr const char *packet_name`을 우선 사용하는 것이다. framework handler
등록과 Stream Connector send/request/on 기본 이름은 이 값을 읽는다. 이름이 없는 타입은
fallback으로 C++ type name을 사용할 수 있지만, 샘플과 정식 DTO는 명시 packet name을
가져야 한다. 이렇게 해야 handler 등록, client 호출, server push가 같은 문자열을 반복해서
관리하지 않는다.

### 3.2 C++에서 생기는 분리 이슈와 결정

`.NET`은 assembly `internal`로 runtime 타입을 숨길 수 있지만 C++는 header를 설치하면
그 자체가 공개 표면이 된다. 그래서 C++ framework는 아래 결정을 따른다.

| 이슈 | 잘못된 방향 | 결정 |
|------|-------------|------|
| template 때문에 구현이 header에 들어감 | descriptor map, queue, socket owner까지 template header에 넣는다. | header에는 type check와 forwarding만 둔다. 실제 저장소와 실행은 type-erased runtime으로 보낸다. |
| concrete facade가 interface 분리를 약하게 만듦 | public class 멤버에 runtime 타입을 직접 둔다. | concrete facade는 허용하지만 상태는 PIMPL/type-erased state로 숨긴다. |
| 성능을 이유로 runtime state를 노출하고 싶어짐 | 사용자가 pending table, native socket, timer token을 직접 다루게 한다. | public 표면은 깊게 유지한다. 필요한 성능 조절은 option, executor, queue limit 같은 contract로 제공한다. |
| 외부 dependency가 public header로 번짐 | JSON/MessagePack/Protobuf 타입을 기본 public signature에 넣는다. | codec별 선택 target 또는 message boundary 뒤에 둔다. 기본 framework/connector는 불필요한 codec dependency를 요구하지 않는다. |
| 테스트가 private header를 include함 | unit test가 runtime header에 의존해 public 계약을 우회한다. | contract test는 public header만 include한다. runtime unit test만 private header를 include할 수 있다. |
| ABI와 inline 구현이 뒤섞임 | public inline 함수가 runtime 자료구조를 직접 조작한다. | inline은 validation과 forwarding으로 제한한다. 자료구조 변경 가능성은 runtime에 숨긴다. |

이 결정 때문에 public 타입 수가 `.NET`보다 조금 많아질 수 있다. C++에는 interface,
record, extension method를 같은 방식으로 표현할 수 없기 때문이다. 하지만 사용자가 보는
개념 수는 늘리지 않는다. 사용자는 app, channel, handler, Spot, stream, timer, connector
같은 framework 개념만 다루고, runtime 실행 순서와 native 소유권은 알 필요가 없어야 한다.

따라서 C++ public header는 다음 두 계층만 가진다.

- `contracts/*`: 실제 public contract owner다. 타입 의미와 호출 shape를 정의한다.
- `zlink/framework/*.hpp`: include 편의를 위한 facade다. 새 계약을 발명하지 않고
  `contracts/*`를 다시 묶는다.

아래 항목은 public contract가 아니라 runtime 구현이다.

- handler descriptor map과 dispatch lookup table
- serializer type-erased map과 JSON backend wiring
- socket/context lifecycle owner
- pending request table과 send-ready queue
- stream frame encoder/decoder 구현
- ActorGateway frame codec과 remote locator
- timer native token과 fire-count drain loop
- monitoring snapshot diff cache

public facade가 runtime 상태를 보관해야 할 때도 상태 정의는 header에 두지 않는다.
예시는 아래 형태다.

```cpp
namespace zlink::framework {

namespace detail {
class handler_registry_state_t;
}

class handler_registry_t {
public:
    handler_registry_t();
    ~handler_registry_t();

private:
    std::shared_ptr<detail::handler_registry_state_t> _state;
};

}
```

위 예시에서 `handler_registry_state_t`의 정의와 descriptor map 구현은
`framework/src/runtime/handlers/*`에만 둔다. public header는 사용자가 호출할 method와
template forwarding만 가진다.

### 3.3 구현 전 Interface Separation 절차

새 기능을 구현하기 전에는 아래 순서로 `.NET` 구조와 C++ 구조를 맞춘다. 이 절차는
코드 리뷰 때 사후로 확인하는 항목이 아니라, public header를 만들기 전에 끝내야 하는
선행 작업이다.

1. 같은 기능의 `.NET Contracts/*` 파일을 확인하고 사용자가 보는 타입과 호출 shape를
   적는다.
2. 같은 기능의 `.NET Runtime/*` 파일을 확인하고 내부 구현 책임을 적는다.
3. C++ public owner를 `framework/include/zlink/framework/contracts/*` 또는 facade
   header 중 하나로 정한다.
4. C++ runtime owner를 `framework/src/runtime/*` 아래의 기능별 디렉토리로 정한다.
5. public header가 runtime header를 include하지 않는지, runtime 타입을 signature로
   노출하지 않는지 layout contract test로 고정한다.
6. template 구현이 필요하면 `contracts/detail/*`에 type trait, concept check,
   forwarding만 남기고 state와 실행 구현은 runtime owner로 보낸다.

아래 표는 구현 시 확인해야 하는 파일 대응의 기준이다.

| 확인 대상 | C++에 남길 것 | C++에서 숨길 것 |
|-----------|---------------|-----------------|
| `.NET Contracts/Channels` | channel builder, client, call object, result shape | pending request table, route receive pump |
| `.NET Contracts/Handlers` | handler option, invocation context, filter contract | descriptor cache, DI resolve order, method invoker |
| `.NET Contracts/Spots` | Spot context, Spot RID view, actor factory shape | activation table, native dispatch router, subscription pump |
| `.NET Contracts/Streams` | stream header, session, bound session, stream error | frame codec, session table, serial executor |
| `.NET Contracts/Registry` | registry options, query model, topology result | backend discovery owner, topology cache, route resolver state |
| `.NET Contracts/Timers` | timer handle, timer option, tick model | native timer token, fire-count drain loop |
| `.NET Contracts/Eventing` | typed runtime event, sink registration | snapshot diff cache, telemetry backend |
| `.NET Runtime/Execution` | public dispatch/offload option만 노출 | thread pool queue, work item storage, drain state |

이 표에서 `C++에 남길 것`은 반드시 pure virtual interface일 필요가 없다. C++ public
타입은 concrete facade일 수 있다. 다만 facade가 깊은 모듈이어야 하므로, 사용자가
runtime 실행 순서나 native 소유권을 기억하지 않아도 같은 기능을 쓸 수 있어야 한다.

분리 기준이 애매할 때는 아래 결정을 따른다.

| 상황 | 결정 |
|------|------|
| public API가 binding 타입을 받아야 하는가? | `message_t`처럼 payload boundary를 나타내는 값 타입만 허용한다. socket, context, native owner는 runtime에 둔다. |
| 성능 때문에 inline 구현이 필요한가? | validation과 forwarding만 inline으로 둔다. queue 조작, dispatch, codec encode/decode는 runtime으로 보낸다. |
| 테스트가 private state를 확인해야 하는가? | public contract test는 public header만 사용한다. private state 검증은 runtime unit test로 분리한다. |
| connector가 framework 타입을 재사용하고 싶은가? | wire 의미와 codec 정책만 공유한다. connector public header가 framework runtime이나 server facade를 include하지 않는다. |

모든 framework 타입은 `zlink::framework` namespace 아래에 둔다.

```cpp
namespace zlink::framework {

class app_t;
class service_collection_t;
class service_provider_t;
class handler_registry_t;
class serializer_registry_t;
class config_builder_t;
class logging_builder_t;
class metrics_builder_t;
class health_builder_t;
class zlink_builder_t;
class registry_builder_t;
class discovery_builder_t;
class channel_builder_t;
class spot_node_builder_t;
class stream_builder_t;
class message_bus_t;
class publisher_t;
class request_client_t;
class spot_context_t;
class stream_header_t;
class stream_error_t;
class stream_t;
class packet_stream_session_t;
class module_t;
class hosted_service_t;

} // namespace zlink::framework
```

## 4. App / Host

`app_t`는 framework의 가장 바깥 public type이다. 사용자는 `app_t::create()`로 앱을
만들고, services, handlers, zlink runtime을 구성한 뒤 `run`을 호출한다.

```cpp
namespace zlink::framework {

class app_t {
public:
    static app_t create();

    service_collection_t &services();
    handler_registry_t &handlers();
    config_builder_t &config();
    logging_builder_t &logging();
    monitoring_builder_t &monitoring();

    app_t &use_zlink(std::function<void(zlink_builder_t &)> configure);
    app_t &add_module(module_t &module);
    app_t &add_zlink_framework(
      std::function<void(zlink_framework_options_t &)> configure);
    template <typename TModule, typename... TArgs>
    app_t &add_zlink_framework(TArgs &&...args);
    app_t &add_hosted_service(std::unique_ptr<hosted_service_t> service);

    int run(int argc, char **argv);
    void stop();
    void request_stop();
};

} // namespace zlink::framework
```

`run`은 `int`를 반환한다. 반환값은 process exit code로 사용할 수 있어야 한다.
handler 예외, runtime 오류, signal shutdown은 host가 수집하고 종료 경로를 닫는다.

## 5. DI

DI는 자체 container로 구현한다. C++ binding에는 DI 개념이 없으므로, framework
계층이 service lifetime과 handler owner resolve를 직접 제공한다.

```cpp
namespace zlink::framework {

enum class service_lifetime_t {
    singleton,
    scoped,
    transient
};

class service_provider_t {
public:
    template <typename T>
    T &get_required();

    template <typename T>
    std::optional<std::reference_wrapper<T>> get();
};

class service_collection_t {
public:
    template <typename T>
    service_collection_t &add_singleton();

    template <typename T, typename... TDependencies>
    service_collection_t &add_singleton();

    template <typename T>
    service_collection_t &add_singleton(std::unique_ptr<T> instance);

    template <typename T>
    service_collection_t &add_scoped();

    template <typename T, typename... TDependencies>
    service_collection_t &add_scoped();

    template <typename T>
    service_collection_t &add_transient();

    template <typename T, typename... TDependencies>
    service_collection_t &add_transient();

    template <typename T>
    service_collection_t &add_factory(
      std::function<std::unique_ptr<T>(service_provider_t &)> factory);
};

} // namespace zlink::framework
```

기본 생성 규칙은 아래와 같다.

- `add_singleton<T>()`, `add_transient<T>()`는 기본 생성 가능한 타입만 자동 생성한다.
- 생성자 의존성이 있는 타입은 `add_singleton<T, Dep1, Dep2>()`,
  `add_scoped<T, Dep1, Dep2>()`, `add_transient<T, Dep1, Dep2>()`처럼 의존 타입을 명시한다.
  framework는 `service_provider_t`에서 `Dep1`, `Dep2`를 resolve한 뒤 `T(Dep1 &, Dep2 &)`를
  호출한다.
- `add_scoped<T>()`는 framework-owned scope 안에서만 resolve한다.
- 복잡한 외부 객체 생성이나 조건부 생성이 필요한 경우에만 `add_factory<T>()`를 사용한다.
- handler owner는 service collection에 등록되어 있어야 한다.
- 등록되지 않은 handler owner를 framework가 암묵적으로 생성하지 않는다.
- `Boost.Ext.DI` 같은 외부 DI 라이브러리는 public dependency로 두지 않는다.

`scoped` lifetime은 zlink core 기능이 아니라 framework-owned DI lifetime이다. `.NET`
framework가 `IServiceScope`를 만들어 handler dispatch, STREAM session, Spot activation
수명에 붙이는 것처럼, C++ framework도 자체 DI container에서 같은 scope 경계를 만든다.
channel handler는 dispatch마다 scope를 만들고, STREAM session은 session scope를 가지며,
Spot과 Entry Spot은 activation scope를 가진다. actor factory는 actor creation scope에서
resolve하고, actor instance 자체는 actor runtime이 소유한다.

예시는 아래와 같다.

```cpp
app.services()
  .add_singleton<order_repository_t>()
  .add_transient<order_service_t, order_repository_t>()
  .add_transient<order_handler_t, order_service_t>();
```

## 6. Runtime Builder

runtime builder는 binding의 `zlink::context_t`, socket classes,
`zlink::service::discovery_t`, `zlink::service::spot_node_t` 생성을 숨긴다.

```cpp
namespace zlink::framework {

class zlink_builder_t {
public:
    zlink_builder_t &node(std::string node_name);
    zlink_builder_t &registry(std::function<void(registry_builder_t &)> configure);
    zlink_builder_t &discovery(std::function<void(discovery_builder_t &)> configure);
    zlink_builder_t &channel(std::string channel_name,
      std::function<void(channel_builder_t &)> configure);
    zlink_builder_t &spot_node(std::string spot_node_name,
      std::function<void(spot_node_builder_t &)> configure);
    zlink_builder_t &stream(std::string stream_name,
      std::function<void(stream_builder_t &)> configure);
};

class discovery_builder_t {
public:
    discovery_builder_t &connect_registry(std::string endpoint);
};

class registry_builder_t {
public:
    registry_builder_t &bind(std::string endpoint);
};

class stream_builder_t {
public:
    stream_builder_t &bind(std::string endpoint);
    stream_builder_t &packet_session(std::string session_name);
    stream_builder_t &attach_actor_gateway(std::string spot_node_name);
};

} // namespace zlink::framework
```

`zlink_builder_t`는 raw socket 생성 순서를 사용자가 기억하지 않게 해야 한다.
framework 내부는 아래 binding 타입을 조합한다.

- `zlink::context_t`
- `zlink::router_socket_t`
- `zlink::dealer_socket_t`
- `zlink::pub_socket_t`
- `zlink::sub_socket_t`
- `zlink::service::discovery_t`
- `zlink::service::spot_node_t`
- `zlink::stream_socket_t`

## 7. Channel Builder

channel은 framework에서 request/reply와 pub/sub capability를 묶는 이름이다.

```cpp
namespace zlink::framework {

class channel_builder_t {
public:
    channel_builder_t &enable_server();
    channel_builder_t &enable_server(
      std::function<void(server_capability_builder_t &)> configure);

    channel_builder_t &enable_client();
    channel_builder_t &enable_client(
      std::function<void(client_capability_builder_t &)> configure);

    channel_builder_t &enable_publisher();
    channel_builder_t &enable_publisher(
      std::function<void(publisher_capability_builder_t &)> configure);

    channel_builder_t &enable_subscriber();
    channel_builder_t &enable_subscriber(
      std::function<void(subscriber_capability_builder_t &)> configure);
};

class server_capability_builder_t {
public:
    server_capability_builder_t &bind(std::string endpoint);
};

class client_capability_builder_t {
public:
    client_capability_builder_t &connect(std::string endpoint);
    client_capability_builder_t &use_discovery();

    client_capability_builder_t &send_timeout(std::chrono::milliseconds timeout);
    client_capability_builder_t &request_timeout(std::chrono::milliseconds timeout);
    client_capability_builder_t &pending_queue_limit(std::size_t count);
};

class publisher_capability_builder_t {
public:
    publisher_capability_builder_t &bind(std::string endpoint);
    publisher_capability_builder_t &send_timeout(std::chrono::milliseconds timeout);
};

class subscriber_capability_builder_t {
public:
    subscriber_capability_builder_t &connect(std::string endpoint);
    subscriber_capability_builder_t &use_discovery();
};

} // namespace zlink::framework
```

내부 매핑은 아래와 같다.

| Capability | Binding 구현 기준 |
|------------|------------------|
| server | `zlink::router_socket_t` |
| client | `zlink::dealer_socket_t` |
| publisher | `zlink::pub_socket_t` |
| subscriber | `zlink::sub_socket_t` |

같은 channel 안에서도 capability별 연결 집합은 분리한다. 예를 들어
`orders.client`와 `orders.subscriber`는 같은 channel 이름을 공유하지만 서로 다른
socket과 연결 정책을 가진다.

따라서 `bind`, `connect`, `use_discovery` 같은 연결 설정은 channel 전체가 아니라
`server`, `client`, `publisher`, `subscriber` capability builder에 둔다.

## 8. Handler Registry

handler registry는 typed payload를 함수 수준에서 처리하게 하는 표면이다.

```cpp
namespace zlink::framework {

enum class handler_execution_t {
    standard,
    offload
};

struct handler_options_t {
    std::optional<std::string> packet_name;
    std::optional<std::chrono::milliseconds> timeout;
    std::optional<std::size_t> max_concurrency;
    handler_execution_t execution = handler_execution_t::standard;
    bool ordered = false;
};

enum class timer_overrun_policy_t {
    skip_late_ticks = 1,
    catch_up_bounded = 2,
    delay_next_tick = 3
};

struct timer_options_t {
    timer_overrun_policy_t overrun_policy =
      timer_overrun_policy_t::skip_late_ticks;
    std::uint32_t max_catch_up_ticks = 1;
    bool stop_on_unhandled_exception = false;
};

struct timer_tick_t {
    std::string name;
    std::uint64_t delivery_index;
    std::uint64_t scheduled_index;
    std::chrono::nanoseconds period;
    std::chrono::steady_clock::duration scheduled_elapsed;
    std::chrono::steady_clock::duration started_elapsed;
    std::chrono::steady_clock::duration delay;
    std::uint64_t skipped_ticks;
};

class timer_t;
class send_call_t;
class relay_call_t;
class stream_write_call_t;
template <typename TReply>
class actor_join_spot_call_t;
class actor_join_entry_spot_call_t;

template <typename T>
class task_t;

template <typename T>
class result_t;

class pending_operation_t;

class actor_ref_t {
public:
    actor_ref_t(zlink::routing_id_t node_rid,
      std::string actor_id,
      std::uint64_t generation);

    zlink::routing_id_t node_rid() const;
    std::string_view actor_id() const;
    std::uint64_t generation() const;
    bool is_unchecked() const;
};

template <typename TReply>
struct actor_join_result_t {
    int result_code;
    actor_ref_t actor;
    TReply reply;
};

enum class framework_error_kind_t {
    actor_route_not_found,
    actor_create_failed,
    actor_already_exists,
    actor_type_mismatch,
    spot_create_failed,
    spot_route_not_found,
    spot_type_mismatch,
    actor_session_not_bound,
    handler_not_found,
    route_handler_not_found,
    actor_dispatch_handler_not_found,
    payload_decode_failed,
    route_not_connected,
    request_target_not_found,
    request_rejected,
    request_protocol_error,
    request_failed,
    timeout,
    shutdown,
    disconnected,
    closed
};

class framework_exception_t : public std::exception {
public:
    framework_error_kind_t kind() const noexcept;
    bool is_retriable() const noexcept;
};

class pending_operation_t {
public:
    pending_operation_t() noexcept;

    static pending_operation_t make_completed();

    bool valid() const noexcept;
    bool completed() const noexcept;
    bool cancelled() const noexcept;
    bool cancel() noexcept;
};

template <typename TReply>
class request_call_t {
public:
    request_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<TReply> submit();
    pending_operation_t submit(std::function<void(result_t<TReply>)> callback);
};

class send_call_t {
public:
    send_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<void> submit();
    pending_operation_t submit(std::function<void(result_t<void>)> callback);
};

class relay_call_t {
public:
    relay_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<void> submit();
    pending_operation_t submit(std::function<void(result_t<void>)> callback);
};

class stream_write_call_t {
public:
    stream_write_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<void> submit();
    pending_operation_t submit(std::function<void(result_t<void>)> callback);
};

template <typename TActor>
class bind_actor_call_t {
public:
    bind_actor_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<TActor> submit();
    pending_operation_t submit(std::function<void(result_t<TActor>)> callback);
};

enum class stream_message_kind_t : std::uint8_t {
    send = 1,
    request = 2,
    response = 3,
    error = 4,
    control = 5
};

enum class stream_codec_t : std::uint8_t {
    raw = 0,
    json = 1,
    message_pack = 2,
    protobuf = 3
};

enum class stream_header_flags_t : std::uint8_t {
    none = 0,
    has_request_seq = 0x01,
    has_metadata = 0x02,
    payload_compressed = 0x04
};

enum class stream_session_error_t {
    internal,
    transport_error,
    handshake_failed
};

class stream_error_t {
public:
    stream_session_error_t error() const;
    int native_code() const;
    std::string_view message() const;
};

class stream_header_t {
public:
    stream_message_kind_t kind() const;
    stream_codec_t codec() const;
    stream_header_flags_t flags() const;
    std::optional<std::uint64_t> request_seq() const;
    std::string_view packet_name() const;
    std::optional<std::string_view> metadata(std::string_view key) const;
    std::optional<std::string_view> correlation_id() const;
};

class stream_t {
public:
    virtual ~stream_t() = default;
    virtual std::string session_id() const = 0;
    virtual stream_write_call_t write_packet(
      const stream_header_t &header,
      const zlink::message_t &payload) = 0;
};

class session_actor_t {
public:
    relay_call_t relay(
      const stream_header_t &header,
      const zlink::message_t &payload);
};

class session_actor_manager_t {
public:
    bind_actor_call_t<session_actor_t> bind(actor_ref_t actor);
};

class packet_stream_session_t {
public:
    virtual ~packet_stream_session_t() = default;
    virtual task_t<void> on_connected(stream_t &stream) = 0;
    virtual task_t<void> on_disconnected(stream_t &stream) = 0;
    virtual task_t<void> on_error(stream_t &stream, const stream_error_t &error) = 0;
    virtual task_t<void> on_packet(
      stream_t &stream,
      const stream_header_t &header,
      const zlink::message_t &payload) = 0;
};

class handler_registry_t {
public:
    template <typename TOwner, typename TEvent>
    handler_registry_t &on_event(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TEvent &),
      handler_options_t options = {});

    template <typename TOwner, typename TRequest, typename TReply>
    handler_registry_t &on_request(
      std::string channel_name,
      std::string topic,
      TReply (TOwner::*method)(const TRequest &),
      handler_options_t options = {});

    template <typename TOwner, typename TCommand>
    handler_registry_t &on_send(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TCommand &),
      handler_options_t options = {});

    handler_registry_t &send_raw(
      std::string channel_name,
      std::string topic,
      std::string packet_name,
      std::function<result_t<void>(const payload_view_t &)> handler,
      handler_options_t options = {});

    handler_registry_t &observe_failures(
      std::function<void(const handler_failure_event_t &)> observer);
};

} // namespace zlink::framework
```

handler owner 타입은 service collection에서 resolve한다.

```cpp
app.services().add_transient<order_handler_t>();

app.handlers()
  .on_event<order_handler_t, order_created_t>(
    "orders",
    "orders.created",
    &order_handler_t::on_created);
```

handler dispatch는 binding의 `zlink::message_t`와 `zlink::multipart_t`를 받은 뒤,
serializer를 통해 typed payload로 변환하고, DI에서 owner를 resolve한 다음 method를
호출한다.

STREAM handler는 일반 request/send/event handler와 분리한다. framework core는 packet
방식만 지원하고, header도 framework가 정의한 `stream_header_t`만 사용한다. raw stream
session과 사용자 정의 header framing은 core public 표면에 넣지 않는다.

stream callback은 framework가 packet을 수신하고 header 검증을 마친 뒤 호출한다. 별도
실행기로 넘기는 것이 기본은 아니며, 같은 stream session의 packet/lifecycle callback은
직렬로 처리한다. CPU-bound 또는 blocking 가능성이 있는 stream handler는 offload 실행
정책을 명시한다.

request handler 반환값은 `TReply` 또는 `task_t<TReply>`를 허용한다. `task_t<TReply>`를
반환하는 handler는 `.NET`의 `async Task<TReply>` handler와 같은 의미이며, 내부
request/send/relay도 `co_await call.submit()` 형태로 사용한다.

handler 실행은 framework runtime의 coroutine executor를 통과한다. 이 executor는 내부적으로
`boost::asio::thread_pool`과 `boost::asio::co_spawn`으로
`boost::asio::awaitable<result_t<T>>`를 실행한다. 그러나 public API에는
`boost::asio::awaitable`, executor, strand 타입을 노출하지 않는다. 사용자 코드는
`task_t<T>`, `co_await call.submit()`, callback submit만 본다.
내부 handler invoker는 `result_t<T>`를 직접 반환하지 않고 `task_t<T>`를 반환한다.
executor는 task 완료를 callback으로 받아 Asio coroutine을 재개한다. 따라서 async handler
실행 중에 `.result()`로 기다리는 bridge는 없다. `.result()`는 C core dispatch callback처럼
동기 응답을 돌려줘야 하는 가장 바깥 runtime 경계에서만 사용한다.
`task_t<T>`는 같은 task를 여러 coroutine이 await할 수 있다. 완료 상태는 한 번만 확정되며,
중복 완료 시도는 기존 결과를 덮어쓰지 않는다. executor 완료 callback은 완료 결과를
불필요하게 반복 복사하지 않고, 다른 executor thread로 넘겨야 하는 지점에서만 복사한다.
handler coroutine executor는 기본적으로 CPU 수 기반 worker pool을 사용하며,
`handler_coroutine_workers(n)`으로 명시 설정할 수 있다.

## 9. Messaging API

사용자 코드에서 raw socket 대신 주입받아 쓰는 messaging 표면은 아래와 같다.

```cpp
namespace zlink::framework {

struct send_options_t {
    std::optional<std::string> packet_name;
    std::optional<std::chrono::milliseconds> timeout;
};

struct request_options_t {
    std::optional<std::string> packet_name;
    std::optional<std::chrono::milliseconds> timeout;
};

class publisher_t {
public:
    template <typename TEvent>
    void publish(std::string_view channel_name,
      std::string_view topic,
      const TEvent &event,
      send_options_t options = {});
};

class request_client_t {
public:
    template <typename TCommand>
    send_call_t send(std::string_view channel_name, const TCommand &command,
      send_options_t options = {});

    template <typename TReply, typename TRequest>
    request_call_t<TReply> request(std::string_view channel_name,
      const TRequest &request,
      request_options_t options = {});
};

class message_bus_t {
public:
    publisher_t &publisher();
    request_client_t &client();
};

class route_client_t {
public:
    template <typename TMessage>
    route_send_call_t send(std::string router_channel_id,
      zlink::routing_id_t target_node_rid,
      TMessage message);

    template <typename TRequest>
    route_request_call_t request(std::string router_channel_id,
      zlink::routing_id_t target_node_rid,
      TRequest request);

    template <typename TRequest, typename TReply>
    typed_route_request_call_t<TReply> request(std::string router_channel_id,
      zlink::routing_id_t target_node_rid,
      TRequest request);
};

} // namespace zlink::framework
```

내부 구현은 binding의 `dealer_socket_t::request(...)`, `pub_socket_t::publish(...)`,
`service::spot_t::request_channel(...)`, `service::spot_t::send_channel(...)` 중
runtime topology에 맞는 경로를 선택한다. public API는 channel name과 typed payload를
기준으로 유지한다.

framework는 아래 서비스를 기본 등록한다. 사용자는 직접 생성하지 않고 DI에서
주입받아 사용할 수 있다.

- `message_bus_t`
- `publisher_t`
- `request_client_t`
- `route_client_t`
- `serializer_registry_t`

## 10. Serialization

framework serializer는 binding의 message 중심 codec API 위에 얹는다. binding의
codec 구조도 connector와 같은 방향으로 맞춘다. 즉 base binding은 raw `message_t`와
protocol enum만 제공하고, JSON, MessagePack, Protobuf 구현은 선택 codec target이
제공한다. JSON 기본 구현은 `message_t::from_json(...)`,
`message.parse_json<T>()` 같은 표면과 `nlohmann/json`을 기준으로 한다.

```cpp
namespace zlink::framework {

class serializer_registry_t {
public:
    template <typename T>
    serializer_registry_t &add_json();

    template <typename T>
    serializer_registry_t &add(
      std::function<zlink::message_t(const T &)> serialize,
      std::function<T(const zlink::message_t &)> deserialize);
};

template <typename T>
class serializer_t {
public:
    zlink::message_t serialize(const T &value) const;
    T deserialize(const zlink::message_t &message) const;
};

} // namespace zlink::framework
```

framework public handler와 messaging API는 `zlink::message_t`를 일반 사용자에게
강요하지 않는다. 다만 고급 handler는 raw message를 직접 받을 수 있다.

binding codec helper는 아래 방향으로 변경한다.

```cpp
auto message = zlink::message_t::from_json(order);
auto order = message.parse_json<order_created_t>();
```

기존 codec namespace의 함수형 helper는 이행 기간의 호환 shim으로만 둔다. 신규
framework, connector, binding 샘플과 테스트는 message 중심 표면을 기준으로 고정한다.
base binding target은 JSON, MessagePack, Protobuf dependency를 갖지 않는다. 각 codec은
별도 선택 target이 제공하되, 사용자가 해당 target을 링크하고 header를 include한 경우에만
관련 `message_t` helper가 열린다.

```cmake
target_link_libraries(app PRIVATE zlink::cpp)

# 필요할 때만 추가한다.
target_link_libraries(app PRIVATE zlink::cpp_codec_json)
target_link_libraries(app PRIVATE zlink::cpp_codec_messagepack)
target_link_libraries(app PRIVATE zlink::cpp_codec_protobuf)
```

```cpp
app.handlers()
  .send_raw("orders", "orders.raw", [](const zlink::message_t &message) {
      // raw payload path
  });
```

## 11. Spot Framework API

framework spot 표면은 binding의 `zlink::service::spot_node_t`와
`zlink::service::spot_t`를 기반으로 한다.

```cpp
namespace zlink::framework {

class spot_node_builder_t {
public:
    spot_node_builder_t &bind(std::string endpoint);
    spot_node_builder_t &enable_router(std::string endpoint);
    spot_node_builder_t &enable_router(
      std::string endpoint,
      zlink::routing_id_t routing_id);
    spot_node_builder_t &enable_pub_sub(std::string endpoint);
    spot_node_builder_t &enable_pub_sub(
      std::string endpoint,
      zlink::routing_id_t routing_id);
    spot_node_builder_t &use_discovery(std::string channel_name);
    spot_node_builder_t &enable_actor_gateway();
    spot_node_builder_t &attach_channel_client(std::string channel_name);
    spot_node_builder_t &attach_publisher(std::string channel_name);

    template <typename TEntrySpot>
    spot_node_builder_t &add_entry_spot();

    template <typename TSpot>
    spot_node_builder_t &add_spot(std::string spot_name);

    template <typename TActorFactory>
    spot_node_builder_t &add_actor_factory(std::string actor_type);
};

class spot_context_t {
public:
    node_rid_t node_rid() const;
    spot_rid_t spot_rid() const;
    std::string spot_name() const;
    spot_handler_registry_t handlers();

    template <typename TCommand>
    send_call_t send_to(node_rid_t node_rid,
      spot_rid_t spot_rid,
      TCommand command);

    template <typename TReply, typename TRequest>
    request_call_t<TReply> request_to(node_rid_t node_rid,
      spot_rid_t spot_rid,
      TRequest request);

    template <typename TEvent>
    send_call_t publish(std::string topic, TEvent event);

    template <typename THandler>
    timer_t add_timer(std::string name,
      std::chrono::milliseconds period,
      timer_options_t options = {});
};

enum class spot_actor_change_kind_t {
    join_spot,
    join_entry_spot,
    leave_spot
};

struct spot_actor_change_result_t {
    spot_actor_change_kind_t kind;
};

struct spot_actor_message_metadata_t {
    std::map<std::string, std::string> values;
};

class spot_actor_reply_options_t {
public:
    spot_actor_reply_options_t &metadata(std::string key, std::string value);
    spot_actor_reply_options_t &compress(bool enabled = true);
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

class spot_handler_registry_t {
public:
    template <typename THandler>
    spot_handler_registry_t &add_handler(std::string packet_name = {});

    template <typename THandler, typename TSpot, typename TMessage>
    spot_handler_registry_t &add_handler(std::string packet_name = {});

    template <typename THandler>
    spot_handler_registry_t &add_subscribe(std::string topic);

    template <typename THandler, typename TSpot, typename TEvent>
    spot_handler_registry_t &add_subscribe(std::string topic);

    template <typename THandler>
    spot_handler_registry_t &add_actor_join(std::string packet_name = {});

    template <typename THandler, typename TSpot, typename TActor,
      typename TRequest,
      typename TReply>
    spot_handler_registry_t &add_actor_join(std::string packet_name = {});

    template <typename THandler>
    spot_handler_registry_t &add_actor_packet(std::string packet_name = {});

    template <typename THandler, typename TSpot, typename TActor,
      typename TMessage>
    spot_handler_registry_t &add_actor_packet(std::string packet_name = {});

    template <typename THandler>
    spot_handler_registry_t &add_post_actor_joined();

    template <typename THandler, typename TSpot, typename TActor>
    spot_handler_registry_t &add_post_actor_joined();

    template <typename THandler>
    spot_handler_registry_t &add_actor_left();

    template <typename THandler, typename TSpot, typename TActor>
    spot_handler_registry_t &add_actor_left();

    template <typename THandler>
    spot_handler_registry_t &add_actor_disconnected();

    template <typename THandler, typename TSpot, typename TActor>
    spot_handler_registry_t &add_actor_disconnected();

    template <typename TSpot>
    result_t<message_t> invoke_packet(std::string_view packet_name,
      TSpot &spot,
      service_provider_t &services,
      serializer_registry_t &serializers,
      const message_t &message) const;

    template <typename TSpot, typename TActor>
    result_t<message_t> invoke_actor_join(std::string_view packet_name,
      TSpot &spot,
      TActor &actor,
      service_provider_t &services,
      serializer_registry_t &serializers,
      const message_t &message) const;

    template <typename TSpot, typename TActor>
    result_t<message_t> invoke_actor_packet(std::string_view packet_name,
      TSpot &spot,
      TActor &actor,
      service_provider_t &services,
      serializer_registry_t &serializers,
      const message_t &message) const;

    template <typename TSpot, typename TActor>
    result_t<message_t> invoke_post_actor_joined(TSpot &spot,
      TActor &actor,
      service_provider_t &services,
      serializer_registry_t &serializers,
      spot_actor_change_result_t result = {});

    template <typename TSpot, typename TActor>
    result_t<message_t> invoke_actor_left(TSpot &spot,
      TActor &actor,
      service_provider_t &services,
      serializer_registry_t &serializers,
      spot_actor_change_result_t result);
};

class bound_session_t {
public:
    template <typename TMessage>
    send_call_t send(const TMessage &message);

    send_call_t disconnect();
};

class actor_context_t {
public:
    const actor_ref_t &actor_ref() const;
    bool is_joined() const;
    bound_session_t bound_session() const;

    template <typename TRequest, typename TReply>
    actor_join_spot_call_t<TReply> join_spot(
      spot_rid_t spot_rid,
      const TRequest &request);

    actor_join_entry_spot_call_t join_entry_spot(node_rid_t spot_node_rid);
};

} // namespace zlink::framework
```

`spot_context_t::publish(...)`는 현재 spot channel 안의 topic publish를 뜻하므로
별도 channel name을 받지 않는다. 직접 `routing_id_t`를 다루는 API는 spot-to-spot
경로와 Entry Spot join 경로에 제한한다. 일반 application handler와 client는 channel
name과 topic을 먼저 사용한다.

`.NET`의 `Context.Handlers.AddHandler`, `AddActorJoin`, `AddActorPacket`과 같은 역할은
C++에서 `spot_context_t::handlers()`가 맡는다. C++에는 assembly reflection이 없으므로
handler type은 명시한다. spot, actor, message, reply type은 handler class의
`spot_type`, `actor_type`, `request_type`, `reply_type` alias에 한 번만 선언하고,
registry는 `add_actor_join<handler_t>()`처럼 handler type만 받는 overload를 기본으로 쓴다.
긴 template 인자를 모두 나열하는 overload는 낮은 수준 확장이나 테스트용 escape hatch다.
샘플과 guide 예제에는 노출하지 않는다.

```cpp
class bingo_room_join_handler_t {
public:
    using spot_type = bingo_room_spot_t;
    using actor_type = player_actor_t;
    using request_type = bingo_room_join_req_t;
    using reply_type = bingo_room_join_res_t;

    bingo_room_join_res_t handle(bingo_room_t &spot,
      const player_actor_t &actor,
      const bingo_room_join_req_t &request);
};

void configure(zlink::framework::spot_context_t &context)
{
    context.handlers()
      .add_actor_join<bingo_room_join_handler_t>()
      .add_actor_packet<start_bingo_game_handler_t>()
      .add_post_actor_joined<bingo_room_actor_joined_handler_t>()
      .add_actor_left<bingo_room_actor_left_handler_t>();
}
```

일반 Spot packet handler와 subscription handler는 `handle(spot, message)` 형태로 호출된다.
actor lifecycle handler는 `spot_actor_change_result_t`를 받는다. 이 타입은 `.NET`의
`ZLinkSpotActorChangeResult`에 해당하며, `join_spot`, `join_entry_spot`, `leave_spot`
change kind를 표현한다. actor disconnected handler는 `.NET`처럼 change result 없이
`handle(spot, actor)` 형태로 호출된다. C++은 handler interface에서 `TSpot`을 reflection으로
추론할 수 없으므로 handler class alias로 타입 정보를 제공한다.
등록된 handler는 descriptor로만 남지 않는다. dispatch 경로는 `serializer_registry_t`로
`message_t`를 DTO로 바꾸고, `service_provider_t`에서 handler owner를 resolve한 뒤
typed `handle(...)`을 호출한다. 샘플도 이 경로를 통과해야 framework 동작을 확인했다고
볼 수 있다.
Entry Spot의 actor packet도 일반 Spot packet으로 등록하지 않는다. `add_actor_packet`으로
등록하고 handler는 `EntrySpot`, actor, `spot_actor_request_context_t` 또는
`spot_actor_send_context_t`, DTO를 받는다. 이렇게 해야 `.NET` sample의 actor request
handler와 같은 구조가 된다.

timer는 native timer handle을 application에 넘기지 않는다. `timer_t`는 CAPI timer
등록의 lifetime과 취소를 표현하는 public handle이며, callback은 user Spot에서는 core
SPOT dispatch boundary를 따르고 Entry Spot에서는 Entry Spot 전체를 전역 직렬화하지
않는다.

timer 구현은 CAPI timer를 기반으로 한다. CAPI timer의 `fire_count`는 framework가
`delivery_index`, `scheduled_index`, `skipped_ticks`, overrun policy를 계산하는 입력이다.
`timer_tick_t`는 native timer event를 그대로 노출한 구조체가 아니라, framework가
`.NET` timer와 같은 의미로 재구성한 dispatch metadata다.

ActorGateway session relay는 `session_actor_manager_t`, `session_actor_t`,
`actor_context_t`, `bound_session_t`가 담당한다. 이 표면은 route mesh channel을 직접
보여 주지 않는다. runtime이 ActorGateway 내부 frame, actor ref, bound session metadata를
소유한다.
actor context의 `join_spot<TRequest, TReply>(...)` 결과는
`actor_join_result_t<TReply>`다. 이 타입은 `.NET`의 `ZLinkActorJoinResult<TReply>`처럼
join result code, join 이후 actor ref, typed reply를 함께 담는다. Entry Spot join은
`.NET`과 같이 actor ref만 돌려준다.

비동기 표면은 `.NET`의 `SubmitAsync()`와 callback submit 모델을 C++로 투영한다.
`request(...)`, `send(...)`, `relay(...)`, `join_spot(...)`, `join_entry_spot(...)` 같은
호출은 즉시 실행하지 않는 call object를 반환하고, 마지막 `submit()`이 실제 submit 지점이다.

```cpp
auto reply = co_await client
  .request<profile_reply_t>("profile", query)
  .timeout(std::chrono::seconds(2))
  .submit();

client
  .request<profile_reply_t>("profile", query)
  .timeout(std::chrono::seconds(2))
  .submit([](zlink::framework::result_t<profile_reply_t> result) {
      if (!result) {
          return;
      }

      use_profile(result.value());
  });
```

public framework async 표면에 `std::future`를 사용하지 않는다. blocking wait는 handler,
timer, STREAM session callback, actor relay 경로에서 허용하지 않는다.

오류 종류는 `.NET` framework의 `ZLinkFrameworkErrorKind`를 C++ naming으로 투영한다.
callback submit은 `result_t<T>` 안에 `framework_error_kind_t`와 message를 담고,
coroutine submit은 같은 정보를 가진 `framework_exception_t`를 throw한다.

## 12. Hosted Service 와 Module

hosted service는 app lifecycle에 묶이는 background worker다.

```cpp
namespace zlink::framework {

class hosted_service_t {
public:
    virtual ~hosted_service_t() = default;
    virtual void start(service_provider_t &services) = 0;
    virtual void stop() = 0;
};

class module_t {
public:
    virtual ~module_t() = default;
    virtual void configure_services(service_collection_t &services) {}
    virtual void configure_zlink(zlink_builder_t &zlink) {}
    virtual void configure_handlers(handler_registry_t &handlers) {}
    virtual void configure_monitoring(monitoring_builder_t &monitoring) {}
};

template <typename TModule>
concept framework_module_contract_t =
  requires(TModule &module,
    service_collection_t &services,
    zlink_builder_t &zlink,
    handler_registry_t &handlers,
    monitoring_builder_t &monitoring) {
      module.configure_services(services);
      module.configure_zlink(zlink);
      module.configure_handlers(handlers);
      module.configure_monitoring(monitoring);
  };

} // namespace zlink::framework
```

module은 서비스 등록, runtime 구성, handler 등록, monitoring 구성을 한 기능 단위로 묶는
낮은 수준 확장 단위다. 일반 애플리케이션 설정의 주 표면은 module type이 아니라
`app_t::add_zlink_framework(options_callback)`이다.

`app_t::add_zlink_framework(options_callback)`는 `.NET`의
`AddZLinkFramework(options => ...)`에 대응하는 C++ 고수준 구성 진입점이다. C++에는 assembly
reflection이 없으므로 `.NET`의 `AddHandlersFromAssemblyOf(...)`만 그대로 옮기지 않는다.
그 대신 handler 타입을 명시해서 `options.handlers().add<THandler>(group_name)`으로 등록한다.
나머지 codec, discovery, client-server channel, handler group 구성은 `.NET`과 같은 읽기 수준을
유지한다.

`options.codecs().add_json()`은 JSON codec 사용만 선언한다. 사용자가 모든 request/reply message
type을 codec 설정에 나열하지 않는다. C++ framework는 `options.handlers().add<THandler>(...)`에서
handler의 `request_type`, `reply_type`을 읽어 필요한 JSON serializer를 내부에서 등록한다.
handler에 생성자 의존성이 있으면 `using dependency_types =
zlink::framework::dependency_list_t<dep1_t, dep2_t>;`처럼 의존 타입을 명시한다. framework는
handler를 등록할 때 `add_singleton<THandler, dep1_t, dep2_t>()`와 같은 DI 생성자 주입 등록을
사용한다.

```cpp
app.add_zlink_framework ([&](zlink::framework::zlink_framework_options_t &options) {
    options.handlers()
      .add<authenticate_player_handler_t>("api")
      .add<match_bingo_api_handler_t>("api");

    options.codecs().add_json();

    options.discovery().add(topology.registry_router_endpoint);

    options.client_server_channel(sample_names_t::api_channel)
      .server(topology.api_channel_endpoint)
      .handler_group("api");

    options.client_server_channel(sample_names_t::play_channel)
      .client();
});
```

이 구조에서는 샘플 `main.cpp`, role `*HostFactory`, 일반 사용자 설정 예제가 handler member
function pointer, handler용 DI factory lambda, monitoring channel 문자열, serializer smoke 검증,
message type을 모두 나열하는 codec 등록 같은 세부 구현을 직접 알 필요가 없다. 그런 내용이 보이면
framework options builder가 아직 충분히 깊지 않은 것으로 본다.

`zlink_framework_options_t`의 사용자 표면은 fluent options builder로 제한한다. 람다 기반
`add_client_server_channel(...)`, `enable_server(...)`, `enable_client(...)` 같은 우회 API는
일반 사용자 설정에 두지 않는다. C++ 내부 runtime builder에는 낮은 수준 API가 남아 있을 수 있지만,
샘플과 guide 수준의 설정은 아래처럼 역할이 바로 보이는 형태를 사용한다.

```cpp
options.client_server_channel(sample_names_t::api_channel)
  .server(topology.api_endpoint)
  .handler_group("api");

options.publisher_channel(sample_names_t::notification_channel)
  .bind(topology.notification_endpoint);

options.use_registry_spot_remote_addresses(sample_names_t::router_channel);

options.route_mesh_channel(sample_names_t::router_channel)
  .bind(topology.session_spot_endpoint)
  .routing_id(topology.session_router_rid)
  .connect(topology.play_router_endpoint);

options.spot_mesh(sample_names_t::game_spot_discovery)
  .node(sample_names_t::session_spot_node)
  .enable_router(topology.session_router_endpoint, topology.session_router_rid)
  .enable_pub_sub(topology.session_spot_endpoint, topology.session_pub_rid)
  .accept_routes_from_channel(sample_names_t::router_channel);

options.stream_node(sample_names_t::stream_name)
  .bind(topology.stream_endpoint)
  .packet_session("client-session")
  .attach_actor_gateway(sample_names_t::spot_node);
```

handler 안에서 다른 channel로 request를 보낼 때도 호출자는 낮은 수준의 request/reply template
쌍이나 blocking wait를 보지 않아야 한다. `.NET`의 `await client.RequestAsync<TReply>(...)`와
같은 읽기 수준을 C++에서는 아래처럼 표현한다.

샘플 namespace에서는 `using zlink::framework::task_t;`를 두고 `task_t<T>`처럼 짧게 쓴다.
`zlink::framework::task_t<T>`를 handler signature마다 반복하면 async 의미보다 namespace
노이즈가 먼저 보이기 때문이다. framework public contract 문서에서는 전체 이름을 쓸 수 있지만,
application sample과 guide 예제는 짧은 alias를 기본으로 한다.

```cpp
task_t<match_bingo_api_res_t> handle(const match_bingo_api_req_t &request)
{
    allocate_bingo_room_res_t allocated = co_await _client
      .request<allocate_bingo_room_res_t>(
        sample_names_t::play_channel,
        allocate_bingo_room_req_t { request.mode })
      .submit();

    co_return match_bingo_api_res_t { allocated.room_id };
}
```

샘플 handler는 `.submit().result().value()`로 결과를 직접 꺼내지 않는다. 그런 코드는 handler가
runtime 안에서 blocking wait를 수행하는 것처럼 보이고, 모든 언어 버전에서 같은 async 모델을
제공한다는 목표와 맞지 않다.

```cpp
class order_module_t final : public zlink::framework::module_t {
public:
    void configure_services(
      zlink::framework::service_collection_t &services) override
    {
        services.add_singleton<order_repository_t>();
        services.add_factory<order_service_t>([](auto &sp) {
            return std::make_unique<order_service_t>(
              sp.get_required<order_repository_t>());
        });
        services.add_transient<order_handler_t>();
    }

    void configure_handlers(
      zlink::framework::handler_registry_t &handlers) override
    {
        handlers.subscribe<order_created_t, order_handler_t>(
          "orders",
          "orders.created",
          &order_handler_t::on_created);
    }
};
```

## 13. Configuration 과 Logging

configuration은 JSON, environment variables, CLI args를 core 표면으로 둔다.

```cpp
namespace zlink::framework {

class config_builder_t {
public:
    config_builder_t &load_json(std::string path);
    config_builder_t &load_env(std::string prefix);
    config_builder_t &load_cli(int argc, char **argv);
};

class logging_builder_t {
public:
    logging_builder_t &use_console();
    logging_builder_t &set_level(std::string level);
};

class metrics_builder_t {
public:
    metrics_builder_t &add_runtime_metrics();
};

class health_builder_t {
public:
    health_builder_t &add_zlink_runtime_check();
};

} // namespace zlink::framework
```

JSON loader는 `nlohmann/json`을 사용한다. YAML은 필요하면 configuration extension으로
둔다. metrics와 health 표면은 core 관찰 기능으로 둔다. exporter, label schema,
tracing hook은 별도 observability 초안에서 확정한다.

## 14. 전체 샘플

아래 샘플은 framework 사용자가 기대하는 최종 표면이다.

```cpp
#include <zlink/framework.hpp>

struct order_created_t {
    std::string order_id;
};

class order_repository_t {
public:
    void save_created(const order_created_t &event);
};

class order_handler_t final {
public:
    explicit order_handler_t(order_repository_t &repository)
      : repository_(repository)
    {
    }

    void on_created(const order_created_t &event)
    {
        repository_.save_created(event);
    }

private:
    order_repository_t &repository_;
};

int main(int argc, char **argv)
{
    auto app = zlink::framework::app_t::create();

    app.config()
      .load_json("appsettings.json")
      .load_env("ZLINK_")
      .load_cli(argc, argv);

    app.services()
      .add_singleton<order_repository_t>()
      .add_factory<order_handler_t>([](auto &services) {
          return std::make_unique<order_handler_t>(
            services.template get_required<order_repository_t>());
      });

    app.use_zlink([](auto &zlink) {
        zlink.node("order-node")
          .channel("orders", [](auto &channel) {
              channel.enable_server([](auto &server) {
                  server.bind("tcp://0.0.0.0:7001");
              });
              channel.enable_subscriber([](auto &subscriber) {
                  subscriber.use_discovery();
              });
          })
          .spot_node("orders-spot", [](auto &spot_node) {
              spot_node.bind("tcp://0.0.0.0:7101");
              spot_node.use_discovery("orders");
          });
    });

    app.handlers()
      .subscribe<order_created_t, order_handler_t>(
        "orders",
        "orders.created",
        &order_handler_t::on_created);

    return app.run(argc, argv);
}
```

이 샘플에서 사용자는 `zlink::router_socket_t`, `zlink::dealer_socket_t`,
`zlink::service::spot_node_t`를 직접 만들지 않는다. framework host가 C++ binding
타입을 생성하고 lifecycle을 관리한다.

## 15. 기존 C++ 세부 초안 정렬 항목

이 문서를 기준으로 기존 `C++` 세부 초안은 아래 방향으로 정리해야 한다.

- 이전 bootstrap 표기는 `app_t::create()`로 맞춘다.
- 이전 raw handler registration 중심 샘플은 `app.handlers()` 표면으로 맞춘다.
- raw `request_handler_t`, `send_handler_t`, `event_handler_t` 중심 표면은 고급 raw
  handler extension으로 내리고, 일반 샘플은 typed handler registry를 사용한다.
- 이전 channel client와 event publisher 문서는 `message_bus_t`,
  `request_client_t`, `publisher_t` 주입 표면과 맞춘다.
- handler와 publisher 표면은 channel name을 먼저 받고, topic 또는 packet name을
  그 다음에 받는 형태로 맞춘다.
- channel 연결 설정은 channel 전체가 아니라 capability builder에 둔다.
- SPOT 문서는 binding의 `service::spot_node_t`, `service::spot_t` 기능을 framework
  builder와 `spot_context_t`로 감싸는 방식으로 정리한다.
- SPOT discovery 설정은 `spot_node.use_discovery(channel_name)`처럼 active SPOT
  channel view 이름을 명시하는 형태로 맞춘다.
- STREAM session actor relay는 `stream.attach_actor_gateway(spot_node_name)`과
  `session_actor_t::relay(...)` 표면으로 맞춘다.
- SPOT timer는 CAPI timer `fire_count`를 기반으로 `timer_options_t`, `timer_tick_t`,
  overrun policy, timer failure monitoring을 포함하는 표면으로 맞춘다.
