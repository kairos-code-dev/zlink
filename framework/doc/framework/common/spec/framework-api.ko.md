<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Channel Topology](channel-topology.ko.md) | [다음: ZLink Framework Actor Model](actor-model.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[문서 묶음](../README.ko.md) | [개요](overview.ko.md) | [use cases](../use-cases/README.ko.md) | [상호작용 모델](interaction-model.ko.md) | [메시지 모델](message-model.ko.md) | [channel topology](channel-topology.ko.md) | [검증](usecase-validation.ko.md) | [.NET](../../dotnet/README.ko.md) | [Java](../../java/README.ko.md) | [Node.js](../../node/README.ko.md) | [C++](../../cpp/README.ko.md)

# ZLink Framework API

## 1. 목적

같은 `ZLink Framework`라도 `ASP.NET Core`, `Spring Boot`, `NestJS`,
`FastAPI`, C++ zlink framework host 사용자가 기대하는 표면은 조금씩
다르다. 이 문서는 각 환경에서 "어떤 식으로 보이면 자연스러운가"를 정리한다.

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
  [message-model.ko.md](message-model.ko.md)의 multipart `header + payload` 계약을
  따른다. 각 언어 adapter는 payload를 header object 안에 다시 넣어 단일 메시지로
  직렬화하면 안 된다.

## 2. 공통 방향

### 2.1 서버 쪽

- handler를 프레임워크 표준 등록 방식으로 붙인다.
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
  [interaction-model.ko.md §3.4](interaction-model.ko.md)에 둔다. 이 문서는
  필요한 곳에서 같은 규칙을 따른다고만 적고, 정의는 한 곳에서만 한다.

### 2.2 클라이언트 쪽

- 공용 outbound client를 DI로 주입한다.
- 요청 메서드는 async 중심으로 제공한다.
- codec, timeout, target channel을 설정할 수 있다.
- outbound 호출의 payload 인자는 **업무 객체**다. codec 선택은 호출부가 아니라 runtime
  구성 단계에서 끝난다. framework가 요청·응답 객체 타입을 보고 serializer를 찾아 byte
  payload로 직렬화하고, reply도 업무 객체로 복원한다.
- JSON은 framework 표준 codec이며, codec을 따로 설정하지 않으면 기본값으로 사용한다.
  기존 `codecs().add_json()`/`addJson()` 호출은 호환을 위해 남아 있지만 새 코드에서는
  JSON만 쓰기 위해 별도 등록을 하지 않는다.
- Protobuf와 MessagePack은 framework core의 기본 의존성이 아니다. 두 codec은 선택
  framework codec extension package로 제공한다. application은 필요한 package만 설치하고
  구성 단계에서 extension을 등록한다.
- 사용자 정의 codec도 Protobuf/MessagePack과 같은 extension 계약을 사용한다. Avro, Thrift,
  사내 binary format 같은 codec은 framework core를 바꾸지 않고 extension package로 추가한다.
  serializer는 업무 객체와 byte payload 사이의 변환만 담당하고, packet name 결정과 codec
  선택 정책은 framework 내부에 남는다.
- framework, stream connector, HTTP client는 같은 codec extension을 공유한다. 대상별 builder는
  다를 수 있지만 등록 모양은 `use(extension)`으로 맞춘다. codec을 바꿔도 handler method,
  request method, reply type, payload DTO는 바꾸지 않는다.

  | 대상 | codec 설정 방향 |
  |------|----------------|
  | framework | runtime 구성 단계에서 codec extension을 등록한다. JSON은 기본값이고, Protobuf/MessagePack/custom codec은 extension으로 추가한다. |
  | stream connector | connector 전용 codec package를 두지 않는다. framework codec extension이 제공하는 connector adapter를 등록한다. |
  | HTTP client | typed request/response body를 같은 codec extension으로 encode/decode한다. raw body API는 extension을 거치지 않는다. |

- Protobuf와 MessagePack extension package는 framework가 작성된 언어에만 만든다. 각 package는
  개별 배포가 가능해야 하며, 같은 codec extension이 framework, stream connector, HTTP client에
  필요한 adapter를 함께 제공한다.

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
- packet name은 기본적으로 객체 타입에서 자동 추론하고(builder override → payload 자체
  이름 정보 → 선언적 metadata(annotation/attribute/decorator/registry) → nominal type
  정보 순), 추론할 수 없을 때만 `.packetName(...)` override를 쓴다.
- gateway 주소나 load balancer 주소 대신 `channel name` 기준 호출을 기본으로
  삼는다.
- send는 기본적으로 async submit으로 둔다. backpressure 처리는 호출자가
  `DontWait` 같은 옵션으로 고르지 않고 framework 내부의 nonblocking send와 ready
  notification이 맡는다.
- framework runtime은 등록한 outbound channel마다 별도 outbound runtime을 관리할
  수 있어야 한다.
- 단순 unary request 외에 event publish와 필요하면 aggregate helper를 분리할 수
  있어야 한다.
- 운영 점검이나 관리 API에서는 Registry topology snapshot/query 결과를 읽는
  별도 surface를 둘 수 있어야 한다.
- socket/discovery/registry/spot runtime 변화를 typed event handler로 받을 수
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
  공개 키로 사용한다. session -> actor 방향은 actor create/dispatch helper로,
  actor -> client 방향은 `IZLinkSessionProxy`로 나눈다. actor 개념의 라이프사이클
  과 표면은 [actor-model.ko.md](actor-model.ko.md)에서, gateway use case의 사용성
  결정은 [session-actor-dispatch.ko.md](session-actor-dispatch.ko.md)에서
  본다.

### 2.3 transport 통합 축

framework가 직접 통합할 transport 축은 [overview.ko.md](overview.ko.md)의
section 2에 정의되어 있다. 이 문서는 channel messaging, `PUB/SUB`, `STREAM`
세 축을 중심으로 보되, 공통 API 원칙과 lifecycle 경계에 직접 영향을 주는
`SPOT` 표면도 함께 다룬다. `SPOT`의 자세한 계약과 샘플은
[.NET SPOT 문서](../../dotnet/spec/aspnet-core-spot.ko.md) 등 별도
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

### 2.4 runtime monitoring

운영 이벤트는 일반 request/send/event handler와 다른 성격을 가진다. 따라서
framework는 monitoring 표면을 별도 축으로 설명하는 편이 맞다.

- event kind는 enum으로 둔다.
- 실제 callback payload는 source 이름과 상세 정보를 함께 가진 구조화된 값으로
  둔다.
- socket/discovery는 하부 monitor를 감싸는 편이 자연스럽다.
- registry/spot는 raw monitor를 가장한 표면보다 snapshot diff 기반 event로
  설명하는 편이 맞다.
- application은 typed runtime event handler를 구현해서 이 이벤트를 받는 모델을
  기본으로 본다.

framework는 모든 source를 같은 raw monitor API로 보이게 하지 않고,
source별 구현 차이를 숨긴 typed runtime event surface를 제공하는 편이 더
자연스럽다.

### 2.4.1 message dispatch error observer

framework message dispatch 단계에서 등록되지 않은 packet, payload decode 실패, handler 예외,
invalid frame 을 만나면 언어별 runtime 은 같은 의미의 dispatch error event 를 만든다. 이 event 는
전역 observer 하나로만 전달한다. channel 별 또는 spot 별 observer 는 이 버전의 공개 계약이 아니다.
사용자가 특정 channel, topic, spot, actor 만 보고 싶으면 event 안의 context 필드로 직접 필터링한다.

request 로 인식한 메시지는 reply path 가 있으면 항상 error reply 로 끝난다. 같은 process 안의 actor
호출처럼 reply frame 이 없는 경로는 caller future, promise, 또는 task 를 framework error 로 완료한다.
send, publish, subscription, actor send 같은 one-way 메시지는 reply 를 만들 수 없으므로 drop 하되,
기본 로그, metric 또는 counter, observer event 를 남긴다.

event 는 원본 native frame 이나 message ownership 을 노출하지 않는 불변 snapshot 이다. 공통 의미는
아래 필드를 가진다.

| 필드 | 의미 |
|------|------|
| `surface` | `Channel`, `RouteMeshChannel`, `SpotRoute`, `SpotSubscription`, `SpotActor`, `StreamSession` |
| `messageKind` | `Request`, `Send`, `Publish`, `ActorRequest`, `ActorSend` |
| `reason` | `HandlerMissing`, `PayloadDecodeFailed`, `HandlerException`, `InvalidFrame`, `ReplyPathMissing` |
| `action` | `ReplyError` 또는 `Drop` |
| `packetName` | packet/message 이름. 알 수 없으면 언어별 null/optional 값 |
| `channelName` | channel 또는 route mesh channel 이름 |
| `topic` | publish/subscription topic |
| `spotRid` | SPOT routing id |
| `actorId` | actor id |
| `sourceRid` | routing source id |
| `correlationId` | request correlation id 또는 sequence |
| `exception` | decode 실패나 handler 예외. handler 없음에는 값이 없을 수 있다 |

observer 등록 여부와 관계없이 기본 로그와 metric/counter 는 남아야 한다. observer callback 실패는
별도 error sink 나 내부 로그로만 기록하고 dispatch loop, error reply 전송, shutdown 을 깨지 않는다.

### 2.5 public contract와 runtime 구현의 분리 기준

이 기준은 `.NET` framework adapter만을 위한 규칙이 아니다. Java, Node.js,
Python, C++, Go, Rust 같은 다른 framework adapter도 같은 정책을 따른다. 언어마다
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
- socket, codec, dispatch, routing, registry 같은 내부 정책 class
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
샘플은 ASP.NET Core host, C++ 샘플은 zlink framework `app_t` host를 기준으로 작성한다.

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
  함께 붙어 있다.
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

예를 들어 metadata, route snapshot, registry entry, monitoring event payload, error
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

수동 연결을 둘 때는 `channel` 전체가 아니라 `channel + capability` 기준으로
설정해야 한다. 예를 들어 `account.client` 수동 연결과 `account.subscriber`
수동 연결은 별도 집합으로 본다. 수동 연결 역할은 startup
설정만이 아니라, 런타임 `Connect`, `Disconnect`, `ListConnections` 같은 제어도
지원해야 한다.

여기서 channel client manual 연결은 remote `RoutingId`를 따로 받지 않는 편이
자연스럽다. 하부 `DEALER(client)`가 connect된 peer 집합으로 요청을 보내는
모델이므로, startup과 런타임 제어 모두 endpoint 집합만 관리하면 된다.

또한 send는 기본 async submit으로 둔다. 구현은 blocking send를 task로 감싸지
않고, 먼저 nonblocking send를 시도한 뒤 temporary backpressure가 발생하면 pending
send queue와 ready notification으로 이어서 처리한다. send 대기 한계는 call
builder가 아니라 framework 기본값 또는 socket의 `SendTimeout` 옵션을 따른다.
framework 기본값은 core socket 기본 send timeout과 같은 1000ms로 맞춘다.
각 binding은 개별 socket option이 있으면 그 값을 우선 사용하고, 없으면
framework 기본값을 async pending deadline으로 사용한다. framework 기본 send
timeout을 명시적으로 비우는 언어에서는 무한 대기로 본다.
publish도 send와 같은 submit 규칙을 따른다. subscriber 처리 완료를 기다리지 않고,
local publish transport에 메시지를 맡길 수 있을 때까지 비동기로 기다린다.

request도 reply를 기다리는 async 호출로 설명한다. 다만 request packet을 보내는
단계는 send와 같은 async submit 경로를 사용해야 한다. `Timeout(...)`은 reply
대기 시간만 정하고, 전송 backpressure는 `SendTimeout` 정책이 처리한다.
request/reply 기본 대기 시간은 framework 전역 기본값 30초다. 호출별 timeout이
있으면 가장 먼저 적용하고, 없으면 channel별 기본 request timeout을 적용하며,
channel 설정도 없을 때 전역 기본값을 사용한다.

고성능 구현에서는 immediate send/publish 성공 path가 allocation 없이 완료되어야
한다. backpressure path는 bounded pending queue를 사용하고, ready notification마다
정해진 batch budget 안에서 queue를 drain한다. 이렇게 해야 thread blocking 없이도
높은 처리량을 유지할 수 있다.

보다 자세한 `.NET` 초안은 [.NET 문서](../../dotnet/README.ko.md)를 참고한다.

### 3.3 ASP.NET Core의 SPOT 방향

`SPOT`은 일반 channel messaging보다 instance lifecycle과 실행 문맥이 더 먼저
보이는 표면이다. 공통 정책 차원에서는 아래 정도만 고정한다.

- active SPOT channel view는 `AddSpotMesh(channelName)`가
  정한다. SpotMesh 등록 하나가 같은 프로세스의 단일 SpotNode를 나타내며, 새 샘플은
  mesh 등록을 기준으로 작성한다.
- `SpotNode`는 router, pub/sub, route bridge 기반 외부 호출 역할을 가진다.
- local spot 인스턴스는 등록 이름으로 만들고, lifecycle 안에서 packet, subscribe,
  timer를 등록한다.
- spot timer 는 framework 가 만든 managed scheduler 를 사용한다. user Spot timer 는
  같은 user Spot 실행 queue 에서 직렬화한다. Entry Spot timer 는 같은 timer instance 의
  callback 이 겹치지 않는다는 점만 공통으로 고정한다. Entry Spot timer 를 Entry Spot
  실행 줄에 묶을지는 언어별 runtime 정책에 맡기며, 언어별 feature map과 상세 문서에
  기록한다.
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
- actor 코드는 `IZLinkClient`나 `IZLinkSpotClient`를 직접 고르지 않고,
  actor context를 통해 channel request/send와 client stream reply/send를 수행한다.
  context는 join 전에는 일반 channel client 경로를, join 후에는 현재 `Spot`에
  route bridge channel socket 경로를 선택한다.
- actor context는 stream 객체를 직접 노출하지 않고, client로 보내는 `Send(...)`와
  request에 응답하는 `Reply(...)` 같은 의도 중심 API를 제공한다.
- actor를 완전히 제거하는 public API는 Entry Spot context에만 둔다. user Spot
  context는 actor를 Entry Spot으로 되돌리는 `leaveActor` 의미의 API까지만 제공한다.
  application은 room/game/stage 정리가 끝난 뒤 user Spot에서 actor에 종료 표시를 남기고
  `leaveActor`로 Entry Spot에 돌려보낸다. Entry Spot handler 또는 lifecycle callback은
  그 표시를 확인한 뒤 언어별 Entry Spot destroy API를 호출한다.
- Entry Spot destroy API는 actor registry, actor-session binding, native actor ref를
  함께 정리한다. 이 작업은 actor 위치 이동이 아니므로 `onLeaveActor`나 다른 lifecycle
  callback을 추가로 호출하지 않는다.
- stream disconnect는 현재 session binding cleanup과 `onDisconnectActor` 의미만 가진다.
  disconnect cleanup만으로 actor destroy가 실행되지 않는다. actor 수명 종료는 위의
  Entry Spot destroy 경로에서만 application이 명시적으로 선택한다.

자세한 contract와 샘플은
[.NET SPOT 문서](../../dotnet/spec/aspnet-core-spot.ko.md)
같은 binding 문서를 기준으로 본다.

#### 3.3.1 Actor lifecycle — zlink 라이브러리 위임

zlink 라이브러리에 native Actor API가 추가됨에 따라, framework는 actor lifecycle를
자체 구현 대신 라이브러리의 native API로 위임한다. 이 정책의 핵심은 아래와 같다.

##### Actor 생성 및 입장 흐름

1. `SpotNode.EntrySpot()` — framework가 입장 수신용 `Spot`을 얻는다.
2. `Spot.RecvActorJoin(RecvFlags)` — actor join request를 수신한다.
3. framework가 join 요청 메시지를 ZMP 포맷으로 해석해 등록된 actor join handler를 호출한다.
4. `Spot.ReplyActorJoin(request, joinResultCode, replyMessage)` — join 결과를 응답한다.
   `0`은 허용이고, 0이 아닌 값은 application 이 정의한 거부 코드다.

##### Actor 생성 (SpotNode 측)

- `SpotNode.CreateActor(string actorId)` — actor node에서 actor를 생성한다.
- `Actor.Join(Spot spot, Message request, TimeSpan timeout, CancellationToken)` — actor가 특정 spot에 join을 요청한다.
- `Actor.Leave(Spot spot, TimeSpan timeout)` — actor가 spot에서 나간다.

##### Actor 메시지 수신

zlink 라이브러리의 `SpotDispatchEvent` 중 두 가지가 actor lifecycle과 관련된다.

| 이벤트 | 값 | 의미 |
| ------ | -- | ---- |
| `ActorJoinReadable` | 6 | 새 actor join 요청이 도착했음 |
| `ActorReadable` | 5 | join된 actor의 STREAM 메시지가 도착했음 |

framework는 이 두 이벤트를 아래와 같이 처리한다.

- `ActorJoinReadable` → `Spot.RecvActorJoin(DontWait)` 루프로 모든 요청을 drain한 뒤 application join handler를 호출하고 `ReplyActorJoin`으로 결과를 반환한다. join handler에는 join 요청의 `TargetActor`(해당 spot에 이미 등록된 로컬 actor)와 요청 메시지를 전달한다.
- `ActorReadable` → 백엔드가 미리 drain한 `ActorPart` 목록을 받아 STREAM 메시지 단위로 묶어서 actor dispatch를 수행한다. 각 메시지는 header part (More=true) + payload part (More=false) 구조다.

`OnDispatchEvent` 핸들러는 spot 초기화 시 항상 등록한다. 패킷 handler나 actor join handler가 없는 spot도 런타임에 actor가 join될 수 있으므로 `ActorReadable` 이벤트를 받을 준비가 되어 있어야 한다.

##### 실행 문맥 보장

user Spot 에서는 두 이벤트를 spot serial executor를 통해 직렬화된 실행 문맥 안에서
처리하므로, actor join handler와 actor packet handler 사이에 동시성 경합이 없다.
Entry Spot 도 같은 handler/callback 등록 표면을 제공하며, Entry Spot packet callback,
actor packet callback, lifecycle callback, request continuation 을 Entry Spot 실행 줄에서
직렬화한다. Entry Spot timer callback 은 같은 timer instance callback 이 겹치지 않는다는
점만 공통으로 보장하고, Entry Spot 실행 줄에 묶을지는 언어별 runtime 정책에 맡긴다.
user Spot timer callback 은 packet, subscription, channel reply, actor packet 과 같은
user Spot queue 에서 처리한다.

##### framework가 직접 관리하지 않는 것

framework는 `Actor` 객체 자체의 네트워크 수명이 아니라, application actor 객체의 lifecycle과 dispatch routing만 관리한다. native `Actor`의 send/recv 루프는 라이브러리가 담당하며, framework는 dispatch event를 통해 통보를 받는다.

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
- registry/discovery/manual connection 설정

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
- scatter-gather 같은 aggregate helper는 adapter 기본 기능이 아니라 별도 확장
  계층으로 둔다.
- context에는 routing, timeout, trace 같은 공통 metadata만 올리고, workflow 엔진
  수준의 metadata는 기본 표면으로 끌어올리지 않는다.

지금 단계에서는 이름보다 "그 프레임워크 사용자가 낯설지 않게 느끼는가"를 더
중요하게 본다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Channel Topology](channel-topology.ko.md) | [다음: ZLink Framework Actor Model](actor-model.ko.md)
<!-- framework-adapter-nav:bottom:end -->
