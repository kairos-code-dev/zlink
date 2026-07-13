<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ Channel Messaging](01-system-structure.ko.md) | [다음: C++ Runtime Architecture](../../../../cpp/internals/runtime-architecture.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[C++ 묶음](../../../../cpp/README.ko.md) | [Runtime Architecture](../../../../cpp/internals/runtime-architecture.ko.md) | [Application Framework](01-system-structure.ko.md) | [channel](01-system-structure.ko.md) | [SPOT](02-framework-interfaces.ko.md) | [STREAM](02-framework-interfaces.ko.md) | [HTTP Client](../../../../../http-client/cpp/README.ko.md) | [HTTP Hosting](60-http-hosting.ko.md)

# Spec -- ZLink Framework C++ Interface Design

> 이 문서는 C++ framework가 제공해야 하는 정식 public interface 계약이다.
> 현재 public header와 다른 항목은 구현 차이로 관리한다.
> 이 문서는 `framework/doc/spec` 아래 공통 framework 정책을 상위 기준으로 따르고,
> C++ binding의 public 라이브러리 표면을 기반으로 framework 계층을 설계한다.

## 1. 설계 기준

`C++` framework는 기존 C++ binding을 대체하지 않는다. framework는 C++ binding 위에
올라가며, binding이 제공하는 typed public API를 내부 runtime substrate로 사용한다.

기능과 사용성 개념은 framework 공통 스펙을 기준으로 맞춘다. 즉 app/host, DI scope,
handler registry, channel messaging, `STREAM`, `SPOT`, ActorGateway session relay,
monitoring, graceful shutdown은 같은 모델을 제공하고, C++ public API는 C++20 coroutine,
callback, RAII ownership에 맞게 표현만 바꾼다.

binding 기준은 아래 문서를 따른다.

- [C++ Binding Specification](../../../../../../../bindings/doc/spec/cpp/README.md)
- [C++ Codec Extension Specification](../../../../../../../bindings/doc/spec/cpp/codec.md)

framework public API는 `zlink::framework` namespace 아래에 둔다. public contract와
runtime 구현의 책임을 분리한다. C++에서 contract는
설치되는 public header이고, runtime은 `src/runtime/*` 안의 구현이다. binding의 public
타입은 framework 내부 구현과 일부 고급 extension point에서 사용할 수 있지만, 일반
사용자는 raw socket이나 poller를 직접 만지지 않아도 앱을 만들 수 있어야 한다.

## 2. Binding 대응표

framework 구현은 아래 C++ binding 타입을 기준으로 삼는다.

| Framework 개념 | Binding 기준 타입 | Framework에서의 역할 |
|----------------|------------------|----------------------|
| runtime context | `zlink::context_t` | app lifecycle 안에서 생성하고 종료한다. |
| message buffer | `zlink::message_t`, `zlink::multipart_t` | serializer가 typed payload를 변환하는 내부 메시지 단위다. |
| request/reply channel | `zlink::router_socket_t`, `zlink::dealer_socket_t` | channel server/client 역할 구현에 사용한다. |
| pub/sub channel | `zlink::pub_socket_t`, `zlink::sub_socket_t` | topic publish/subscribe 역할 구현에 사용한다. |
| stream ingress | `zlink::stream_socket_t` | STREAM packet/session 역할 구현에 사용한다. |
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
`Contracts/*`에 대응하는 실제 public contract owner이고, `zlink/framework.hpp`는
사용자가 전체 framework 표면을 한 번에 include할 수 있는 facade다. 한 줄짜리
`zlink/framework/*.hpp` compatibility wrapper는 유지하지 않는다.

```text
zlink/framework.hpp
zlink/framework/version.hpp
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
| 공통 기능을 사용자에게 제공하는가? | 같은 기능 축의 public 계약이면 C++ contract로 둔다. | runtime 실행에만 필요한 타입이면 숨긴다. |
| native 실행 순서를 드러내는가? | 드러내지 않으면 facade로 둘 수 있다. | poll/recv/drain 순서가 보이면 숨긴다. |

### 3.1 기능별 Contract/Runtime Owner

인터페이스 분리는 파일 이름만 맞추는 작업이 아니다. 기능을 구현하기 전에 어느 타입이
사용자 계약이고 어느 타입이 runtime 구현인지 먼저 닫아야 한다. 아래 표는 `.NET`
framework의 `Contracts/*`와 `Runtime/*` 구조를 C++ framework에 옮길 때의 기준이다.

| 기능 축 | C++ public contract owner | C++ runtime implementation owner | public에 두지 않는 것 |
|---------|---------------------------|----------------------------------|-----------------------|
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

`src/runtime/channels/channel_pending_requests.*`는 channel request의 pending table을
담는다. request sequence 발급, pending 등록, reply completion, drain은 이 모듈이 맡고
`message_bus_t`나 public call object는 pending table을 직접 알지 않는다.

`src/runtime/channels/channel_reply_writer.*`는 `.NET`의 `ZLinkChannelReplyWriter`에
대응한다. request envelope에서 correlation id와 message name을 보존한 response/error
header를 만들고, error code는 stable string으로 기록한다.

`src/runtime/channels/channel_packet_dispatcher.*`는 `.NET`의
`ZLinkChannelPacketDispatcher`에 대응한다. server ingress envelope를 해석하고 request는
handler result를 response envelope로 감싸며, command/send는 reply 없이 dispatch한다.

`src/runtime/channels/channel_bundle_factory.*`는 `.NET`의
`ZLinkChannelBundleFactory`에 대응한다. channel 역할 snapshot에서 client, server,
publisher, subscriber runtime bundle을 만들고 manual endpoint attachment를 bundle 내부로
옮긴다.

`src/runtime/channels/channel_runtime_manager.*`는 `.NET`의
`ZLinkChannelRuntimeManager`에 대응한다. 역할 bundle lazy creation, inbound/client/
publisher 초기화, route channel lookup, monitoring source parsing을 담당한다.

`src/runtime/channels/channel_runtime_bundle.*`는 `.NET`의
`ZLinkChannelRuntimeBundle`에 대응한다. manual connection set, receive gate,
channel pending request owner를 역할 내부 상태로 묶고 public contract에는
노출하지 않는다.

server ingress dispatch는 channel host service가 수신한 envelope parts를
`src/runtime/channels/channel_packet_dispatcher.*`로 바로 넘긴다. C++ runtime은 별도
message pump 타입을 production 모듈로 두지 않고, receive gate와 connection 상태를
`src/runtime/channels/channel_runtime_bundle.*` 안에 둔다.

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

public `route_client_t`, `route_send_call_t`, `route_request_call_t`는 `.NET`의
`IZLinkRouteClient`와 `ZLinkRouteClient`에
대응한다. 사용자는 router channel id, target node routing id, typed payload만 넘기고,
route channel runtime lookup, envelope 작성, serializer 호출은 runtime owner가 처리한다.
C++는 낮은 수준 검증을 위해 request sequence submission call도 유지하지만, 일반 사용 표면은
`request(...).metadata(...).timeout(...).async<TReply>()`으로
typed reply를 받는다. `.metadata(key, value)`로 넣은 값은 framework envelope header에 보존되며,
route runtime lookup과 serializer 호출은 사용자에게 드러나지 않는다. typed reply completion은
route runtime backend seam을 통해 검증되고,
`native_route_backend_t`가 C++ binding `router_socket_t::send/request`로 이 seam에 붙는다.
현재 완료 범위에서는 route runtime lookup, envelope 작성, backend seam, Registry 기반
route lookup을 회귀 테스트로 고정한다. router socket lifecycle을 더 자동화해야 하는 경우에도
public route client 표면을 늘리지 않고 이 backend seam 아래에서 처리한다.

`src/runtime/channels/route_packet_dispatcher.*`는 `.NET`의 `ZLinkRoutePacketDispatcher`에
대응한다. route channel host service가 routed packet 수신과 worker 실행을 맡고,
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

`pending_operation_t`는 runtime 내부 pending queue와 cancellation 상태를 추적하는 핸들이다.
서버 framework public call object는 callback submit을 노출하지 않으므로 일반 사용자는 이
타입을 직접 다루지 않는다. 상태 저장소, 실패 예외, queue slot은 public header에 두지 않고
`src/runtime/messaging`의 private state에 둔다.

`src/runtime/messaging/pending_submit.*`는 command submit의 accepted 완료, request
submit의 별도 응답 완료, deadline 만료, wake callback을
내부 상태로 유지한다. public cancellation token은 `contracts/cancellation.hpp`가 담당하며,
pending queue slot이나 wake callback 구조를 사용자가 직접 다루지 않게 한다.

`src/runtime/messaging/submit_queue.*`는 bounded FIFO이며 capacity 초과와 disposed
상태를 내부에서 막는다. public channel call
object는 큐 구현을 알 필요가 없고 `pending_operation_t`만 받는다.

`src/runtime/messaging/envelope_codec.*`는 header/body 2-part envelope,
`application/json` content type, error envelope header,
body part 누락 검사는 이 모듈이 맡는다. 사용자는 envelope JSON 구조를 직접 만들지 않는다.

`src/runtime/messaging/client_call_codec.*`는 request/command/publish header 생성,
correlation id, deadline 문자열, typed body encode,
reply body decode, error reply 해석을 한 곳에 둔다.

`src/runtime/messaging/request_failure_mapper.*`는 native request result나 error
envelope code를 `framework_error_kind_t`와
retriable 여부로 사상한다. 이 매핑은 handler, channel, connector sample에 흩어져 있으면
안 된다.

주요 매핑은 아래처럼 고정한다.

| native/error code | C++ error kind | retriable |
|-------------------|----------------|-----------|
| `timed_out`, `timeout` | 경계 timeout — public enum 값이 아니라 `framework_exception_t`의 `code() == std::errc::timed_out` 파셋(§8.1) | no |
| `not_connected`, `route_not_connected` | `route_not_connected` | yes |
| `not_found`, `request_target_not_found` | `request_target_not_found` | no |
| `rejected`, `request_rejected` | `request_rejected` | no |
| `busy`, `conflict` | `request_rejected` | yes |
| `protocol_error`, `request_protocol_error` | `request_protocol_error` | no |
| `handler_not_found` | `handler_not_found` | no |

이 표는 request completion과 error envelope reply 양쪽에 같은 의미로 적용한다.

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
class spot_publisher_client_t;
class spot_context_t;
class stream_dispatch_context_t;
class stream_error_t;
class stream_t;
class packet_stream_session_t;
class module_t;
class hosted_service_t;

} // namespace zlink::framework
```

## 4. App / Host

`app_t`는 framework의 가장 바깥 public type이다. 사용자는 `app_t::create()`로 앱을
만들고, `add_zlink_framework(...)`에서 services, handlers, zlink runtime을 한 번에
구성한 뒤 `run`을 호출한다. 낮은 수준의 runtime builder는 일반 애플리케이션 표면에
직접 노출하지 않는다.

```cpp
namespace zlink::framework {

enum class drain_force_reason_t {
    deadline_exceeded,
    draining_state_publish_failed,
    owner_cleanup_failed,
    teardown_failed
};
struct drained_t {};
struct force_stopped_t { drain_force_reason_t reason; };
using drain_result_t = std::variant<drained_t, force_stopped_t>;

class app_t {
public:
    static app_t create();

    config_builder_t &config();
    logging_builder_t &logging();
    monitoring_builder_t &monitoring();
    app_advanced_t advanced();

    app_t &add_module(module_t &module);
    app_t &add_zlink_framework(
      std::function<void(zlink_framework_options_t &)> configure);
    template <typename TModule, typename... TArgs>
    app_t &add_zlink_framework(TArgs &&...args);
    app_t &add_hosted_service(std::unique_ptr<hosted_service_t> service);

    task_t<drain_result_t> drain(std::chrono::milliseconds deadline);
    task_t<drain_result_t> drain();
    task_t<drain_result_t> await_drained();
    bool is_ready() const;

    int run(int argc, char **argv);
    void stop();
    void request_stop();
};

class app_advanced_t {
public:
    service_collection_t &services();
    handler_registry_t &handlers();
    zlink_builder_t &zlink() noexcept;
};

} // namespace zlink::framework
```

`app_advanced_t`는 framework extension, contract test, 상위 options로 승격하지 않은
낮은 수준 기능을 위한 탈출구다. Bingo, TicTacToe 같은 일반 샘플은 이 표면을 사용하지
않고 `add_zlink_framework(...)`만 사용해야 한다.

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
- `add_scoped<T>()`는 framework가 소유하는 scope 안에서만 resolve한다.
- 복잡한 외부 객체 생성이나 조건부 생성이 필요한 경우에만 `add_factory<T>()`를 사용한다.
- handler owner는 service collection에 등록되어 있어야 한다.
- 등록되지 않은 handler owner를 framework가 암묵적으로 생성하지 않는다.
- `Boost.Ext.DI` 같은 외부 DI 라이브러리는 public dependency로 두지 않는다.

`scoped` lifetime은 zlink core 기능이 아니라 framework가 소유하는 DI lifetime이다. `.NET`
framework가 `IServiceScope`를 만들어 handler dispatch, STREAM session, Spot activation
수명에 붙이는 것처럼, C++ framework도 자체 DI container에서 같은 scope 경계를 만든다.
channel handler는 dispatch마다 scope를 만들고, STREAM session은 session scope를 가지며,
Spot과 Entry Spot은 activation scope를 가진다. actor factory는 actor creation scope에서
resolve하고, actor instance 자체는 actor runtime이 소유한다.

예시는 아래와 같다.

```cpp
options.services()
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
    zlink_builder_t &add_node(std::string node_name);
    zlink_builder_t &max_pending(std::size_t count);
    registry_builder_t enable_registry();
    discovery_builder_t discovery();
    route_channel_builder_t route_channel(std::string route_channel_name);
    channel_builder_t channel(std::string channel_name);
    spot_node_builder_t add_spot_node(std::string spot_node_name);
    stream_builder_t stream(std::string stream_name);
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
    stream_builder_t &register_session(std::string session_name);
};

class stream_node_options_builder_t {
public:
    stream_node_options_builder_t &bind(std::string endpoint);

    template<typename TSession>
    stream_node_options_builder_t &register_session();

    stream_node_options_builder_t &register_session(std::string session_name);
      std::string spot_node_name);
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

channel은 framework에서 request/reply와 pub/sub 역할을 묶는 이름이다.

```cpp
namespace zlink::framework {

class channel_builder_t {
public:
    capability_builder_t enable_server();
    capability_builder_t enable_client();
    capability_builder_t enable_publisher();
    capability_builder_t enable_subscriber();
};

class capability_builder_t {
public:
    capability_builder_t &bind(std::string endpoint);
    capability_builder_t &connect(std::string endpoint);
    capability_builder_t &set_routing_id(zlink::routing_id_t routing_id);
    };

} // namespace zlink::framework
```

요청 timeout은 call object의 `.timeout(...)`과 route request fluent 표면에서 설정한다. pending
queue 상한은 `zlink_builder_t::max_pending(...)`이 runtime 단위로 소유한다. C++ draft는
`.NET` 역할 builder에 없는 per-역할 timeout/pending option을 만들지 않는다.

내부 매핑은 아래와 같다.

| Capability | Binding 구현 기준 |
|------------|------------------|
| server | `zlink::router_socket_t` |
| client | `zlink::dealer_socket_t` |
| publisher | `zlink::pub_socket_t` |
| subscriber | `zlink::sub_socket_t` |

같은 channel 안에서도 역할별 연결 집합은 분리한다. 예를 들어
`orders.client`와 `orders.subscriber`는 같은 channel 이름을 공유하지만 서로 다른
socket과 연결 정책을 가진다.

따라서 `bind`, `connect`, 인자 없는 `enable_client`/`enable_subscriber` 같은 연결 설정은 channel 전체가 아니라
`server`, `client`, `publisher`, `subscriber` 역할 builder에 둔다.

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
    handler_execution_t execution = handler_execution_t::inline_on_runtime;
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
class stream_write_call_t;
template <typename TReply>
class actor_join_spot_call_t;
class actor_join_entry_spot_call_t;

template <typename T>
class task_t;

template <typename T>
class result_t;

class endpoint_connections_t {
public:
    void connect(std::string endpoint);
    void disconnect(std::string endpoint);
    std::vector<std::string> list_connections() const;
};

class actor_ref_t {
public:
    actor_ref_t(node_rid_t node_rid,
      std::string actor_type,
      std::string actor_id,
      std::uint64_t generation = 1);

    node_rid_t node_rid() const;
    std::string_view actor_type() const;
    std::string_view actor_id() const;
    std::uint64_t generation() const;
    bool empty() const;
};

template <typename TReply>
struct actor_join_accepted_t {
    actor_ref_t actor;
    TReply reply;
};

template <typename TReply>
struct actor_join_rejected_t {
    TReply reply;
};

template <typename TReply>
using typed_actor_join_result_t =
  std::variant<actor_join_accepted_t<TReply>, actor_join_rejected_t<TReply>>;

using actor_join_result_t = typed_actor_join_result_t<message_t>;

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
    request_failed
};

class framework_exception_t : public std::exception {
public:
    framework_error_kind_t kind() const noexcept;
    bool is_retriable() const noexcept;
};

template <typename TReply>
class request_call_t {
public:
    request_call_t &timeout(std::chrono::milliseconds timeout);
    request_call_t &metadata(std::string key, std::string value);
    task_t<TReply> async();
};

class send_call_t {
public:
    send_call_t &timeout(std::chrono::milliseconds timeout);
    send_call_t &metadata(std::string key, std::string value);
    void submit();
};

class stream_write_call_t {
public:
    stream_write_call_t &metadata(std::string key, std::string value);
    stream_write_call_t &packet_name(std::string packet_name);
    stream_write_call_t &compress();
    void submit();
};

template <typename TActor>
class bind_actor_call_t {
public:
    bind_actor_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<TActor> async();
};

enum class stream_codec_t : std::uint8_t {
    raw = 0,
    json = 1,
    message_pack = 2,
    protobuf = 3
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

class stream_dispatch_context_t {
public:
    std::string_view packet_name() const;
    const stream_metadata_t &metadata() const;
    bool can_reply() const;
};

class stream_t {
public:
    virtual ~stream_t() = default;
    virtual std::string session_id() const = 0;
    virtual task_t<void> close() = 0;
    virtual stream_write_call_t write_packet(zlink::message_t payload) = 0;
    virtual stream_write_call_t reply_packet(zlink::message_t payload) = 0;
};

class session_actor_t {
public:
    task_t<void> relay(const zlink::message_t &payload);
    task_t<void> notify_disconnected();
};

class session_actor_manager_t {
public:
    result_t<session_actor_t> create(
        std::string actor_type,
        std::string actor_id);
    result_t<session_actor_t> create(
        std::string actor_type,
        std::string actor_id,
        message_t create_request);
    std::optional<session_actor_t> find(std::string actor_id) const;
    result_t<session_actor_t> get_or_create(
        std::string actor_type,
        std::string actor_id);
    result_t<session_actor_t> get_or_create(
        std::string actor_type,
        std::string actor_id,
        message_t create_request);
    request_call_t<session_actor_t> bind(actor_ref_t actor);
};

class packet_stream_session_t {
public:
    virtual ~packet_stream_session_t() = default;
    virtual task_t<void> on_connected(stream_t &stream) = 0;
    virtual task_t<void> on_disconnected(stream_t &stream) = 0;
    virtual task_t<void> on_error(stream_t &stream, const stream_error_t &error) = 0;
    virtual task_t<void> on_packet(
      stream_t &stream,
      const stream_dispatch_context_t &dispatch,
      const zlink::message_t &payload);
};

struct handler_invocation_context_t {
    handler_descriptor_t descriptor;
    handler_context_t context;
    std::shared_ptr<const zlink::message_t> message;
};

struct handler_context_t {
    std::string channel_name;
    std::string packet_name;
    std::string content_type;
};

struct request_context_t : handler_context_t {};
struct send_context_t : handler_context_t {};

struct publish_context_t : handler_context_t {
    std::string topic;
    std::string source;
};

using handler_next_t = std::function<task_t<zlink::message_t>()>;

class handler_registry_t {
public:
    template <typename TOwner, typename TEvent>
    handler_registry_t &on_event(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TEvent &),
      handler_options_t options = {});

    template <typename TOwner, typename TEvent>
    handler_registry_t &on_event(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TEvent &, const publish_context_t &),
      handler_options_t options = {});

    template <typename TOwner, typename TRequest, typename TReply>
    handler_registry_t &on_request(
      std::string channel_name,
      std::string topic,
      TReply (TOwner::*method)(const TRequest &),
      handler_options_t options = {});

    template <typename TOwner, typename TRequest, typename TReply>
    handler_registry_t &on_request(
      std::string channel_name,
      std::string topic,
      TReply (TOwner::*method)(const TRequest &, const request_context_t &),
      handler_options_t options = {});

    template <typename TOwner, typename TCommand>
    handler_registry_t &on_send(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TCommand &),
      handler_options_t options = {});

    template <typename TOwner, typename TCommand>
    handler_registry_t &on_send(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TCommand &, const send_context_t &),
      handler_options_t options = {});

    handler_registry_t &send_raw(
      std::string channel_name,
      std::string topic,
      std::string packet_name,
      std::function<result_t<void>(const payload_view_t &)> handler,
      handler_options_t options = {});

    template <typename TFilter>
    handler_registry_t &use_filter();

    handler_registry_t &observe_failures(
      std::function<void(const handler_failure_event_t &)> observer);
};

} // namespace zlink::framework
```

handler owner 타입은 service collection에서 resolve한다. 일반 application은
`add_zlink_framework(...)` 안에서 handler와 service를 함께 등록한다.

```cpp
options.services().add_transient<order_handler_t>();

options.handlers()
  .group ("orders-api")
  .add<order_created_handler_t> ();
```

STREAM application 업무 경로는 header 객체를 직접 받지 않는다. C++ stream session과 actor relay는
`zlink::message_t` payload 하나를 사용하고, reply와 relay에 필요한 header 값은 runtime 내부
dispatch state가 보존한다. 별도 `_raw` 이름의 public API는 두지 않는다.

handler dispatch는 binding의 `zlink::message_t`와 `zlink::multipart_t`를 받은 뒤,
serializer를 통해 typed payload로 변환하고, DI에서 owner를 resolve한 다음 method를
호출한다.

handler method는 payload만 받을 수도 있고, payload 뒤에 typed context를 함께 받을 수도
있다. request handler는 `request_context_t`, send handler는 `send_context_t`, event/publish
handler는 `publish_context_t`를 받는다. context에는 channel, packet 이름, content type처럼
사용자가 정책 판단에 쓰는 값만 둔다. raw multipart header나 dispatch table은 public context로
노출하지 않는다.

handler filter는 `.NET`의 handler filter처럼 handler 호출 앞뒤의 공통 처리를 맡는다.
일반 application 설정에서는 `options.use_filter<TFilter>()`로 등록한다. 낮은 수준 extension이나
unit test가 직접 registry를 다룰 때만 `handlers.use_filter<TFilter>()`를 사용한다. filter 타입은
`invoke(const handler_invocation_context_t &, handler_next_t)`를 제공하며, 계속 처리하려면
`co_await next()`를 호출하고 요청을 가로채야 하면 reply message를 직접 반환한다. descriptor
lookup, serializer 선택, DI resolve 순서, filter chain 저장은 registry 내부 구현으로 숨긴다.

STREAM handler는 일반 request/send/event handler와 분리한다. framework core는 packet
방식만 지원한다. 내부 wire header는 runtime이 만들고 검증하며, raw stream session과 사용자
정의 header framing은 core public 표면에 넣지 않는다.

stream callback은 framework가 packet을 수신하고 header 검증을 마친 뒤 호출한다. 별도
실행기로 넘기는 것이 기본은 아니며, 같은 stream session의 packet/lifecycle callback은
직렬로 처리한다. CPU-bound 또는 blocking 가능성이 있는 stream handler는 offload 실행
정책을 명시한다.

request handler 반환값은 `TReply` 또는 `task_t<TReply>`를 허용한다. `task_t<TReply>`를
반환하는 handler는 `.NET`의 `async Task<TReply>` handler와 같은 의미이며, 내부
request처럼 결과를 기다려야 하는 호출은 `co_await call.async()` 형태로 사용한다.
one-way send/push는 `call.submit()`으로 제출하고 handler 흐름에서 송신 수락 완료나
backpressure 결과를 기다리지 않는다.

handler 실행은 framework runtime의 coroutine executor를 통과한다. 이 executor는 내부적으로
`boost::asio::thread_pool`과 `boost::asio::co_spawn`으로
`boost::asio::awaitable<result_t<T>>`를 실행한다. 그러나 public API에는
`boost::asio::awaitable`, executor, strand 타입을 노출하지 않는다. 사용자 코드는
`task_t<T>`와 `co_await call.async()`만 본다.
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

class spot_publisher_client_t {
public:
    template <typename TEvent>
    send_call_t publish(std::string channel_name, std::string topic,
                        const TEvent &event) const;
};

class request_client_t {
public:
    template <typename TCommand>
    send_call_t send(std::string_view channel_name, const TCommand &command,
      send_options_t options = {});

    template <typename TRequest>
    channel_request_call_t request(std::string_view channel_name,
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

};

class route_send_call_t {
public:
    route_send_call_t &metadata(std::string key, std::string value);
    void submit();
};

class route_request_call_t {
public:
    route_request_call_t &metadata(std::string key, std::string value);
    route_request_call_t &timeout(std::chrono::milliseconds timeout);
    template <typename TReply>
    task_t<TReply> async();
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
- `spot_publisher_client_t` (SpotMesh가 pub/sub 역할을 켠 경우)
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

bindings package는 JSON, MessagePack, Protobuf dependency를 갖지 않는다. JSON은 framework
기본 codec으로 제공하고, Protobuf와 MessagePack은 framework codec extension package가
제공한다. framework, connector, HTTP client가 codec을 바꿔도 handler/client 업무 API는
바뀌지 않는다.

```cmake
target_link_libraries(app PRIVATE zlink::cpp)

# Protobuf가 필요할 때만 추가한다.
target_link_libraries(app PRIVATE zlink::framework_codec_protobuf)
```

```cpp
app.advanced().handlers()
  .send_raw("orders", "orders.raw", [](const zlink::message_t &message) {
      // raw payload path
  });
```

## 11. Spot Framework API

framework spot 표면은 binding의 `zlink::service::spot_node_t`와
`zlink::service::spot_t`를 기반으로 한다.

```cpp
namespace zlink::framework {

class spot_t {
public:
    virtual ~spot_t() = default;
};

class entry_spot_t : public spot_t {
public:
    ~entry_spot_t() override = default;
};

class spot_node_builder_t {
public:
    spot_node_builder_t &bind(std::string endpoint);
    spot_node_builder_t &set_routing_id(zlink::routing_id_t routing_id);
    spot_node_builder_t &enable_router(std::string endpoint);
    spot_node_builder_t &connect_router(std::string endpoint);
    spot_node_builder_t &connect_router(zlink::routing_id_t peer_rid, std::string endpoint);
    spot_node_builder_t &enable_pub_sub(std::string endpoint);
    spot_node_builder_t &connect_pub_sub(std::string endpoint);
    spot_node_builder_t &connect_peer_pub(std::string endpoint);
    spot_node_builder_t &set_spot_route_channel(std::string route_channel_name);
    spot_node_builder_t &set_actor_transfer_forward_window(
        std::chrono::milliseconds window);

    template <typename TEntrySpot>
    spot_node_builder_t &add_entry_spot();

    template <typename TSpot>
    spot_node_builder_t &add_spot(std::string spot_name);

    template <typename TActorFactory>
    spot_node_builder_t &add_actor_factory(std::string actor_type);

    template <typename TActor, typename TAdapter>
    spot_node_builder_t &add_actor_transfer_adapter(std::string actor_type);
};

class spot_common_context_t {
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

    template <typename TResult, typename TWork>
    worker_call_t<TResult> run_worker(TWork work);
};

class spot_context_t : public spot_common_context_t {
public:
    template <typename TActor>
    task_t<actor_ref_t> leave_actor(const actor_ref_t &actor_ref, TActor &actor);
    task_t<bool> close();
};

class entry_spot_context_t : public spot_common_context_t {
public:
    template <typename TActor>
    task_t<void> destroy_actor(TActor &actor);
};

struct spot_actor_join_response_t {
    bool accepted;
    std::optional<zlink::framework::message_t> reply;
};

template <typename TActor>
class actor_transfer_adapter_t {
public:
    virtual ~actor_transfer_adapter_t() = default;

    virtual task_t<zlink::framework::message_t> transfer_out(
      const TActor &actor) = 0;

    virtual task_t<TActor> transfer_in(
      std::string actor_id,
      zlink::framework::message_t state) = 0;
};

enum class spot_create_state_t {
    existing,
    created,
    rejected
};

struct spot_create_response_t {
    bool accepted;
    std::optional<zlink::framework::message_t> reply;
};

struct spot_create_result_t {
    spot_rid_t spot_rid;
    spot_create_state_t state;
    std::optional<zlink::framework::message_t> reply;
    spot_context_t context;
};

struct spot_actor_message_metadata_t {
    std::optional<std::string_view> find(std::string_view key) const;
    bool contains(std::string_view key) const;
    bool empty() const;
    std::map<std::string, std::string> values;
};

class message_metadata_policy_t {
public:
    message_metadata_policy_t &forward(std::string key);
    bool can_forward(std::string_view key) const;
    spot_actor_message_metadata_t project(
      const std::map<std::string, std::string> &metadata) const;
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
    template <auto Method>
    spot_handler_registry_t &add_handler(std::string packet_name = {});

    template <auto Method>
    spot_handler_registry_t &add_subscribe(std::string topic);

    template <auto Method>
    spot_handler_registry_t &add_actor_send(std::string packet_name = {});

    template <auto Method>
    spot_handler_registry_t &add_actor_request(std::string packet_name = {});

    template <typename TSpot>
    result_t<message_t> invoke_packet(std::string_view packet_name,
      TSpot &spot,
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

};

class bound_session_t {
public:
    template <typename TMessage>
    send_call_t send(const TMessage &message);

    task_t<void> disconnect();
};

class actor_context_t {
public:
    std::optional<spot_rid_t> spot_rid() const;
    bound_session_t bound_session() const;

    actor_join_spot_call_t join_spot(spot_rid_t spot_rid,
      const zlink::framework::message_t &request);

    actor_join_entry_spot_call_t join_entry_spot(node_rid_t spot_node_rid,
      const zlink::framework::message_t &request);

    template <typename TRequest>
    actor_join_spot_call_t join_spot(spot_rid_t spot_rid,
      const TRequest &request);

    template <typename TRequest>
    actor_join_entry_spot_call_t join_entry_spot(node_rid_t spot_node_rid,
      const TRequest &request);
};

} // namespace zlink::framework
```

`spot_context_t::publish(...)`는 현재 spot channel 안의 topic publish를 뜻하므로
별도 channel name을 받지 않는다. 직접 `routing_id_t`를 다루는 API는 spot-to-spot
경로와 Entry Spot join 경로에 제한한다. 일반 application handler와 client는 channel
name과 topic을 먼저 사용한다.

SPOT node는 router 또는 pub/sub 역할 중 하나 이상을 켜야 한다. `add_spot_mesh(...)`는
프로세스의 단일 Spot node와 discovery view를 함께 선언한다. `enable_router(...)`나
`enable_pub_sub(...)` 없이 node를 선언하면 options 적용 시점에 설정 오류로 실패한다.

Spot Actor Join / Transfer 관련 interface도 이 문서에 기록된 정식 계약이며,
그 동작 의미는 [공통 스펙](../../23-spot-actor.ko.md)을 따른다. 구현이나 contract test가
이 시그니처와 다르면 계약 불일치로 처리한다.

`actor_transfer_adapter_t<TActor>`는 remote transfer에서 domain state를 옮겨야 하는 actor type에만
등록한다. 등록이 없으면 framework는 빈 `message_t`를 전송하고 target의 actor factory로 actor를
만드는 기본 경로를 사용한다. custom state를 전달해야 하는 actor type은
`add_actor_transfer_adapter<TActor, TAdapter>(...)`로 adapter type을 등록한다.

C++의 일반 packet handler registry는 `spot_context_t::handlers()`가 맡는다. 다만 actor
lifecycle은 registry 등록 표면이 아니다. user Spot은
`on_actor_join(actor_id, zlink::framework::message_t)`, `on_actor_joined(actor)`, `on_leave_actor(actor)`
member callback을 직접 제공한다. Entry Spot도 user Spot에서 Entry Spot으로 돌아오는
명시적 join을 `on_actor_join(actor_id, zlink::framework::message_t)`에서 accept/reject하고, commit 이후
callback인 `on_actor_joined(actor)`와 `on_leave_actor(actor)`를 제공한다.
일반 Spot 타입은 `zlink::framework::spot_t`를 상속해야 하고, Entry Spot 타입은
`zlink::framework::entry_spot_t`를 상속해야 한다. 이름이나 파일 위치로 역할을 추론하지 않는다.
`add_spot<TSpot>()`와 `add_entry_spot<TEntrySpot>()`가 이 계약을 compile-time으로 확인한다.

```cpp
class bingo_room_spot_t : public zlink::framework::spot_t,
                          public bingo_room_t {
public:
    zlink::framework::spot_actor_join_response_t on_actor_join(
      std::string_view actor_id,
      const zlink::framework::message_t &request);

    start_bingo_game_res_t start_game(const player_actor_t &actor,
      const zlink::framework::spot_actor_request_context_t &context,
      const start_bingo_game_req_t &request);

    void on_actor_joined(const player_actor_t &actor);

    void on_leave_actor(const player_actor_t &actor);

    void configure(zlink::framework::spot_context_t &context)
    {
        context.handlers()
          .add_actor_request<&bingo_room_spot_t::start_game>();
    }
};

class bingo_entry_spot_t : public zlink::framework::entry_spot_t {
public:
    void configure(zlink::framework::spot_context_t &context);
};
```

일반 Spot packet member와 subscription member는 payload 하나를 받는다.
actor join admission을 처리하는 member는 actor id와 `zlink::framework::message_t` request만 받으며,
`spot_actor_join_response_t`로 accepted 여부와 optional reply `zlink::framework::message_t`를 돌려준다.
actor type과 source/target Spot 및 node 정보는 framework 내부 routing과 검증에만 사용한다.
accepted가 `true`일 때만 actor 위치를 user Spot으로 commit하고
`on_actor_joined(actor)`를 호출한다. accepted가 `false`이면 actor 위치를 바꾸지 않고
post-joined callback도 호출하지 않는다. 예전 change-result 값 객체와 change kind는
commit 이후 callback 이름으로 의미가 분리되어 더 이상 필요하지 않다.
actor packet member는 actor, `spot_actor_request_context_t` 또는 `spot_actor_send_context_t`,
DTO를 받는다. actor disconnected handler는 actor만 받을 수 있다.
등록된 member는 descriptor로만 남지 않는다. dispatch 경로는 `serializer_registry_t`로
`message_t`를 DTO로 바꾸고, runtime이 현재 Spot instance와 actor를 찾아 typed member
function을 호출한다. 샘플도 이 경로를 통과해야 framework 동작을 확인했다고
볼 수 있다.
Entry Spot의 actor packet도 일반 Spot packet으로 등록하지 않는다. request는
`add_actor_request`, one-way send는 `add_actor_send`로 등록하고 member는 actor,
`spot_actor_request_context_t` 또는
`spot_actor_send_context_t`, DTO를 받는다. 이렇게 해야 `.NET` sample의 actor request
handler와 같은 구조가 된다.
stream header metadata 전체를 actor handler에 그대로 노출하지 않는다. 사용자는
`options.metadata().add_forwarded_metadata_key("trace-id")`처럼 application metadata forwarding 정책을 선언하고,
framework는 허용된 key만 `spot_actor_message_metadata_t`로 project해서 actor context에 넣는다.
handler는 `find(...)` 또는 `contains(...)`로 값을 조회한다. `values`는 단순 반복과 기존
호출자 호환을 위해 남기지만, handler code가 `std::map` 구조에 직접 묶이지 않아도 되게 조회
표면을 제공한다. 빈 metadata key와 공백만 있는 key는 의미가 모호하므로 `forward("")`와
`forward(" ")`에서 거부한다.
이 정책은 stream frame 구조나 ActorGateway 내부 frame을 public handler 표면에 드러내지 않기
위한 경계다.

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
actor context의 `join_spot(...)` request와 reply는 DTO 또는 `zlink::framework::message_t`다.
JSON DTO는 기본 serializer를 사용하므로 message type별 codec 설정이 필요 없다. Protobuf,
MessagePack, custom binary payload처럼 기본 JSON으로 표현할 수 없는 타입만 startup/options 에
serializer extension을 연결하고 업무 코드는 같은 join 호출을 유지한다. join 결과는
승인과 거절 `variant`다. 승인 값만 join 이후 actor ref를 가지며 두 값 모두 reply
`zlink::framework::message_t`를 담는다. typed reply가 필요하면
`async<TReply>()`가 같은 serializer registry로 decode한다. Entry Spot join도 같은 결과 타입을 돌려준다.
raw payload 처리는 framework 내부 invoker가 맡으며 application public actor context에
별도 raw join overload를 두지 않는다.

호출 실행 표면은 공통 비동기 call 계약을 C++ coroutine 관례로 표현한다.
`request(...)`, `send(...)`, `relay(...)`, `join_spot(...)`, `join_entry_spot(...)` 같은
호출은 call object를 반환한다. one-way call은 `submit()`이 local queue 수락 지점이고,
request와 join은 `async()`가 reply 완료를 기다리는 지점이다.
일반 channel `request_call_t`와 `send_call_t`는 metadata와 timeout을 submit 전에 모으고,
submit 시점에 framework envelope 정책으로 넘긴다. typed packet name은 registration
descriptor가 결정한다. request, join과 worker는 `async()` 완료 terminator 하나만 제공하고
framework가 실행 문맥을 관리한다. 장기 작업 중단 표면이 필요하면 C++ 표준 중단 관례를
사용하는 별도 정식 시그니처를 먼저 정의해야 한다.

```cpp
auto reply = co_await client
  .request("profile", query)
  .async<profile_reply_t>();

use_profile(reply);
```

public framework async 표면에 `std::future`를 사용하지 않는다. blocking wait는 handler,
timer, STREAM session callback, actor relay 경로에서 허용하지 않는다.

오류 종류는 `.NET` framework의 `ZLinkFrameworkErrorKind`를 C++ naming으로 투영한다.
`async()`는 실패 시 같은 정보를 가진 `framework_exception_t`를 throw한다.

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
그 대신 handler group을 먼저 고르고, 그 group 안에 handler 타입을 명시해서
`options.handlers().group(group_name).add<THandler>()`,
`add_send<THandler>()`, `add_publish<THandler>()`로 등록한다.
나머지 codec, discovery, client-server channel, handler group 구성은 `.NET`과 같은 읽기 수준을
유지한다.

JSON은 기본 codec이므로 별도 등록하지 않는다. 사용자가 모든 request/reply message
type을 codec 설정에 나열하지 않는다. C++ framework는 `options.handlers().group(...).add<THandler>()`에서
handler의 `request_type`, `reply_type`을 읽고 기본 JSON serializer를 내부에서 선택한다.
send handler는 `message_type`, publish handler는 `event_type`을 읽어 같은 방식으로 serializer와
handler registry 항목을 등록한다. `options.codecs().use(...)`는 일반 message type을 나열하는
단계가 아니라, 기본 JSON으로 표현할 수 없는 payload나 별도 binary serializer extension을
연결하는 고급 확장점이다. 따라서 request/send/publish handler를 같은 group 이름으로
묶고, channel builder의 `.use_handler_group(...)`에서 channel에 연결할 수 있다.
handler group은 channel 종류와 맞아야 한다. client/server channel은 request/send
handler group을 받을 수 있고, fanout channel은 publish handler group만 받을 수 있다. 맞지 않는
group을 연결하면 options 작성 시점에 설정 오류로 실패한다.
같은 channel에 같은 packet 이름의 handler가 두 번 노출되면 `request_protocol_error`로 실패한다.
이 규칙은 low-level `handler_registry_t` 직접 등록뿐 아니라 fluent options의 handler group
경로에도 적용한다. channel이 group을 먼저 참조한 뒤 handler가 들어오는 경우와 handler가 먼저
등록되고 channel이 나중에 group을 참조하는 경우 모두 중복을 허용하지 않는다.
client/server channel은 `server(...)` 또는 `client(...)` 중 하나 이상을 켜야 한다. fanout
channel은 `bind(...)`로 publisher를 열거나 `subscriber(...)`로 subscriber를 열어야 한다.
역할이 하나도 없는 channel 선언은 `.NET` registration validation과 같이 options 적용 시점에
설정 오류로 실패한다.
client/server channel이 server role을 켜면 request/send handler group을 하나 이상 연결해야 한다.
단, SPOT route channel로 accept된 server는 SPOT route ingress로 쓰일 수 있으므로 handler group이
없어도 허용한다. fanout channel이 subscriber role을 켜면 publish handler group을 하나 이상
연결해야 한다.
handler에 생성자 의존성이 있으면 `using dependency_types =
zlink::framework::dependency_list_t<dep1_t, dep2_t>;`처럼 의존 타입을 명시한다. framework는
handler를 등록할 때 `add_singleton<THandler, dep1_t, dep2_t>()`와 같은 DI 생성자 주입 등록을
사용한다.
`logger_t<THandler>`는 framework 기본 dependency다. handler가
`dependency_types`에 `logger_t<THandler>`를 넣으면 사용자가 별도 service registration을
작성하지 않아도 DI가 `.NET`의 `ILogger<T>`처럼 category logger를 주입한다. 로그 출력 대상은
handler 등록이 아니라 `app.logging().use_console()`, `app.logging().use_file(...)` 같은
host logging 설정에서 정한다. custom category가 필요하면 `logger_factory_t`를 dependency로
받아 handler 내부에서 category logger를 만들 수 있다.

```cpp
app.add_zlink_framework ([&](zlink::framework::zlink_framework_options_t &options) {
    options.use_filter<audit_filter_t>();
    options.metadata().add_forwarded_metadata_key("trace-id");

    options.add_client_server_channel(sample_names_t::api_channel)
      .enable_server(topology.api_channel_endpoint)
      .use_handler_group("api");

    options.add_client_server_channel(sample_names_t::play_channel)
      .enable_client();

    auto &dispatch = options.configure_dispatch();
    // Dispatch 최적화 방식은 runtime이 선택하고 public option으로 노출하지 않는다.
    dispatch.diagnostics.message_flow =
      zlink::framework::message_flow_log_mode_t::errors_only;

    options.handlers()
      .group("api")
      .add<authenticate_player_handler_t>()
      .add<match_bingo_api_handler_t>()
      .add_send<player_command_handler_t>();

    options.handlers()
      .group("events")
      .add_publish<notification_event_handler_t>();
});
```

`enable_client()`처럼 endpoint 인자 없이 client role을 켜면 등록된 location store에서 peer를 찾는다는 뜻이다.
이 경우 host service에 location store가 등록되어 있어야 한다. 특정 endpoint를 직접
붙일 때는 `enable_client(endpoint)`를 사용한다. `enable_client(endpoint)`와 fanout
`enable_subscriber(endpoint)`는 반복 호출할 수 있고, 호출 순서대로 같은 역할의 manual endpoint
목록에 추가된다. fanout subscriber도 discovery/manual 선택 규칙은 같다. location store
등록 오류는 host 시작 단계에서 즉시 거부한다.

이 구조에서는 샘플 `main.cpp`, role `*HostFactory`, 일반 사용자 설정 예제가 handler member
function pointer, handler용 DI factory lambda, monitoring channel 문자열, serializer smoke 검증,
message type을 모두 나열하는 codec 등록 같은 세부 구현을 직접 알 필요가 없다. 그런 내용이 보이면
framework options builder가 충분히 깊지 않은 것으로 본다.

`zlink_framework_options_t`의 사용자 표면은 fluent options builder로 제한한다.
일반 사용자 설정에는 낮은 수준 channel runtime builder를 직접 노출하지 않는다. C++ 내부 runtime builder에는 낮은 수준 API가 남아 있을 수 있지만,
샘플과 guide 수준의 설정은 아래처럼 역할이 바로 보이는 형태를 사용한다.

`options.configure_dispatch(...)`는 interface graph를 만들지 않고
`dispatch_options_t` value를 람다에 넘긴다. 이 value는 Spot과
STREAM dispatch mode, unhandled request/send/publish 정책, message flow diagnostics 설정을
담는다. native dispatch token, queue slot, handler lookup table은 이 표면에 나오지 않는다.
diagnostics sample rate는 `0.0`에서 `1.0` 사이여야 하며 NaN은 허용하지 않는다.
send와 publish는 reply path가 없으므로 unhandled 정책에 `reply_error`를 사용할 수 없다.

```cpp
options.add_client_server_channel(sample_names_t::api_channel)
  .enable_server(topology.api_endpoint)
  .use_handler_group("api");

options.add_fanout_channel(sample_names_t::notification_channel)
  .enable_publisher(topology.notification_endpoint)
  .enable_subscriber(topology.notification_subscriber_endpoint)
  .use_handler_group("events");

options.use_registry_spot_remote_addresses(sample_names_t::router_channel);

options.add_route_mesh(sample_names_t::router_channel)
  .enable_server(topology.session_spot_endpoint)
  .set_routing_id(topology.session_router_rid)
  .enable_client(topology.play_router_endpoint);

options.add_spot_mesh(sample_names_t::game_spot_discovery)
  .set_routing_id (topology.session_router_rid)
  .enable_router (topology.session_router_endpoint)
  .enable_pub_sub (topology.session_spot_endpoint)
  .add_entry_spot<session_entry_spot_t>();

options.add_stream_node(sample_names_t::stream_name)
  .bind(topology.stream_endpoint)
  .register_session<client_session_t>()
```

`register_session<TSession>()`은 `.NET`의 `RegisterSession<TSession>()`에 맞춘 typed session
등록 표면이다. `TSession`은 `packet_stream_session_t`를 상속해야 하며, framework service
collection에 stream-session scope 서비스로 등록된다. `TSession::session_name`이 있으면 그 값을
native packet session 이름으로 사용하고, 없으면 타입 이름 기반 message name을 사용한다.
`register_session(name)`은 session 이름을 직접 지정해야 하는 low-level 구성에 남긴다.
하나의 stream node에는 packet session을 하나만 선언한다. `register_session<T>()`과
`register_session(...)`을 중복 호출하면 마지막 값으로 덮어쓰지 않고 설정 오류로 처리한다.

route mesh는 server 역할 또는 client 역할 중 하나 이상을 선언해야 한다.
route handler 수신이나 SPOT route ingress가 필요한 runtime은 `enable_server(...)`로
local route endpoint를 연다. 다른 node로만 보내는 runtime은 bind endpoint 없이
`enable_client()`로 Discovery 기반 peer를 사용하거나 `enable_client(endpoint)`로
수동 peer에 연결한다. 역할 없이 `add_route_mesh(...)`만 선언하면 options 적용 시점에
설정 오류로 실패한다.
RouteMesh와 SpotMesh가 같은 프로세스에 함께 등록되면 framework가 SPOT route packet을 자동으로
local SpotNode로 분기한다.
fluent options에서 channel 이름, handler group 이름, endpoint, SPOT node 이름, stream node
이름처럼 식별자나 연결 주소로 쓰이는 값은 빈 문자열이나 공백 문자열을 허용하지 않는다.
잘못된 값은 low-level socket/runtime까지 전달하지 않고 builder 호출 또는 options 적용 시점의
framework error로 닫는다.
SPOT code가 client/server channel로 send/request 하려면 해당 channel에서
`enable_client(...)`를 설정한다. Spot node builder는 별도 channel client를 부착하지 않는다.
SPOT router와 pub/sub 역할도 registry discovery 없이 고정 peer를 붙일 수 있다. 이때는
`enable_router(endpoint).connect_router(peer)` 또는 target node routing id를 함께 주는
`enable_router(endpoint).connect_router(peer_rid, peer)` 또는
`enable_pub_sub(endpoint).connect_pub_sub(peer)`처럼 역할별 manual endpoint를 기록한다.
routing id는 `set_routing_id (routing_id)`로 Spot node에 한 번 지정한다. router와 pub/sub 역할을
같이 켜면 framework가 같은 node id에서 역할별 내부 id를 파생해 설정한다.
SpotMesh가 pub/sub 역할을 켜면 외부 코드의 Spot publish client는 해당 SpotMesh의
publisher handle을 사용할 수 있다. RouteMesh와 SpotMesh가 같은 프로세스에 있으면
framework가 route bridge를 자동으로 붙인다. 외부에서 Spot으로 routed send/request를
보내는 경로는 RouteMesh만 사용하며, client/server channel이나 fanout channel은 Spot
route ingress로 쓰지 않는다.

### 12.1 HTTP Hosting

HTTP hosting은 ASP.NET Core Minimal API의 `MapGet`, `MapPost`, `MapPut`,
`MapDelete`에 대응하는 C++ framework 표면이다. MVC controller, Razor page,
template rendering, WebSocket transport는 범위에 넣지 않는다. 대신 route handler,
DI scope, JSON binding, middleware/filter, logging, validation, error mapping,
zlink channel 호출은 같은 application host 안에서 제공한다.

```cpp
namespace zlink::framework {

class http_options_builder_t {
public:
    http_options_builder_t &listen(std::string endpoint);
    http_options_builder_t &configure_tls(
      std::function<void(http_tls_options_builder_t &)> configure);
    http_options_builder_t &configure_server(
      std::function<void(http_server_options_builder_t &)> configure);

    template <typename THandler>
    http_options_builder_t &map_get(std::string path);

    template <typename THandler>
    http_options_builder_t &map_post(std::string path);

    template <typename THandler>
    http_options_builder_t &map_put(std::string path);

    template <typename THandler>
    http_options_builder_t &map_delete(std::string path);

    template <typename TMiddleware>
    http_options_builder_t &use();

    http_options_builder_t &map_health(std::string path);
    http_options_builder_t &map_readiness(std::string path);
    http_options_builder_t &map_liveness(std::string path);
};

struct http_context_t {
    http_method_t method;
    std::string path;
    std::string correlation_id;
    std::map<std::string, std::string> request_headers;
    std::map<std::string, std::string> response_headers;
    std::optional<std::string> response_body;
    int response_status;

    http_context_t &response_header(std::string name, std::string value);
    http_context_t &json_response(int status, std::string body);
};

struct http_request_t {
    http_method_t method;
    std::string path;
    std::string target;
    std::string query_string;
    std::string correlation_id;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> route_values;
    std::map<std::string, std::string> query_values;
    std::string body;
    std::string content_type;
    std::string remote_endpoint;
};

struct http_response_t {
    int status = 200;
    std::string body;
    std::string content_type = "application/json";
    std::map<std::string, std::string> headers;

    http_response_t &header(std::string name, std::string value);
};

class zlink_framework_options_t {
public:
    http_options_builder_t http();
};

} // namespace zlink::framework
```

사용 예시는 아래와 같다.

```cpp
app.add_zlink_framework([&](auto &options) {
    options.add_client_server_channel(sample_names_t::api_channel)
      .enable_server(topology.api_channel_endpoint)
      .use_handler_group("api");
    options.add_client_server_channel(sample_names_t::play_channel)
      .enable_client();

    options.http()
      .listen(topology.api_http_endpoint)
      .map_post<create_game_http_handler_t>("/games");
});
```

HTTP handler는 message handler와 같은 type alias 규칙을 사용한다.

```cpp
class create_game_http_handler_t {
public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types =
      dependency_list_t<channel_client_t, logger_t<create_game_http_handler_t>>;

    explicit create_game_http_handler_t(
      channel_client_t &client,
      logger_t<create_game_http_handler_t> &logger);

    task_t<create_game_http_res_t> handle(const create_game_http_req_t &request);
};
```

`map_get<THandler>(...)`, `map_post<THandler>(...)`, `map_put<THandler>(...)`,
`map_delete<THandler>(...)`는 handler type을 DI에 등록하고, `request_type`과
`reply_type`의 JSON serializer를 등록하며, HTTP route table에 `method + path`를
연결한다. request마다 DI scope를 만들고 handler를 resolve한다. handler가 반환한 DTO는
JSON response body가 되고, 기본 status는 `200 OK`다.

HTTP handler는 아래 shape를 모두 지원한다.

- typed DTO: `reply_type handle(const request_type &request)`
- typed DTO async: `task_t<reply_type> handle(const request_type &request)`
- typed DTO + context:
  `reply_type handle(const request_type &request, http_context_t &context)`
- typed DTO + context async:
  `task_t<reply_type> handle(const request_type &request, http_context_t &context)`
- typed DTO + request:
  `reply_type handle(const request_type &request, const http_request_t &http)`
- typed DTO + request async:
  `task_t<reply_type> handle(const request_type &request, const http_request_t &http)`
- typed DTO + request + context:
  `reply_type handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- typed DTO + request + context async:
  `task_t<reply_type> handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- typed response: `http_response_t handle(const request_type &request)`
- typed response + context:
  `http_response_t handle(const request_type &request, http_context_t &context)`
- typed response async: `task_t<http_response_t> handle(const request_type &request)`
- typed response + context async:
  `task_t<http_response_t> handle(const request_type &request, http_context_t &context)`
- typed response + request:
  `http_response_t handle(const request_type &request, const http_request_t &http)`
- typed response + request async:
  `task_t<http_response_t> handle(const request_type &request, const http_request_t &http)`
- typed response + request + context:
  `http_response_t handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- typed response + request + context async:
  `task_t<http_response_t> handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- raw HTTP request: `http_response_t handle(const http_request_t &request)`
- raw HTTP request async: `task_t<http_response_t> handle(const http_request_t &request)`

`http_request_t`와 `http_response_t`는 framework public type이다. Raw HTTP handler도
`Boost.Beast` request, socket, SSL stream을 받지 않는다. `map_*<THandler>(...)`는 handler
shape를 compile-time으로 판별한다. typed route에서 여러 overload가 있으면 반환 타입보다
인자 shape를 먼저 본다. `http_request_t`와 `http_context_t`를 모두 받는 shape가 가장 먼저
선택되고, 그 다음 `http_request_t`, `http_context_t`, DTO-only shape 순서로 선택된다.
typed route와 raw route shape를 한 handler에 동시에 제공하면 static assertion 또는 startup
validation으로 실패해야 한다.

route parameter와 query string은 `request_type` DTO에 binding한다. 예를 들어
`/games/{gameId}/moves?actorId=p1`로 들어온 값은 body DTO와 합쳐 handler request가 된다.
같은 필드가 body, route, query에 동시에 있으면 route, query, body 순서로 우선한다. 이
우선순위는 URL에 드러난 식별자가 request body보다 더 명시적인 입력이라는 ASP.NET Core식
route handler 사용성을 따르기 위한 규칙이다.

`use<TMiddleware>()`는 exception, logging, validation, auth, correlation id 같은
cross-cutting 처리를 route handler 앞뒤에 연결한다. middleware/filter는 Beast나 Asio
타입을 받지 않고 `http_context_t`와 framework DTO만 다룬다.
middleware가 `before(http_context_t&)` 또는 `after(http_context_t&)`를 제공하면 runtime은
route handler 전후에 호출한다. request의 `X-Correlation-Id` 또는 `X-Request-Id`는
`http_context_t::correlation_id`로 들어가고 response의 `X-Correlation-Id`로 전파된다.
middleware가 `before(...)`에서 `json_response(...)`를 설정하면 runtime은 handler를 호출하지
않고 해당 JSON response를 반환한다. `map_health(...)`, `map_readiness(...)`,
`map_liveness(...)`는 `app.health()` report를 HTTP endpoint로 노출한다.

`listen(...)`은 `http://`와 `https://` endpoint를 모두 받는다. `https://` endpoint를
사용하면 `configure_tls(...)`로 server certificate와 private key를 설정해야 한다. TLS 설정 public
표면은 파일 경로, PEM data, reload policy 같은 framework 값만 사용하고 OpenSSL 또는
Boost.Asio SSL 타입을 노출하지 않는다.

HTTP runtime은 `hosted_service_t`로 app lifecycle에 묶인다. `Boost.Beast`, `Boost.Asio`,
OpenSSL/SSL context 타입은 runtime 구현에만 있고 public header에는 나타나지 않는다. HTTP error response는
`framework_error_kind_t`를 기반으로 `400`, `404`, `405`, `500`, `503`, `504`로 매핑한다.

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
      .request(sample_names_t::play_channel, allocate_bingo_room_req_t { request.mode })
      .async<allocate_bingo_room_res_t>();

    co_return match_bingo_api_res_t { allocated.room_id };
}
```

샘플 handler는 `.async().result().value()`로 결과를 직접 꺼내지 않는다. 그런 코드는
handler가 runtime 안에서 blocking wait를 수행하는 것처럼 보이고, 모든 언어 버전에서 같은 async
모델을 제공한다는 목표와 맞지 않다.

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
    config_builder_t &load_json(std::string path, optional_t optional);
    config_builder_t &load_env(std::string prefix);
    config_builder_t &load_cli(int argc, char **argv);
    config_builder_t &use_environment(std::string name);
    std::string environment() const;
    bool is_environment(std::string_view name) const;
    template<typename T> std::optional<T> bind(std::string prefix) const;
    template<typename T> T bind_required(std::string prefix) const;
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
tracing hook은 공통 message-flow tracing 계약의 observer와 runtime control을 구현한다.

### 13.1 message-flow dispatch error event

미등록 메시지와 dispatch 실패 관측은 메시지 흐름 observer의 `outcome=error` event 로 처리한다.
channel 별, spot 별 observer 등록은 이 버전의 공개 계약이 아니다. request 실패는 reply path 가 있으면
error reply 로 끝나고, local actor call 처럼 reply frame 이 없는 경로는 `task_t` 또는 pending operation
을 framework error 로 완료한다. one-way 실패는 drop 되지만 기본 로그, counter, message-flow event 를 남긴다.

```cpp
class dispatch_options_t
{
  public:
    dispatch_options_t &set_message_flow_observer(
      std::shared_ptr<message_flow_observer_t> observer);

    dispatch_options_t &set_message_flow_observer(
      std::function<void (const message_flow_event_t &)> observer);
};
```

`message_flow_event_t` 의 error event 는 `surface`, `message_kind`, `error_reason`, `error_action`,
`packet_name`, `channel_name`, `topic`, `spot_rid`, `actor_id`, `source_rid`,
`correlation_id`, `exception` 을 담는 snapshot 이다. native message 소유권이나 frame 참조는
포함하지 않는다.

```cpp
app.add_zlink_framework([](auto &options) {
  options.configure_dispatch()
    .set_message_flow_observer(
      std::make_shared<my_message_flow_observer>());
});
```

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

    app.add_zlink_framework([](auto &options) {
        options.services()
          .add_singleton<order_repository_t>()
          .add_singleton<order_handler_t, order_repository_t>();
        options.add_client_server_channel("orders")
          .enable_server("tcp://0.0.0.0:7001")
          .use_handler_group("orders-api");
        options.add_spot_mesh("orders")
          .enable_router("tcp://0.0.0.0:7101");
        options.handlers()
          .group ("orders-api")
          .add<order_created_handler_t> ();
    });

    return app.run(argc, argv);
}
```

이 샘플에서 사용자는 `zlink::router_socket_t`, `zlink::dealer_socket_t`,
`zlink::service::spot_node_t`를 직접 만들지 않는다. framework host가 C++ binding
타입을 생성하고 lifecycle을 관리한다.


## 15. C++ 고유 계약

### 15.1 Backpressure

**SPOT과 STREAM의 send-ready callback과 pending queue는 runtime 내부 구현이다.** public 표면은
**call object, timeout, result error kind**로만 backpressure를 보여 준다.

- **application handler가 pending queue를 직접 resume하거나 poller readiness를 다루는 API를 두지
  않는다.**
- **기본 정책은 무한 queue가 아니다.** queue 상한·submit timeout·overflow 정책은 framework runtime
  설정으로 닫고, **한도 초과는 실패 result로 돌려준다**(`request_rejected` 등).

### 15.2 Handler filter

**filter는 `handler_invocation_context_t`로 descriptor·dispatch context·immutable message payload를
읽는다.** **payload를 바꾸려면 `next()` 결과 대신 새 `message_t`를 반환한다.**

filter의 등록 순서·`next` 의미·scope는 [framework API §2.6](../../05-framework-api.ko.md)이
소유한다.

### 15.3 Public surface 경계

- **public surface는 native socket, poller, callback userdata를 직접 노출하지 않는다.**
- **handler public contract는 `contracts/handlers/*`가 소유한다.** handler descriptor map, DI
  resolve, serializer 호출 순서, dispatch lookup **구현**은 `src/runtime/handlers/*`에 둔다.
- **handler template 코드는 handler shape 검사와 type-erased 호출로 제한한다.** pending queue,
  recv loop, monitoring event 생성 구현을 `contracts/detail/*`에 넣지 않는다.


## 16. Public 타입 카탈로그

**이 절은 위 절들이 다루지 않은 public 타입을 채운다.** 여기 없는 `*_state_t`·`*_snapshot_t`는
**runtime 내부 상태**이며 공개 계약이 아니다.

### 16.1 Dispatch 오류 계약

**언어 중립 의미는 [framework API §2.4.3](../../05-framework-api.ko.md)이 소유한다.** C++은 다음
enum과 event로 표현한다.

```cpp
enum class dispatch_error_surface_t
{ channel, route_mesh_channel, spot_route, spot_subscription, spot_actor, stream_session };

enum class dispatch_message_kind_t
{ request, send, publish, response, error, actor_request, actor_send };

enum class dispatch_error_reason_t
{ handler_missing, payload_decode_failed, handler_exception,
  invalid_frame, reply_path_missing, unexpected_reply };

enum class dispatch_error_action_t { reply_error, drop };

struct message_dispatch_error_event_t
{
    dispatch_error_surface_t surface;
    dispatch_message_kind_t  message_kind;
    dispatch_error_reason_t  reason;
    dispatch_error_action_t  action;
    std::optional<std::string> packet_name, channel_name, topic;
    std::optional<std::string> spot_rid, actor_id, source_rid, correlation_id;
    std::exception_ptr exception;
    std::optional<std::string>    flow_id;      // flow_origin과 함께 있거나 함께 없다
    std::optional<flow_origin_t>  flow_origin;
};

enum class flow_origin_t : std::uint8_t
{ inbound = 1, timer = 2, application = 3, lifecycle = 4 };  // wire 값 고정
```

> **미충족.** 공통 스펙은 `action`에 **`fail_caller`** 를 요구한다(reply frame이 없는 경로에서
> caller를 오류로 완료). C++ `dispatch_error_action_t`에는 **`reply_error`와 `drop` 두 값뿐**이다.
> [구현 차이 §10.7b](../../90-implementation-gap.ko.md)이 이 gap을 소유한다.

### 16.2 Dispatch 실행 정책

```cpp
enum class handler_execution_t;         // handler 실행 방식
enum class unhandled_dispatch_action_t; // 처리되지 않은 dispatch의 처리
struct unhandled_dispatch_options_t;
class  dispatch_diagnostics_options_t;  // read-only 진단 옵션
struct dispatch_options_t;
```

### 16.3 메시지 흐름 관측

```cpp
enum class message_flow_log_mode_t;   // off, errors_only(기본), key_transitions, verbose, diagnostic
enum class message_flow_outcome_t;    // received, dispatched, replied, dropped, sent, reply_received, error
struct message_flow_event_t;
class  message_flow_observer_t;       // on_message_flow(...)
```

의미는 [메시지 흐름 추적](../../52-message-flow-tracing.ko.md)과
[흐름 상관관계](../../53-flow-correlation.ko.md)가 소유한다.

### 16.4 Health

```cpp
enum class health_status_t;             // healthy, degraded, unhealthy
enum class health_check_scope_t { readiness, liveness, readiness_and_liveness };

struct health_check_result_t
{
    std::string name, component;
    health_status_t     status = health_status_t::healthy;
    health_check_scope_t scope = health_check_scope_t::readiness_and_liveness;
    std::string message;
};

struct health_report_t
{
    health_status_t status, readiness, liveness;
    std::vector<health_check_result_t> checks;
    bool ready () const noexcept;   // readiness != unhealthy
    bool live  () const noexcept;   // liveness  != unhealthy
};

class health_builder_t;   // health check 등록
```

**`readiness`와 `liveness`를 분리한다.** 트래픽을 받을 준비(readiness)와 프로세스 생존(liveness)은
다른 질문이다. **`degraded`는 `ready()`·`live()`를 막지 않는다.**

### 16.5 HTTP route와 middleware

**HTTP hosting 시나리오는 [60](60-http-hosting.ko.md)·[61](61-embedded-http-server.ko.md)이
소유한다.** 여기서는 public 타입만 고정한다.

```cpp
enum class http_method_t { get, post, put, delete_ };

struct http_context_t;    // 요청 처리 문맥
struct http_request_t;
struct http_response_t;

class http_route_t
{
public:
    http_method_t method;
    std::string   path;
    std::string   handler_name;
    bool context_response_precedence = false;  // context가 만든 response를 우선한다
    bool validates_json_content_type = true;   // JSON content type을 검증한다
    // 실제 invoker는 private. builder만 설정한다
};

struct http_middleware_t
{
    std::string name;
    std::function<std::shared_ptr<void> ()> create_instance;
    std::function<void (service_provider_t &, http_context_t &, const std::shared_ptr<void> &)> before;
    std::function<void (service_provider_t &, http_context_t &, const std::shared_ptr<void> &)> after;
};

struct http_endpoint_t { std::string uri; std::optional<http_tls_options_t> tls; };
struct http_server_options_t;
struct http_tls_options_t;
class  http_tls_options_builder_t;
```

- **middleware는 `before`/`after` 쌍이다.** `next` delegate 방식이 아니다 —
  [handler filter](../../05-framework-api.ko.md)와 모양이 다르다.
- **middleware 인스턴스는 `create_instance`로 만들고 DI provider를 함께 받는다.**

### 16.6 Location store

```cpp
class peer_location_store_t;
class spot_location_store_t;
class actor_location_store_t;
class route_location_store_t;
class location_change_stamp_store_t;
```

**store 계약과 owner lease 의미는 [location runtime §3](../../40-location-runtime.ko.md)이
소유한다.** Redis 구현의 key 규약은 [41](../../41-location-store-redis.ko.md)이 소유한다.

```cpp
enum class location_write_status_t;
struct location_write_intent_t;
struct location_write_result_t;
struct location_owner_token_t;
struct owner_lease_renewal_t;
enum class location_auto_connect_type_t;
enum class location_change_type_t;
```

### 16.7 Worker

```cpp
enum class worker_completion_mode_t;
class worker_scheduler_t;
```

**worker는 spot·session 실행 문맥 밖에서 도는 작업이다.** 완료를 원래 실행 문맥에서 재개하는
규칙은 [비동기 실행 정책](../../04-async-execution-policy.ko.md)이 소유한다.

### 16.8 Timer

```cpp
struct timer_failure_event_t;   // handler 실패. 계속 실행 / timer 중단을 구분한다
```

timer 등록 검증은 [stage-wrapper §4.1](../../25-stage-wrapper-on-spot.ko.md)이 소유한다.

### 16.9 오류 경계

```cpp
namespace detail { class boundary_error_t; }   // 내부 상태. public 아님
class framework_exception_t;                    // code() -> std::error_code
template <typename T> class result_t;
```

**공개 계약층은 `result_t`로 실패를 돌려준다.** 예외는 경계에서만 쓰고, 의미는
`framework_exception_t::code()`의 `std::error_code` 파셋으로 노출한다.


### 16.10 Transport

```cpp
enum class transport_scheme_t { tcp, ipc, tls, websocket, websocket_tls };

class transport_endpoint_t
{
public:
    transport_endpoint_t (transport_scheme_t scheme, std::string uri);
};
```

**endpoint는 scheme과 URI를 함께 갖는다.** scheme→transport 매핑의 의미는
[Stream Connector §3](../../32-stream-connector.ko.md)이 소유한다.

### 16.11 등록 builder

**등록 표면은 builder 계층이다.** 각 builder가 자기 역할의 설정만 소유한다.

```cpp
class client_server_channel_builder_t;        // client/server channel
class fanout_channel_builder_t;               // fanout channel
class route_mesh_channel_builder_t;           // route mesh channel
class spot_node_options_builder_t;            // SpotNode 옵션
class spot_mesh_builder_t : public spot_node_options_builder_t;  // spot mesh + node
class group_builder_t;                        // handler group
class handler_options_builder_t;              // handler 옵션
class codec_options_builder_t;                // codec registry
class metadata_policy_builder_t;              // 전달할 metadata key
class stream_compression_options_builder_t;   // STREAM 압축
```

- **channel 종류는 배타적이다.** client/server builder와 fanout builder를 같은 channel 이름에
  함께 쓸 수 없다([channel-topology §4](../../10-channel-topology.ko.md)).
- **`spot_mesh_builder_t`가 SPOT channel 이름과 그 channel을 소유하는 SpotNode를 함께 등록한다**
  ([spot-messaging §4.1](../../20-spot-messaging.ko.md)).

```cpp
enum class spot_drain_policy_t { drain_natural, release_and_recreate };
enum class drain_force_reason_t;
```

drain 정책의 의미는 [Graceful Drain §5](../../54-graceful-drain-handoff.ko.md)가 소유한다.

### 16.12 Channel 표면

```cpp
enum class channel_capability_t;              // server, client, publisher, subscriber
enum class route_handler_kind_t;              // route handler 종류
struct route_handler_context_t;
struct route_handler_registration_t;
struct channel_runtime_options_t;
struct client_server_channel_runtime_options_t;
struct route_mesh_channel_runtime_options_t;
struct channel_server_socket_runtime_options_t;
struct channel_reliability_event_t;           // 연결 신뢰성 event
class  channel_outbound_exchange_t;           // outbound 교환 표면
class  relay_call_t;
class  bound_session_send_call_t;
```

### 16.13 SPOT 표면

```cpp
enum class spot_handler_kind_t { packet, subscription, actor_send, actor_request };

struct spot_route_t
{
    node_rid_t  node_rid;
    spot_rid_t  spot_rid;
    std::string spot_name;
};

struct accepted_spot_route_channel_t
{
    std::string              channel_name;
    std::vector<std::string> manual_connections;
};

struct spot_info_t;                       // 조회 결과. spot rid만 담는다
struct spot_packet_context_t;             // packet handler가 받는 문맥
struct spot_packet_descriptor_t;
struct spot_handler_descriptor_t;
struct spot_lifecycle_callbacks_t;        // 생성·초기화·종료
struct spot_actor_admission_callbacks_t;  // actor join admission
enum  class spot_accept_reject_result_t;  // admission 결과
class  spot_node_manager_t;               // 생성·조회·종료
```

**lifecycle callback의 호출 순서는 [spot-node §3.2](../../21-spot-node.ko.md)가 소유한다** —
handler 구성 → 생성 callback → **수락된 경우에만** 초기화 → 종료는 한 번.

### 16.14 Actor 표면

```cpp
enum class actor_placement_t;             // actor를 어디에 둘지

struct actor_ref_snapshot_t
{
    node_rid_t    node_rid;
    std::string   actor_id;
    std::uint64_t generation = 0;

    static actor_ref_snapshot_t from (const actor_ref_t &);
    actor_ref_t   to_actor_ref (std::string actor_type) const;
};

struct actor_join_reply_t;                // join 결과
class  actor_client_t;                    // actor로 보내는 client
class  actor_directory_t;                 // actor 조회
class  actor_send_call_t;
class  actor_request_call_t;
class  actor_join_call_t;
class  relay_request_call_t;
```

**`generation`이 stale actor ref를 걸러낸다.** 의미는
[spot-actor §8](../../23-spot-actor.ko.md)이 소유한다.

### 16.15 STREAM 표면

```cpp
enum class stream_message_kind_t : std::uint8_t;   // wire kind
enum class stream_header_flags_t : std::uint8_t;   // wire flags
enum class stream_codec_t        : std::uint8_t;
enum class stream_session_error_t;                 // session에 귀속되는 오류

enum class stream_close_reason_t : std::uint8_t
{
    client_close = 1, idle_timeout = 2, heartbeat_timeout = 3,
    server_drain = 4, protocol_error = 5, transport_error = 6
};

struct stream_header_t;
class  stream_compression_codec_t;   // compress / decompress
```

**wire 값이 계약이다.** `stream_close_reason_t`의 1~6은
[Stream Connector §4.6](../../32-stream-connector.ko.md)의 `session-closing` payload와 같은 값이다.
**enum을 정수로 cast해 wire 값으로 쓰지 않는다** — codec이 명시적으로 변환한다.

### 16.16 Location 표면

```cpp
enum class location_kind_t;          // peer / spot / actor / route
enum class location_role_t;
enum class route_kind_t;
enum class location_readiness_t;
enum class location_change_type_t;
enum class location_auto_connect_type_t;
enum class location_change_stamp_scope_t;

struct peer_location_t;   struct peer_location_key_t;   struct peer_location_filter_t;
struct spot_location_t;   struct spot_location_key_t;   struct spot_location_filter_t;
struct actor_location_t;  struct actor_location_key_t;  struct actor_location_filter_t;
struct route_location_t;  struct route_location_key_t;  struct route_location_filter_t;

struct owner_lease_t;  struct owner_lease_snapshot_t;  struct owner_lease_renewal_t;
struct location_changed_t;
struct location_page_request_t;
struct location_options_t;

class peer_location_store_t;   class spot_location_store_t;
class actor_location_store_t;  class route_location_store_t;
class owner_lease_store_t;     class location_watch_store_t;
class location_change_stamp_store_t;

class location_runtime_query_t;   // 운영 조회
struct location_runtime_status_t;
struct location_topology_entry_t;   struct location_topology_filter_t;
struct location_service_summary_t;  struct location_service_summary_filter_t;
```

**row·key·lease의 의미는 [location runtime §2~§3](../../40-location-runtime.ko.md)이 소유한다.**

**resolver:**

```cpp
class spot_handle_t;                  // 불투명 spot 주소
class spot_handle_resolver_t;         // spot rid -> handle
class actor_spot_handle_resolver_t;   // actor -> 현재 spot handle
class peer_location_resolver_t;
```

**`spot_handle_t`는 불투명하다.** application은 **handle 안의 위치값을 낱개로 풀어 쓰지
않는다**([spot-address-messaging](../../24-spot-address-messaging.ko.md)).

### 16.17 Runtime event

```cpp
enum class runtime_event_severity_t;
enum class socket_event_kind_t;     struct socket_event_payload_t;
enum class spot_event_kind_t;       struct spot_event_payload_t;
enum class actor_event_kind_t;      struct actor_event_payload_t;
enum class stream_event_kind_t;     struct stream_event_payload_t;
enum class location_event_kind_t;   struct location_event_payload_t;
struct drain_event_t;
struct spot_timer_diagnostic_t;
struct runtime_event_base_t;
class  runtime_event_publisher_t;
```

**source별로 표면을 나누는 근거는 [runtime-monitoring §2](../../50-runtime-monitoring.ko.md)가
소유한다.** timer 실패는 **계속 도는 실패**와 **timer가 중단된 실패**를 구분한다.

**metric:**

```cpp
enum class metric_instrument_kind_t;   // counter / histogram / gauge
enum class metric_temporality_t;
struct metric_event_payload_t;
```

계기 카탈로그는 [runtime-metrics](../../51-runtime-metrics.ko.md)가 소유한다.

### 16.18 실행 문맥

```cpp
class serial_turn_t;          // spot·session의 직렬 실행 턴
class serial_turn_scope_t;    // RAII
struct ambient_context_hooks_t;  // flow 등 ambient 문맥 훅
```

**같은 spot의 dispatch가 직렬화되는 근거는
[stage-wrapper §3](../../25-stage-wrapper-on-spot.ko.md)이 소유한다.**

### 16.19 Codec

```cpp
struct encoded_payload_t;              // codec이 만든 payload
template <typename T> struct is_json_serializable_t;
template <typename T> struct is_json_deserializable_t;
```

### 16.20 Handler

```cpp
enum class handler_kind_t;   // request / send / publish
```


### 16.21 Configuration 조회

```cpp
class configuration_model_t;   // 계층으로 합친 설정
enum class optional_t;         // 필수/선택

class configuration_section_t
{
public:
    configuration_section_t (const configuration_model_t &model, std::string prefix);

    std::string key () const;                        // 이 section의 prefix
    bool contains (std::string_view key) const;      // 하위 key 존재 여부
    // 타입별 조회는 model이 제공한다
};
```

**section은 prefix로 잘라낸 view다.** 설정 소스를 계층으로 합치는 규칙은
[01 §5](01-system-structure.ko.md)가 소유한다.

### 16.22 Codec 등록

```cpp
class codec_registration_context_t
{
public:
    explicit codec_registration_context_t (serializer_registry_t &serializers);

    template <typename TPayload>
    codec_registration_context_t &add (...);   // payload 타입별 serializer 등록
};
```

**codec extension이 이 context로 serializer를 등록한다.** 같은 registry를 framework·HTTP
client·stream connector가 공유한다([channel-messaging §6](../../11-channel-messaging.ko.md)).

### 16.23 Location watch

```cpp
struct location_watch_filter_t
{
    location_kind_t             kind = location_kind_t::peer;
    std::optional<std::string>  mesh_name;
    std::optional<route_kind_t> route_kind;
};

enum class location_change_type_t { upserted = 1, removed = 2, expired = 3 };
```

**watch는 필터로 좁힌다.** `expired`는 owner lease 만료이며 `removed`와 구분한다
([location runtime §2.5](../../40-location-runtime.ko.md)).

### 16.24 공개 계약이 아닌 타입

**다음은 public header에 이름이 보이지만 계약이 아니다.** forward declaration만 있고 정의가
`src/runtime/`에 있는 **runtime 내부 타입**이다. application은 이 타입을 직접 다루지 않는다.

```text
channel_runtime_t          spot_node_runtime_t        stream_runtime_t
actor_gateway_runtime_t    actor_gateway_t            timer_runtime_t
monitoring_runtime_t       endpoint_connections_runtime_t
channel_runtime_manager_t  service_registry_t         injected_handler_registrar_t
*_state_t  *_snapshot_t  *_access_t  *_impl_t  erased_*
```

**구조는 [internals/runtime-architecture](../../../../cpp/internals/runtime-architecture.ko.md)가
소유한다.**

## 17. C++ 문서와 sample 일관성 요구

C++ framework의 public 문서와 sample은 다음 표면만 사용해야 한다.

- bootstrap은 `app_t::create()`를 사용한다.
- handler 등록은 `add_zlink_framework(...)` 안의 `options.handlers()` 표면을 사용한다.
- raw `request_handler_t`, `send_handler_t`, `event_handler_t` 중심 표면은 고급 raw
  handler extension으로 내리고, 일반 샘플은 typed handler registry를 사용한다.
- channel client와 event publisher는 `message_bus_t`,
  `request_client_t`, `publisher_t` 주입 표면과 맞춘다.
- handler와 publisher 표면은 channel name을 먼저 받고, topic 또는 packet name을
  그 다음에 받는 형태로 맞춘다.
- channel 연결 설정은 channel 전체가 아니라 역할 builder에 둔다.
- SPOT 문서는 binding의 `service::spot_node_t`, `service::spot_t` 기능을 framework
  builder와 `spot_context_t`로 감싼다.
- SPOT discovery는 등록된 location store와 `set_spot_route_channel(...)`로 사용할
  route channel을 연결한다.
- session actor relay는 `session_actor_t::relay(...)` 표면을 사용한다.
- SPOT timer는 CAPI timer `fire_count`를 기반으로 `timer_options_t`, `timer_tick_t`,
  overrun policy, timer failure monitoring을 포함하는 표면으로 맞춘다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ Channel Messaging](01-system-structure.ko.md) | [다음: C++ Runtime Architecture](../../../../cpp/internals/runtime-architecture.ko.md)
<!-- framework-adapter-nav:bottom:end -->
