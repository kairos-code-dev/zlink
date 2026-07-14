<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md) | [다음: ZLink Framework Channel Topology](10-channel-topology.ko.md)
<!-- framework-adapter-nav:end -->


[문서 묶음](../README.ko.md) | [개요](01-overview.ko.md) | [상호작용 모델](02-interaction-model.ko.md) | [메시지 모델](03-message-model.ko.md) | [channel topology](10-channel-topology.ko.md) | [공통 sample](../sample/README.ko.md) | [공통 E2E](../e2e/README.ko.md) | [.NET](../../dotnet/README.ko.md) | [Java](../../java/README.ko.md) | [Node.js](../../node/README.ko.md) | [C++](../../cpp/README.ko.md)

# ZLink Framework API

## 1. 목적

같은 `ZLink Framework`라도 `ASP.NET Core`, `Spring Boot`, `NestJS`,
`FastAPI`, C++ zlink framework host 사용자가 기대하는 표면은 조금씩
다르다. 이 문서는 각 환경에서 "어떤 식으로 보이면 자연스러운가"를 정리한다.

2절과 8절은 공통 규범 계약이다. 3절부터 7절까지의 언어별 예시와 host mapping은
비규범 설명이며 정확한 public interface를 정의하지 않는다. 언어별 타입과
시그니처는 `languages/<lang>/`의 정식 interface 문서가 소유한다.

핵심 원칙은 단순하다.

- 프레임워크 사용자가 익숙한 등록 방식에 맞춘다.
- low-level socket 이름을 공용 API 앞면으로 내세우지 않는다.
- request handler, event handler, outbound client를 DI와 함께 설명한다.
- runtime monitoring도 DI와 함께 설명할 수 있어야 한다.
- 서버 간 `send/request`는 HTTP handler mapping과 닮은 경험으로 보이게 한다.
- application이 직접 호출하는 high-level messaging API(`send/request/reply/publish/
  join`, `sendToChannel/requestToChannel`, Spot outbound 등)는 `Message`가 아니라
  **업무 객체**를 받고, request/join에는 업무 reply 객체를 돌려준다.
- 업무 객체를 byte payload로 직렬화하는 codec 선택과 packet name 결정은 **framework
  내부**(runtime serializer registry + packet name 추론)에서 처리한다. 호출자는
  `Message.from(...)`, `.ToJson()`, `.ToProto()` 같은 직렬화 helper를 high-level 호출에서
  직접 사용하지 않는다.
- `Message`는 bindings low-level transport 표현으로 남고, high-level application API의
  기본 입력 타입이 되지 않는다(설계 기준: framework object messaging surface 및
  bindings message boundary 정렬).
- raw transport header는 handler 인자로 직접 노출하지 않는다.
- 서버 간 framework transport는 공통
  [03-message-model.ko.md](03-message-model.ko.md)의 multipart `header + payload` 계약을
  따른다. 각 언어 adapter는 payload를 header object 안에 다시 넣어 단일 메시지로
  직렬화하면 안 된다.

## 2. 공통 방향

### 2.1 서버 쪽

- handler를 프레임워크 표준 등록 방식으로 등록한다.
- 요청 payload는 typed object로 받는다.
- header metadata와 timeout 정보는 context에서 조회한다.
- `send`는 응답 없는 handler, `request`는 응답 있는 handler로 설명할 수 있어야
  한다.
- `stream`은 일반 request handler와 다른 전용 handler 그룹으로 분리할 수
  있어야 한다.
- `stream`은 framework Header 기반 packet session만 우선 지원하고, recv loop는
  기본 application 표면에 올리지 않는다.
- `stream` callback은 write와 peer 식별을 함께 가진 stream 객체를 받고,
  session error는 error kind enum과 native detail을 함께 가진 구조화된 값으로
  받는 편이 자연스럽다.
- stream 직렬성 / callback 실행 규칙의 권위는
  [02-interaction-model.ko.md §3.4](02-interaction-model.ko.md)에 둔다. 이 문서는
  필요한 곳에서 같은 규칙을 따른다고만 적고, 정의는 한 곳에서만 한다.

### 2.2 클라이언트 쪽

- 공용 outbound client를 DI로 주입한다.
- 요청 메서드는 async 중심으로 제공한다.
- codec, timeout, target channel을 설정할 수 있다.
- outbound 호출의 payload 인자는 **업무 객체**다. codec 선택은 호출부가 아니라 runtime
  구성 단계에서 끝난다. framework가 요청·응답 객체 타입을 보고 serializer를 찾아 byte
  payload로 직렬화하고, reply도 업무 객체로 복원한다.
- JSON은 framework 표준 codec이며, codec을 따로 설정하지 않으면 기본값으로 사용한다.
  새 코드에서는 JSON만 쓰기 위해 별도 등록을 하지 않는다.
- Protobuf와 MessagePack은 framework core의 기본 의존성이 아니다. 두 codec은 선택
  framework codec extension package로 제공한다. application은 필요한 package만 설치하고
  구성 단계에서 extension을 등록한다.
- 사용자 정의 codec도 Protobuf/MessagePack과 같은 extension 계약을 사용한다. Avro, Thrift,
  사내 binary format 같은 codec은 framework core를 바꾸지 않고 extension package로 추가한다.
  serializer는 업무 객체와 byte payload 사이의 변환만 담당하고, packet name 결정과 codec
  선택 정책은 framework 내부에 남는다.
- framework와 HTTP client는 같은 codec extension을 공유한다. TypeScript browser stream connector는
  browser-safe payload codec을 connector의 `codec` option으로 주입한다. codec을 바꿔도 handler
  method, request method, reply type, payload DTO는 바꾸지 않는다.

  | 대상 | codec 설정 방향 |
  |------|----------------|
  | framework | runtime 구성 단계에서 codec extension을 등록한다. JSON은 기본값이고, Protobuf/MessagePack/custom codec은 extension으로 추가한다. |
  | TypeScript stream connector | codec package root가 제공하는 browser-safe payload codec을 connector 생성 시 `codec` option으로 주입한다. |
  | HTTP client | typed request/response body를 같은 codec extension으로 encode/decode한다. raw body API는 extension을 거치지 않는다. |

- Protobuf와 MessagePack extension package는 framework가 작성된 언어에만 만든다. Node package
  root는 browser-safe payload codec을, `./framework` subpath는 server serializer 등록 adapter를
  제공한다. package root의 module graph는 Node framework runtime을 참조하지 않는다.

  | 언어 | Protobuf extension 작성 위치 | MessagePack extension 작성 위치 |
  |------|------------------------------|---------------------------------|
  | .NET | `framework/languages/dotnet/src/Zlink.Framework.Codecs.Protobuf/` | `framework/languages/dotnet/src/Zlink.Framework.Codecs.MessagePack/` |
  | Java/Kotlin | `framework/languages/java/zlink-framework-codec-protobuf/` | `framework/languages/java/zlink-framework-codec-msgpack/` |
  | Node | `framework/languages/node/packages/framework-codec-protobuf/` | `framework/languages/node/packages/framework-codec-msgpack/` |
  | C++ | `framework/languages/cpp/extensions/framework-codec-protobuf/` | `framework/languages/cpp/extensions/framework-codec-messagepack/` |
  | Python, Go, Rust | 작성하지 않는다. | 작성하지 않는다. |

- bindings는 codec extension을 소유하지 않는다. bindings는 raw `Message`, byte payload,
  core protocol API만 제공한다. Python, Go, Rust는 bindings codec package 제거 뒤 대체
  codec package를 제공하지 않고 raw `Message`/bytes API만 유지한다.
- typed message의 packet name은 registration 시 message type descriptor에서 한 번
  확정한다. 선언적 metadata가 있으면 그 이름을 사용하고, 없으면 nominal type 이름을
  사용한다. **codec은 packet name에 관여하지 않는다** — codec을 바꿔도 dispatch key는
  그대로다. payload instance와 handler가 이름 결정 규칙을 다시 구현하지 않는다.
- packet name을 명시적으로 받는 표면은 셋으로 한정한다: raw message extension, handler 등록
  호출의 packet name override, 그리고 **STREAM connector의 호출별 `PacketName(...)`
  override**(호출자가 명시하면 그 이름이 우선한다).
- gateway 주소나 load balancer 주소 대신 `channel name` 기준 호출을 기본으로
  삼는다.
- send는 기본적으로 one-way submit으로 둔다. backpressure 처리는 호출자가
  `DontWait` 같은 옵션으로 고르지 않고 framework 내부의 nonblocking send와 ready
  notification이 맡는다.
- framework runtime은 등록한 outbound channel마다 별도 outbound runtime을 관리할
  수 있어야 한다.
- 단순 unary request 외에 event publish와 필요하면 aggregate helper를 분리할 수
  있어야 한다.
- 운영 점검이나 관리 API에서는 location runtime query
  (`IZLinkLocationRuntimeQuery` — [location runtime](40-location-runtime.ko.md) §7)의
  원시 row, runtime이 합성한 topology 보기, status를 읽는 별도 조회 표면을 둘 수 있어야 한다.
- socket/location runtime/spot 변화를 typed event handler로 받을 수
  있는 별도 monitoring surface도 둘 수 있어야 한다.
- 이 outbound client는 framework 전용 메시지 handler 안뿐 아니라, 기존 HTTP
  handler나 controller 안에서도 그대로 쓸 수 있어야 한다.
- caller가 transport 위치값을 직접 넘기는 direct routed 호출은 기본 application
  표면으로 두지 않는다. actor나 spot으로 보내는 public send/request는 resolver가
  target `RoutingId`를 숨기는 형태를 우선한다.
- application public API가 typed object 중심이더라도 adapter 내부 wire에서는
  server-to-server message를 multipart로 유지한다. handler 표면에서 raw header를
  숨기는 일과 transport에서 header/payload를 한 메시지로 합치는 일은 다르다.
- session server와 play server를 분리하는 구조에서는 `actorId`를 client-facing
  공개 키로 사용한다. session -> actor 방향은 actor bind/relay helper로,
  actor -> client 방향은 bound session(`IZLinkBoundSession`)으로 나눈다. actor 개념의 라이프사이클
  과 표면은 [22-actor-model.ko.md](22-actor-model.ko.md)에서, gateway 패턴의 사용성
  결정은 [31-session-actor-dispatch.ko.md](31-session-actor-dispatch.ko.md)에서
  본다.

### 2.3 Framework 오류 kind

framework 오류는 언어마다 exception/error 표현이 달라도 같은 kind 집합을 가진다. 숫자 값은
관측과 진단 데이터의 안정성을 위해 고정한다. 오류 kind는 값 `0`도 유효한 멤버이므로,
`Invalid=0` 규칙의 예외다.

| 값 | kind | 기본 재시도 |
|----|------|-------------|
| 0 | `ActorRouteNotFound` | no |
| 1 | `ActorCreateFailed` | no |
| 2 | `ActorAlreadyExists` | no |
| 3 | `ActorTypeMismatch` | no |
| 4 | `SpotCreateFailed` | no |
| 5 | `SpotRouteNotFound` | no |
| 6 | `SpotTypeMismatch` | no |
| 7 | `ActorSessionNotBound` | no |
| 8 | `HandlerNotFound` | no |
| 9 | `RouteHandlerNotFound` | no |
| 10 | `ActorDispatchHandlerNotFound` | no |
| 11 | `PayloadDecodeFailed` | no |
| 12 | `RouteNotConnected` | yes |
| 13 | `RequestTargetNotFound` | no |
| 14 | `RequestRejected` | no |
| 15 | `RequestProtocolError` | no |
| 16 | `RequestFailed` | no |
| 17 | `WorkerQueueFull` | no |
| 18 | `WorkerTimedOut` | no |
| 19 | `WorkerFailed` | no |
| 20 | `ActorLocationStale` | yes |
| 21 | `ActorCreateRejected` | no |

기본 재시도 값은 kind별 공통 정책이다. `RouteNotConnected`는 아직 route가 연결되지 않은
수렴 창일 수 있고, `ActorLocationStale`은 actor 위치가 바뀐 직후의 bounded retry 대상이다.
그 밖의 kind는 기본적으로 같은 요청을 즉시 반복해도 의미 있는 진전이 없다고 본다.

### 2.4 transport 통합 축

framework가 직접 통합할 transport 축은 [01-overview.ko.md](01-overview.ko.md)의
section 2에 정의되어 있다. 이 문서는 channel messaging, `PUB/SUB`, `STREAM`
세 축을 중심으로 보되, 공통 API 원칙과 lifecycle 경계에 직접 영향을 주는
`SPOT` 표면도 함께 다룬다. `SPOT`의 자세한 계약과 샘플은
[.NET SPOT 문서](languages/dotnet/01-system-structure.ko.md) 등 별도
문서에서 따로 다룬다.

핵심은 transport 축은 명확히 두되, 프레임워크 사용자가 보는 이름은 socket
이름보다 역할 이름이 되게 만드는 것이다.

transport 축마다 wire shape도 섞으면 안 된다.

- channel, routed channel, SPOT channel, internal actor dispatch, internal session
  proxy는 서버 간 framework message이므로 multipart `header + payload`를 사용한다.
- STREAM은 session 연결 위의 packet transport이므로 단일 stream packet 안에
  stream header/payload frame을 넣는다.

이 구분은 모든 언어 adapter에 적용된다. 언어별 serializer나 framework DI 모양이
달라도, 서버 간 payload를 JSON envelope의 필드로 넣어 다시 인코딩하는 방식은 이
정책에 맞지 않는다.

### 2.4.1 transport 연결 책임

framework는 이미 만든 zlink socket connection의 재연결 기능을 직접 구현하지 않는다. 연결이 끊겼을
때 다시 연결을 시도하는 일은 core/binding socket의 책임이다. framework는 socket option을 전달하고,
location store나 topology가 바뀌었을 때 어떤 endpoint를 연결 대상으로 둘지 갱신한다.

따라서 framework runtime은 disconnected event를 보고 별도 reconnect loop, timer, backoff를 만들지
않는다. 재시도 설정으로 transport 버그를 가리거나, 같은 요청을 반복해서 성공처럼 보이게 만들면 안
된다. 허용되는 대기는 startup 또는 topology 수렴 대기다. 예를 들어 client/runner는 대상 server가
location store에 나타나고 channel이 준비될 때까지 기다릴 수 있지만, server끼리 특정 순서로 떠야만
동작하는 구조는 public contract에 맞지 않는다.

socket connect 직후 아직 submit 준비가 끝나지 않은 짧은 구간은 readiness 대기로만 다룬다. 이 대기는
요청 timeout 안에서 끝나야 하며, payload decode 실패, handler 오류, protocol 오류, 이미 끊긴
connection의 복구를 반복 호출로 가리면 안 된다.

이미 받은 route request의 reply도 같은 원칙을 따른다. handler가 만든 reply는 새 요청으로 재시도하지
않고, 같은 route channel의 connected/peer-ready 상태를 확인한 뒤 한 번 submit한다. 이 확인은 server
구동 순서를 고정하는 장치가 아니라, zlink socket이 이미 연결 대상으로 가진 peer에 reply를 내보낼 수
있는지 확인하는 readiness 대기다.

RouteMesh의 모든 구성원은 endpoint 유무와 관계없이 router row 하나를 게시한다.
endpoint가 없는 router는 수신 endpoint가 없으므로 pairwise 순서와 관계없이 endpoint가
있는 remote router를 항상 dial한다. 양쪽 router에 endpoint가 있으면 pairwise initiator
규칙으로 정한 한쪽만 dial한다. RouteMesh에 dealer row를 게시하거나 router/dealer 역할을
동시에 게시하는 구성은 유효하지 않다. framework는 이전 dealer row를 읽는 호환 경로나
socket 역할을 실행 중에 바꾸는 이중 역할을 두지 않는다.

drain은 새 요청의 선택 대상에서 빠지는 의미다. 이미 받은 request의 handler 실행과 reply는 drain
이후에도 완료되어야 한다. weight 변경, endpoint handover, owner 변경을 같은 사건으로 다루면
in-flight request가 끊길 수 있으므로, adapter는 weight-only 변경과 endpoint identity 변경을 구분해야
한다.

`PUB/SUB` 자동 연결 방향도 transport 책임 경계에 포함된다. subscriber 역할은 publisher endpoint를
발견해 connect한다. publisher와 subscriber 양쪽이 같은 public endpoint를 동시에 connect하는 모델을
공통 framework 계약으로 보지 않는다. publisher는 자신이 여는 endpoint를 location store에 게시하고,
subscriber는 그 row를 보고 연결한다.

### 2.4.2 runtime monitoring

운영 이벤트는 일반 request/send/event handler와 다른 성격을 가진다. 따라서
framework는 monitoring 표면을 별도 축으로 설명하는 편이 맞다.

- event kind는 enum으로 둔다.
- 실제 callback payload는 source 이름과 상세 정보를 함께 가진 구조화된 값으로
  둔다.
- socket source는 하부 monitor를 감싸는 편이 자연스럽다.
- location runtime/spot source는 raw monitor를 가장한 표면보다, 일정 주기로 상태를 읽고 직전 상태와
  비교해 바뀐 때만 event를 만드는 방식으로 설명하는 편이 맞다([location runtime](40-location-runtime.ko.md) §9).
- application은 typed runtime event handler를 구현해서 이 이벤트를 받는 모델을
  기본으로 본다.

framework는 모든 source를 같은 raw monitor API로 보이게 하지 않고,
source별 구현 차이를 숨긴 typed runtime event surface를 제공하는 편이 더
자연스럽다.

### 2.4.3 message dispatch error observer

framework message dispatch 단계에서 등록되지 않은 packet, payload decode 실패, handler 예외,
invalid frame 을 만나면 언어별 runtime 은 같은 의미의 dispatch error event 를 만든다. 이 event 는
전역 observer 하나로만 전달한다. channel 별 또는 spot 별 observer 는 이 버전의 공개 계약이 아니다.
사용자가 특정 channel, topic, spot, actor 만 보고 싶으면 event 안의 context 필드로 직접 필터링한다.

**이 절은 dispatch error event의 공통 스키마를 소유한다.** 어느 경로가 어느 결과로 끝나는지는
[channel 메시징 §3](11-channel-messaging.ko.md), [SPOT 메시징 §5](20-spot-messaging.ko.md),
[STREAM 서버 세션](30-stream-session.ko.md)이 소유하며, 결과는 세 가지다.

| `action` | 언제 |
|---|---|
| `ReplyError` | reply path가 있는 request — **error reply로 끝난다** |
| `FailCaller` | **reply frame이 없는 경로**(같은 process 안의 local actor 호출 등) — caller의 future·promise·task를 **framework 오류로 완료한다** |
| `Drop` | send·publish·subscription·actor send 같은 **one-way** — reply를 만들 수 없으므로 drop한다 |

**세 경우 모두 기본 로그와 metric/counter, observer event를 남긴다.**

event 는 원본 native frame 이나 message ownership 을 노출하지 않는 불변 snapshot 이다. 공통 의미는
아래 필드를 가진다.

| 필드 | 의미 |
|------|------|
| `surface` | `Channel`, `RouteMeshChannel`, `SpotRoute`, `SpotSubscription`, `SpotActor`, `StreamSession` |
| `messageKind` | `Request`, `Send`, `Publish`, `ActorRequest`, `ActorSend`, **`Response`**, **`Error`** — reply 흐름의 실패도 기록한다 |
| `reason` | `HandlerMissing`, `PayloadDecodeFailed`, `HandlerException`, `InvalidFrame`, `ReplyPathMissing`, **`UnexpectedReply`** |
| `action` | `ReplyError`, `FailCaller`, `Drop` — [§dispatch 실패 정책](11-channel-messaging.ko.md) 참조 |
| `packetName` | packet/message 이름. 알 수 없으면 언어별 null/optional 값 |
| `channelName` | channel 또는 route mesh channel 이름 |
| `topic` | publish/subscription topic |
| `spotRid` | SPOT routing id |
| `actorId` | actor id |
| `sourceRid` | routing source id |
| `correlationId` | request correlation id 또는 sequence |
| 오류 정보 | decode 실패나 handler 예외. **예외 객체를 그대로 노출할 의무는 없다** — 언어에 따라 오류 타입과 메시지 문자열로 투영할 수 있다. handler 없음에는 값이 없을 수 있다 |

observer 등록 여부와 관계없이 기본 로그와 metric/counter 는 남아야 한다. 다만 message flow
trace mode 를 `off` 로 두면 **로그만 침묵**하고 metric/counter 와 observer 통지는 계속 발생한다
([52 메시지 흐름 추적 §2](52-message-flow-tracing.ko.md)). observer callback 실패는
별도 error sink 나 내부 로그로만 기록하고 dispatch loop, error reply 전송, shutdown 을 깨지 않는다.

### 2.5 public contract와 runtime 구현의 분리 기준

이 기준은 모든 framework 언어에 적용한다. 언어마다
package, module, namespace, file layout 관례는 다를 수 있지만, 사용자에게 보이는
public 계약과 내부 runtime 구현을 분리한다는 원칙은 동일하다.

각 binding 구현은 사용자에게 보이는 public 계약을 별도 `Contracts` 폴더나 그와 같은
역할의 위치에 모을 수 있다. 이 위치는 사용자가 읽어야 하는 표면을 보여 주기 위한
공간이다. 따라서 내부 구현체, 내부 policy, 내부 registration record처럼 framework만
알아야 하는 타입을 이 위치에 두면 안 된다.

framework project의 최상위 구조는 가능한 한 public 계약 영역과 runtime 구현 영역 두
축으로 시작한다. `.NET`처럼 폴더와 namespace를 명확히 나누기 쉬운 언어에서는
`Contracts`와 `Runtime` 이름을 우선한다. 다른 언어에서는 같은 뜻을 가진 package,
module, namespace, include 디렉토리로 대응한다. 예를 들어 `api`/`runtime`,
`public`/`internal`, `include`/`src`처럼 언어 생태계에서 더 자연스러운 이름을 써도
되지만, public 계약과 내부 구현이 같은 위치에 섞이면 안 된다.

`Contracts` 또는 그와 같은 public 계약 영역은 사용자와 binding 개발자가 직접 봐야
하는 public 표면이다. `Runtime` 또는 그와 같은 내부 구현 영역은 framework가 public
계약을 실행하기 위해 사용하는 구현이다. codec policy, handler scanner, dispatch
queue, registration validator, message codec처럼 내부 실행을 돕는 타입은 별도 최상위
폴더를 만들지 말고 runtime 구현 영역 아래에 둔다.

public 계약 영역에 둘 타입은 아래 범위로 제한한다.

- 사용자가 구현해야 하는 handler, session, actor, spot, resolver, policy interface
- framework가 발급하고 사용자가 호출하는 client, manager, context, call, handle, view
  interface
- 사용자가 직접 만들거나 저장해도 자연스러운 값 객체, event payload, error payload,
  route snapshot, option 값
- attribute, enum, exception처럼 public 계약을 설명하는 보조 타입
- concrete 구현을 숨기기 위한 public factory. 단 factory의 반환 타입은 가능한 한
  interface나 값 객체여야 한다.

반대로 아래 타입은 public 계약 영역에 두지 않는다.

- framework 내부 구현 class
- public interface를 구현하는 기본 구현체
- socket, codec, dispatch, routing, location runtime 같은 내부 정책 class
- runtime을 직접 만들거나 시작하는 host class와 start 함수
- runtime 상태를 담는 record
- 특정 binding 내부 테스트나 샘플만 편하게 하기 위한 helper

framework runtime은 application이 직접 시작하는 public contract로 노출하지 않는다.
각 언어 adapter는 해당 언어의 host lifetime에 runtime 시작과 종료를 묶는다. `.NET`은
`IHostedService`, Java는 Spring `SmartLifecycle`, Node.js는 NestJS lifecycle hook,
C++는 zlink framework `app_t` host가 이 역할을 맡는다. 테스트나 adapter 내부 구현이
runtime 객체를 직접 만들 수는 있지만, guide와 public entrypoint에서는 이 경로를
사용자 실행 방법으로 설명하지 않는다.

언어별로 경계를 막는 방식은 아래 기준을 따른다. 모든 언어에서 같은 수준의 접근 제한을
문법만으로 강제할 수는 없으므로, 각 생태계에서 실제로 유지 가능한 가장 강한 장치를
사용한다.

| 언어 | application이 보는 표면 | runtime 접근 제한 |
|------|--------------------------|-------------------|
| `.NET` | `Zlink.Framework` public contract와 `Zlink.Framework.AspNetCore` extension | runtime class는 `internal`로 둔다. application은 DI로 등록된 public service만 받는다. |
| `Java` | `zlink-framework-core` public contract와 Spring Boot starter bean | direct start facade와 public constructor/start 함수를 두지 않는다. Spring `SmartLifecycle`이 runtime을 시작한다. JPMS 또는 artifact 분리가 없는 classpath 환경에서는 class 이름 자체를 완전히 숨길 수 없으므로, public entrypoint와 guide에서 runtime 생성 경로를 제공하지 않는다. |
| `Node.js` | `@zlink-systems/framework` root export와 `@zlink-systems/nestjs` module/token | package `exports`는 root entrypoint만 공개한다. NestJS adapter는 내부 workspace 경로로 runtime 구현을 읽고, application은 `dist/runtime/*` 또는 `dist/internal` package subpath를 import하지 않는다. |
| `C++` | 설치되는 `framework/include` public header와 `app_t` host | runtime header와 `src/runtime/*`는 설치하지 않고 target private input으로 둔다. public header는 runtime header를 include하지 않는다. |

따라서 application 예제와 샘플은 항상 host framework의 시작점을 사용한다. Java 샘플은
Spring Boot application context, Node.js 샘플은 NestJS application context, `.NET`
샘플은 해당 언어의 표준 host와 lifecycle 관례를 기준으로 작성한다.

public 타입을 interface로 둘지 concrete 값 객체로 둘지는 타입이 가진 도메인 의미를
기준으로 판단한다. 필드 수가 적다는 이유만으로 값 객체로 보고, 구현 클래스가 있다는
이유만으로 interface로 숨기지 않는다.

이 판단도 모든 언어 adapter에 동일하게 적용한다. Java의 `interface`, TypeScript의
`interface` 또는 `type`, Python의 `Protocol`, C++의 abstract class처럼 표현 방식은
달라도 의미 기준은 같다. framework가 발급한 handle과 view는 추상 표면으로 숨기고,
사용자가 직접 만들고 보관하는 데이터는 concrete 값 객체로 둔다.

interface가 맞는 경우는 아래와 같다.

- framework가 생성하거나 발급하고 application은 그 표면만 사용하는 handle, view,
  context, call builder다.
- 내부 routing, lifecycle, native resource, lazy decode, 실행 문맥 같은 구현 의미가
  함께 포함되어 있다.
- application이 callback이나 handler로 구현해서 framework가 호출하는 계약이다.
- 여러 구현이 자연스럽거나, binding별 구현 차이를 숨겨야 한다.
- 사용자가 직접 생성해서 저장하는 값이 아니라, framework 실행 흐름 안에서만 의미가
  분명하다.

예를 들어 actor 참조는 겉으로 `actorId`, `actorType`만 가진 값처럼 보여도 단순한
DTO가 아니다. actor 참조는 framework가 발급한 actor handle이며, 내부적으로 현재
session bind, actor dispatch 대상, routing 선택과 연결될 수 있다. 따라서
`ActorRef` 계열은 concrete 값 객체보다 interface로 두는 편이 맞다.

stream session 입력도 비슷하다. stream에서 막 도착한 frame은 framework가 decode한
header와 native payload message의 쌍으로 application handler에 전달된다. payload message의
수명과 decode 정책은 값 객체가 아니라 callback 호출 범위의 계약으로 다룬다.

concrete 값 객체가 맞는 경우는 아래와 같다.

- 값의 구조와 전달 자체가 public 의미다.
- 사용자가 직접 만들고, 복사하고, 저장해도 자연스럽다.
- 내부 저장 방식이 바뀌어도 public 의미가 거의 변하지 않는다.
- interface로 바꾸면 factory, builder, cast 같은 보조 API가 늘어나고, 호출자가 알아야
  할 것이 오히려 많아진다.

예를 들어 metadata, route snapshot, location row, monitoring event payload, error
payload, option 값처럼 작은 구조화 데이터는 concrete record, class, struct로 두는 편이
낫다. 내부 저장소가 dictionary에서 배열이나 immutable collection으로 바뀌더라도,
사용자에게 중요한 것은 "어떤 값을 표현하는가"이지 "어떤 구현체인가"가 아니다.

정리하면 아래 기준을 따른다.

- public 계약 영역과 runtime 구현 영역은 모든 언어 adapter에서 분리한다.
- 언어 관례상 이름이 달라도 `Contracts`는 public 계약, `Runtime`은 내부 구현이라는
  역할을 유지한다.
- 동작, handle, view, context, call, handler, lifecycle, codec 확장점은 interface를
  우선한다.
- 데이터, snapshot, event payload, route value, error value, metadata value는 concrete
  값 객체를 우선한다.
- 값처럼 보여도 framework가 발급한 handle이거나 native resource 수명과 연결되어 있으면
  interface로 둔다.
- concrete 값 객체를 public으로 둘 때도 내부 저장 방식은 숨긴다. public 필드나 mutable
  collection을 그대로 노출하지 않는다.

### 2.6 Handler filter

**handler filter는 framework가 소유하는 dispatch 계약이다.** 언어별 이름은 달라도 의미는 같다.

| 축 | 계약 |
|---|---|
| **등록** | framework 등록 루트에서 filter 타입을 등록한다 |
| **실행 순서** | **등록 순서대로 바깥에서 안쪽으로** 실행한다. 마지막 안쪽이 handler다 |
| **호출 표면** | filter는 **invocation**(메시지와 context)과 **`next`**(다음 단계)를 받는다 |
| **`next`의 의미** | **`next`를 호출해야 다음 filter 또는 handler가 실행된다.** 호출하지 않으면 파이프라인이 거기서 끝나고 filter가 결과를 대신 만든다 |
| **결과** | filter는 `next`의 결과를 그대로 돌려주거나 **바꿔서 돌려줄 수 있다** |
| **생성** | filter는 **그 dispatch의 DI scope에서 resolve한다.** handler와 같은 scope다 |

**AOP와 구분한다.** AOP는 handler가 주입받는 **서비스 계층**에 적용하고, filter는 **dispatch
파이프라인 자체**에 적용한다.

**적용 범위:** filter는 **channel dispatch 경로**(request·send·publish)에 적용한다. **SPOT
handler, STREAM session handler, route-mesh handler는 filter 파이프라인을 거치지 않는다** — 이
경로들은 spot 실행 문맥과 session 실행 문맥이 소유하는 별도 dispatch다. **filter를 이 경로까지
넓히려면 공개 계약을 먼저 확장해야 한다.**

## 3. ASP.NET Core 방향

### 3.1 기대하는 표면

- `AddZLinkFramework(...)`
- `options.AddHandlersFromAssemblyOf<...>()` 또는 그와 비슷한 handler assembly 등록
- outbound client DI
- runtime monitoring 등록
- `SPOT` node / publisher / subscriber의 hosted lifecycle 통합
- stream hosted lifecycle 또는 stream session 등록

### 3.2 예시

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddClientServerChannel("profile")
        .EnableServer("tcp://0.0.0.0:7101");

    options.AddFanoutChannel("profile.events")
        .EnableSubscriber();

    options.AddClientServerChannel("account")
        .EnableClient("tcp://10.0.20.15:7101");
    options.AddHandlersFromAssemblyOf<Program>();
});

public sealed class ProfileHandlers
{
    [ZLinkRequest]
    public ValueTask<ProfileReply> GetAsync(
        GetProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new ProfileReply());
    }
}
```

이 예시에서 중요한 점은 handler가 raw header part를 직접 받지 않는다는 점이다.
필요한 metadata는 `ZLinkRequestContext` 같은 context에서 조회한다.

또한 framework는 startup 시점에 channel별 역할을 등록하고, 필요한 역할만
여는 쪽을 기본 방향으로 본다.

### 3.3 Handler 등록 정책

handler 등록은 session, node/channel, Spot 에서 같은 원칙을 따라야 한다. 사용자는
handler가 어느 실행 문맥에 속하는지만 알면 되고, packet 이름, actor 타입, request/send
종류처럼 handler 타입에서 알 수 있는 정보를 등록 호출부에 반복해서 적지 않아야 한다.

수동 등록은 handler를 소유한 실행 문맥의 `Configure()` 단계에서 한다. node/channel
handler는 application startup 의 channel builder가 실행 문맥이므로 startup 구성에서
등록한다. session handler는 session 객체의 `Configure()`에서, Entry Spot과 user Spot
handler는 각 Spot 객체의 `Configure()`에서 등록한다.

session handler 수동 등록은 session 객체 안에서 아래처럼 표현한다.

```csharp
public void Configure()
{
    Context.Handlers.AddHandler<AuthenticateHandler>();
    Context.Handlers.AddHandler<JoinHandler>();
}
```

Spot 메시지 handler 수동 등록은 Spot 객체 안에서 아래처럼 표현한다. actor request/send와
Spot packet handler는 `AddHandler<THandler>()` 하나로 등록한다 — handler가 구현한 typed
interface에서 종류와 메시지 타입을 추론하기 때문이다. **subscription은 topic이 필요하므로
`AddSubscribe<THandler>(topic)`으로 등록한다.** 같은 topic을 선언적 metadata로 제공하는 handler는
자동 등록으로도 붙는다. timer는 메시지 dispatch handler가 아니며, timer 이름과 주기처럼 실행
계획에 속한 값이 필요하므로 별도 timer 등록 API를 사용한다.

```csharp
public void Configure()
{
    Context.Handlers.AddHandler<JoinActorHandler>();               // actor request/send handler
    Context.Handlers.AddSubscribe<DomainEventHandler>("domain.events");  // subscription: topic이 인자다
}

// 자동 등록 경로에서는 같은 topic을 선언적 metadata로 준다.
[ZLinkSpotSubscriptionHandler("domain.events")]
public sealed class DomainEventHandler :
    IZLinkSpotSubscriptionHandler<DomainSpot, DomainEvent>
{
    // ...
}
```

`AddHandler<THandler>()` 는 기본 등록 표면이다. handler가 구현한 typed interface에서
session context, Spot 타입, actor 타입, 메시지 타입, request/send/subscription 종류를
추론한다. packet 이름은 메시지 타입에서 정한다. 메시지 타입 이름과 다른 packet 이름이 필요한
경우에만 handler metadata나 `AddHandler<THandler>("PacketName")`처럼 packet 이름 override를
사용한다.

아래처럼 handler 타입에서 이미 알 수 있는 정보를 반복해서 받는 API는 표준 표면으로 두지
않는다.

```csharp
Context.Handlers.AddActorRequest<JoinHandler, PlayerActor>("JoinReq");
```

위 형태는 actor 타입, request/send 종류, packet 이름을 호출부가 다시 알아야 하므로 handler
등록 표면을 얕게 만든다. 같은 의미는 아래처럼 표현한다.

```csharp
Context.Handlers.AddHandler<JoinHandler>();
Context.Handlers.AddHandler<StateHandler>();
```

**subscription topic은 예외다.** topic은 handler interface에서 추론할 수 없는 값이므로,
등록 호출의 인자로 받는 `AddSubscribe<THandler>(topic)` 형태를 정식 표면으로 둔다. 같은 topic을
선언적 metadata로 제공하는 경로도 함께 지원한다(아래 참조).

자동 등록은 assembly, module, package scan 으로 handler 후보를 찾는 기능이다. 자동 등록도
수동 등록과 같은 추론 규칙을 사용한다. interface 기반 handler는 attribute 없이도 자동 등록
대상이 될 수 있어야 한다. attribute는 handler interface에서 알 수 없는 추가 metadata를
제공하거나, method 기반 handler를 선언할 때 사용한다.

자동 등록은 기본값으로 켠다. 이때 runtime은 사용자가 명시한 handler scan 범위뿐 아니라
등록된 session, Entry Spot, user Spot, actor factory, 명시 channel handler 같은 application
타입의 assembly/module/package도 scan 범위로 볼 수 있다. 그래야 샘플과 일반 application이
handler class를 만든 뒤 별도 등록 호출을 반복하지 않아도 된다.

다만 같은 테스트 assembly 안에 같은 실행 문맥과 같은 packet key를 가진 대체 handler를 여러
개 둔 fixture처럼, 자동 등록이 의도하지 않은 후보까지 발견하는 경우가 있다. 이런 경우에는
handler 타입이나 메시지 타입의 추론 규칙을 임시로 바꾸지 않는다. 대신 해당 host에서 implicit
handler auto registration을 끄고, 필요한 handler만 `Configure()` 또는 명시 builder 호출로
등록한다. 명시적으로 추가한 assembly/module/package scan은 사용자가 의도한 등록 범위이므로
implicit auto registration을 꺼도 유지할 수 있어야 한다.

C++ framework는 이 자동 등록 원칙의 예외다. C++은 runtime reflection 기반 scan을 전제로
하지 않고 compile-time 타입과 명시 builder 호출을 기준으로 handler를 등록한다. 따라서 C++
샘플과 E2E는 같은 메시지, 같은 handler 책임, 같은 충돌 검증 규칙을 유지하되 handler 발견과
등록은 C++ public builder 표면에 맞게 명시한다.

아래 표의 자동 등록 key는 C++을 제외하고 assembly, module, package scan 을 제공하는 언어에
적용한다.

| 영역 | 수동 등록 위치 | 자동 등록 key | attribute 역할 |
|------|----------------|---------------|----------------|
| node/channel | startup channel builder | channel + kind + packet name | handler group, method handler 선언 |
| session | session `Configure()` | session type + packet name | method handler 선언 또는 packet name override |
| Entry Spot | Entry Spot `Configure()` | Entry Spot type + actor/packet key | method handler 선언 또는 topic/timer metadata |
| user Spot | user Spot `Configure()` | Spot type + actor/packet key | method handler 선언 또는 topic/timer metadata |

channel handler는 한 assembly 안의 handler 후보를 여러 channel 중 어디에 노출할지 정해야
하므로 handler group 또는 명시 channel 등록이 필요하다. Spot handler는 handler interface의
Spot 타입이 실행 문맥을 정하므로 별도 group 없이 해당 Spot에 붙는다. session handler는
session 타입이 실행 문맥을 정한다. 언어별 runtime이 session 타입을 안정적으로 알 수 없는
경우에만 context 타입을 보조 key로 사용할 수 있으며, 이 경우에도 한 실행 문맥 안에서 같은
packet 이름이 둘 이상 등록되면 startup에서 실패해야 한다.

subscription topic은 handler interface만으로 알 수 없는 유일한 dispatch key다. 따라서 **두 경로를
모두 제공한다**: 수동 등록에서는 `AddSubscribe<THandler>(topic)`처럼 topic을 등록 호출 인자로
받고, 선언적 경로에서는 attribute·annotation·decorator가 topic을 제공한다. 두 경로는 같은
subscription registry로 수렴하며, 같은 topic에 대해 서로 다른 handler가 중복 등록되면 startup
validation 오류다. timer 이름과 주기는 메시지 handler metadata가 아니라
timer 실행 계획이므로 `AddTimer<THandler>(name, period)`처럼 timer 등록 API에서 제공한다.
반대로 actor send/request handler처럼 interface가 actor 타입, 메시지 타입, request/send
종류를 모두 제공하는 경우 attribute를 필수로 요구하지 않는다.

자동 등록과 수동 등록이 같은 dispatch key를 만들면 startup validation 오류로 처리한다.
같은 handler 타입을 같은 key에 두 번 등록한 경우도 중복으로 본다. 조용히 덮어쓰거나
수동 등록이 자동 등록을 대신하게 만들지 않는다. 오류 메시지는 충돌한 key, 자동/수동 등록
출처, handler 타입을 함께 보여 주어야 한다. explicit scan 범위와 implicit scan 범위가 같은
assembly/module/package를 가리키는 경우에는 scan source 자체를 먼저 de-duplicate해서 같은
handler 후보를 두 번 만들지 않는다. 그래도 서로 다른 handler가 같은 key를 만들면 충돌로
처리한다. 자동 등록과 수동 등록을 섞어야 하면 channel/session/Spot 처럼 scan 범위를 나누는
명시 API를 제공하거나, 필요한 host에서 implicit handler auto registration을 끈다.

수동 연결을 둘 때는 `channel` 전체가 아니라 `channel + capability` 기준으로
설정해야 한다. 예를 들어 `account.client` 수동 연결과 `account.subscriber`
수동 연결은 별도 집합으로 본다. 수동 연결 역할은 startup
설정만이 아니라, 런타임 `Connect`, `Disconnect`, `ListConnections` 같은 제어도
지원해야 한다.

여기서 channel client manual 연결은 remote `RoutingId`를 따로 받지 않는 편이
자연스럽다. 하부 `DEALER(client)`가 connect된 peer 집합으로 요청을 보내는
모델이므로, startup과 런타임 제어 모두 endpoint 집합만 관리하면 된다.

transport 재접속은 framework가 별도 기능으로 다시 구현하지 않는다. 하부 zlink socket이
이미 연결 단절 후 재접속, backoff, peer handover 같은 socket 수준의 동작을 담당한다.
framework가 같은 endpoint를 주기적으로 disconnect/connect 하거나 자체 재접속 loop를 두면
socket 내부 상태와 location runtime의 desired connection 상태가 서로 다른 결정을 내릴 수
있다. framework가 해야 할 일은 공개 설정과 location row를 바탕으로 "연결되어 있어야 하는
endpoint 집합"을 계산하고 socket에 전달하는 것이다. 이미 전달한 endpoint의 실제 재접속은
socket 책임으로 둔다.

따라서 framework 구현에는 transport reconnect manager, reconnect timer, reconnect retry
queue, 주기적인 disconnect/connect loop를 두지 않는다. framework가 socket에 다시 connect를
요청할 수 있는 경우는 desired endpoint 집합 자체가 바뀌었을 때뿐이다. 예를 들어 location
row가 추가되었거나 제거되었을 때, 또는 사용자가 수동 연결 설정을 바꾸었을 때만 socket에
반영한다. 같은 endpoint가 이미 desired set에 남아 있다면 연결이 끊겼더라도 framework는
그 endpoint를 다시 연결하려고 반복 호출하지 않고 socket 내부 재접속 정책에 맡긴다.

따라서 연결 수렴이 늦거나 첫 요청이 실패하는 문제를 framework-level retry, sleep, 또는
재접속 loop로 가리지 않는다. 자동 연결이면 location runtime이 desired set을 갱신하고,
수동 연결이면 사용자가 설정한 endpoint 집합을 유지한다. 둘 다 같은 endpoint에 대해
framework가 임의로 연결을 끊은 뒤 다시 연결하는 방식으로 복구를 시도하면 안 된다. 연결이
없거나 아직 수렴하지 않았을 때의 오류는 정해진 public error로 드러나야 하며, 실제 transport
재접속은 zlink socket의 책임이다.

또한 send는 기본 one-way submit으로 둔다. 구현은 blocking send를 task로 감싸지
않고, 먼저 nonblocking send를 시도한 뒤 temporary backpressure가 발생하면 pending
send queue와 ready notification으로 이어서 처리한다. send 대기 한계는 application
호출부가 아니라 framework 기본값 또는 socket의 `SendTimeout` 옵션을 따른다.
framework 기본값은 core socket 기본 send timeout과 같은 1000ms로 맞춘다.
각 binding은 개별 socket option이 있으면 그 값을 우선 사용하고, 없으면
framework 기본값을 async pending deadline으로 사용한다. framework 기본 send
timeout을 명시적으로 비우는 언어에서는 무한 대기로 본다.
publish도 send와 같은 내부 submit 규칙을 따른다. subscriber 처리 완료를 기다리지 않고,
local publish transport에 메시지를 맡길 수 있을 때까지 비동기로 기다린다.

request도 reply를 기다리는 async 호출로 설명한다. 다만 request packet을 보내는
단계는 send와 같은 내부 submit 경로를 사용해야 한다. `Timeout(...)`은 reply
대기 시간만 정하고, 전송 backpressure는 `SendTimeout` 정책이 처리한다.
request/reply 기본 대기 시간은 framework 전역 기본값 30초다. 호출별 timeout이
있으면 가장 먼저 적용하고, 없으면 channel별 기본 request timeout을 적용하며,
channel 설정도 없을 때 전역 기본값을 사용한다.

고성능 구현에서는 immediate send/publish 성공 path가 allocation 없이 완료되어야
한다. backpressure path는 bounded pending queue를 사용하고, ready notification마다
정해진 batch budget 안에서 queue를 drain한다. 이렇게 해야 thread blocking 없이도
높은 처리량을 유지할 수 있다.

보다 자세한 `.NET` public contract는
[.NET 언어별 스펙](languages/dotnet/README.ko.md)을 참고한다.

### 3.4 ASP.NET Core의 SPOT 방향

`SPOT`은 일반 channel messaging보다 instance lifecycle과 실행 문맥이 더 먼저
보이는 표면이다. 공통 정책 차원에서는 아래 정도만 고정한다.

- active SPOT channel view는 `AddSpotMesh(channelName)`가
  정한다. SpotMesh 등록 하나가 같은 프로세스의 단일 SpotNode를 나타내며, 새 샘플은
  mesh 등록을 기준으로 작성한다.
- `SpotNode`는 router, pub/sub, route bridge 기반 외부 호출 역할을 가진다.
- local spot 인스턴스는 등록 이름으로 만들고, lifecycle 안에서 packet, subscribe,
  timer를 등록한다.
- spot timer 는 framework 가 만든 managed scheduler 를 사용한다. user Spot timer 는
  같은 user Spot 실행 queue 에서 직렬화한다. Entry Spot timer 는 lifecycle, route,
  subscription 같은 Entry callback과 동일한 Entry 실행 줄에서 직렬화한다. Entry actor
  packet은 이 실행 줄이 아니라 actor별 mailbox에서 처리한다.
- timer handler 는 callback 번호, 예정 시각, 시작 시각, 지연, 건너뛴 tick 수를
  담은 metadata 를 받는다. 늦은 tick 은 skip, bounded catch-up, fixed-delay 중
  하나의 정책으로 처리한다. hard realtime 보장은 제공하지 않는다.
- local spot이 없는 외부 노드용 publish 표면은 별도 client로 분리할 수 있다.
- actor/session 모델을 지원하는 binding에서는 actor가 `Spot`에 attach된 뒤의
  actor dispatch를 반드시 해당 `Spot` 실행 문맥에서 처리한다. stream session은
  ingress 역할을 하고, room/stage 같은 domain 상태를 만지는 코드는 `Spot` 실행
  문맥으로 들어가야 한다.
- actor join으로 현재 `Spot`이 바뀌면, join 완료 뒤의 actor dispatch는 새 `Spot`
  실행 문맥에서 처리되어야 한다. framework는 join 상태 갱신과 packet dispatch
  선택 사이의 경합을 막아야 한다.
- actor context는 현재 user Spot의 식별값, 현재 actor에 연결된 bound session,
  `JoinSpot(...)`, `JoinEntrySpot(...)`을 제공한다. actor가 channel client, route bridge
  socket 또는 stream 객체를 직접 고르지 않게 한다.
- bound session은 현재 client로 보내는 one-way `Send(...)`와 연결 종료만 제공한다.
  client를 향한 request/reply API나 session 위치 조회 API는 제공하지 않는다.
- actor를 완전히 제거하는 public API는 Entry Spot context에만 둔다. user Spot
  context는 actor를 Entry Spot으로 되돌리는 `leaveActor` 의미의 API까지만 제공한다.
  application은 room/game/stage 정리가 끝난 뒤 user Spot에서 actor에 종료 표시를 남기고
  `leaveActor`로 Entry Spot으로 이동한다. Entry Spot handler 또는 lifecycle callback은
  그 표시를 확인한 뒤 언어별 Entry Spot destroy API를 호출한다.
- Entry Spot destroy API는 actor registry, actor-session binding, native actor ref를
  함께 정리한다. 이 작업은 actor 위치 이동이 아니므로 `onLeaveActor`나 다른 lifecycle
  callback을 추가로 호출하지 않는다.
- stream disconnect는 현재 session binding cleanup과 `onDisconnectActor` 의미만 가진다.
  disconnect cleanup만으로 actor destroy가 실행되지 않는다. actor 수명 종료는 위의
  Entry Spot destroy 경로에서만 application이 명시적으로 선택한다.

자세한 contract와 샘플은
[.NET SPOT 문서](languages/dotnet/01-system-structure.ko.md)
같은 binding 문서를 기준으로 본다.

#### 3.4.1 Actor lifecycle과 core 위임

framework는 actor 생성, Spot 입장, 이탈과 actor 메시지 수신을 core actor 기능에
위임해야 한다. 별도의 actor registry나 wire protocol을 공개 계약으로 중복 구현해서는
안 된다.

새 actor는 location runtime의 `NewClaim`을 먼저 얻은 실행만 활성화한다. framework는
claim 성공 뒤 factory, `Configure()`, create lifecycle 순서로 진행하며, 같은 actor id의
동시 생성에서 claim을 얻지 못한 실행은 application actor를 활성화하지 않는다
([location runtime §4](40-location-runtime.ko.md#4-ownergeneration-규칙)).

입장 요청을 받으면 framework는 요청을 application join handler에 전달하고, handler의
허용 또는 거부 결과와 선택적 reply를 core 응답으로 변환한다. 입장이 완료된 actor의
메시지는 해당 Spot의 직렬 실행 문맥에서 dispatch한다. actor join 요청과 actor 메시지
수신 준비는 등록된 handler 유무와 관계없이 Spot 초기화 때 설정해야 한다.

actor 생성, 입장과 이탈의 정확한 함수 이름, timeout 표현과 취소 인자는 언어별 스펙이
고정한다. 공통 계약은 다음 관찰 가능한 결과를 요구한다.

- 같은 actor id를 중복 생성할 때의 결과가 언어별 오류 계약과 일치한다.
- join handler가 완료되기 전에 actor가 해당 Spot의 메시지를 처리하지 않는다.
- leave가 완료된 뒤에는 이전 Spot 실행 문맥에서 새 actor 메시지를 처리하지 않는다.
- actor 제거는 Entry Spot의 명시적 destroy 경로에서만 수행한다.

##### 실행 문맥 보장

user Spot 에서는 두 이벤트를 spot serial executor를 통해 직렬화된 실행 문맥 안에서
처리하므로, actor join handler와 actor packet handler 사이에 동시성 경합이 없다.
Entry Spot도 같은 handler/callback 등록 표면을 제공한다. Entry Spot packet, lifecycle,
route, subscription, timer callback과 해당 실행 줄에서 시작한 request continuation은
Entry 실행 줄에서 직렬화한다. Entry actor packet은 actor별 mailbox에서 처리하므로 같은
actor의 순서는 유지하면서 서로 다른 actor는 병렬로 실행할 수 있다.
user Spot timer callback 은 packet, subscription, channel reply, actor packet 과 같은
user Spot queue 에서 처리한다.

##### framework가 직접 관리하지 않는 것

framework는 core actor 객체의 네트워크 수명을 관리하지 않는다. framework는 application
actor 객체의 lifecycle과 dispatch routing만 관리하고, core의 수신 알림을 언어별
handler 실행 문맥으로 연결한다.

## 4. Spring Boot 방향

### 4.1 기대하는 표면

Spring에서는 annotation 기반 handler가 자연스럽다.
RSocket의 `@MessageMapping`과 비슷한 경험을 주는 방향이 적합하다.
서버 간 `send/request`도 이 annotation 계열에 자연스럽게 올라가야 한다.

### 4.2 예시

```java
@ZLinkController
public final class ProfileController {

    @ZLinkMapping(packetName = "profile.get")
    public Mono<ProfileReply> get(ProfileRequest request, ZLinkContext ctx) {
        return Mono.just(new ProfileReply());
    }
}
```

여기서는 annotation에 packet 이름을 직접 적는 예시를 들었지만, 실제 구현에서는
request 타입 이름을 기본 packet key로 삼고 annotation 값은 explicit override로
쓰는 쪽이 더 자연스럽다.

## 5. NestJS 방향

### 5.1 기대하는 표면

NestJS는 메시지 기반 프로그래밍 모델이 이미 익숙하므로, 가능하면
`@MessagePattern`, `@EventPattern` 같은 기존 감각과 닮게 가는 편이 좋다.
다만 raw header를 message payload에 섞어 넣는 방식은 기본으로 두지 않는다.

### 5.2 예시

```typescript
@Controller()
export class ProfileController {
  @MessagePattern('profile.get')
  getProfile(data: ProfileRequest, ctx: ZLinkContext): Promise<ProfileReply> {
    return Promise.resolve({} as ProfileReply);
  }

  @EventPattern('cache.invalidate')
  invalidate(data: InvalidateEvent, ctx: ZLinkContext): void {
  }
}
```

NestJS 예시도 같은 맥락이다. decorator 값은 packet key 또는 event name override
예시로 보는 편이 맞다.

## 6. FastAPI 방향

### 6.1 기대하는 표면

FastAPI에서는 dependency 주입과 startup/shutdown hook이 핵심이다.
그래서 zlink runtime도 application 수명과 함께 올라가고 내려가는 형태가
자연스럽다.

- `add_zlink_framework(...)`
- `Depends(...)`로 받는 outbound client
- startup 시 local channel / outbound channel 등록
- route handler 안에서 그대로 쓰는 request/send client

### 6.2 예시

```python
app = FastAPI()
add_zlink_framework(
    app,
    channel_name="profile",
    outbound_channels=["account"],
)


@app.post("/profiles/get")
async def get_profile(
    request: GetProfileHttpRequest,
    client: ZLinkClient = Depends(get_zlink_client),
) -> ProfileReply:
    return await client.request(
        "account",
        GetProfileRequest(account_id=request.account_id),
    )
```

FastAPI 방향에서는 framework 내부 dispatch loop를 route 함수로 끌어올리지 않고,
기존 async application 구조 안에 zlink runtime을 붙이는 모양을 기본으로 본다.

## 7. C++ zlink framework host 방향

### 7.1 기대하는 표면

`C++`는 다른 언어처럼 기존 웹 프레임워크 위 adapter보다, zlink framework host가
application lifetime과 dispatch loop를 직접 소유하는 쪽이 더 자연스럽다.

- application host builder
- local channel 등록
- outbound channel 등록
- request/send handler registry
- poll loop와 lifecycle 통합
- location store 등록/manual connection 설정

### 7.2 예시

```cpp
using namespace zlink::framework;

int main() {
    app_t app = app_t::build();
    app.set_channel_name("profile")
       .add_outbound_channel("account")
       .add_request_handler("GetProfileRequest", profile_handler)
       .run();
}
```

`C++` 방향에서는 DI container보다 host builder와 registration API가 더 중요하다.
핵심은 raw socket 배선을 application 코드로 퍼뜨리지 않으면서, lifecycle과
dispatch loop를 framework host가 직접 관리하는 것이다. application이 runtime 구현체를
직접 만들고 시작하는 방법은 public contract로 두지 않는다.

## 8. 결정된 기준

- 공용 annotation 이름을 모든 프레임워크에서 억지로 통일하지 않는다.
  각 호스트 프레임워크의 익숙한 idiom을 우선한다.
- NestJS는 기존 `@MessagePattern`, `@EventPattern`과 닮은 감각을 우선한다.
- ASP.NET Core는 attribute 기반 handler model을 기본으로 보고, endpoint mapping은
  보조 등록 표면으로 다룬다.
- FastAPI는 runtime bootstrap을 helper registration이 맡고, HTTP 쪽은 기존 route
  decorator를 그대로 사용한다.
- `C++` host는 framework lifecycle에 필요한 scheduler/timer만 기본 제공하고,
  범용 application scheduler까지 표준 표면으로 끌어올리지는 않는다.
- pub/sub은 일반 `PUB/SUB` event 모델을 먼저 설명하고, `SPOT` event는 별도 상위
  모델로 분리한다.
- `STREAM` 정책 설명은 framework Header 기반 packet session과 session lifecycle
  축으로 충분하다고 본다.
- context에는 routing, timeout, trace 같은 공통 metadata만 올리고, workflow 엔진
  수준의 metadata는 기본 표면으로 끌어올리지 않는다.

지금 단계에서는 이름보다 "그 프레임워크 사용자가 낯설지 않게 느끼는가"를 더
중요하게 본다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](README.ko.md) | [이전: 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md) | [다음: ZLink Framework Channel Topology](10-channel-topology.ko.md)
<!-- framework-adapter-nav:bottom:end -->
