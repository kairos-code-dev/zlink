<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [다음: Draft -- ZLink Framework C++ Channel Messaging Samples](./channel-messaging-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Framework Adapter 정책](../../../../doc/spec/README.ko.md) | [구현 계획](./cpp-framework-implementation-plan.ko.md) | [POSD 기록](./cpp-framework-posd-refactoring-log.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Application Framework](../spec/cpp-application-framework.ko.md) | [Framework 인터페이스](../spec/cpp-framework-interfaces.ko.md) | [인터페이스](../spec/handler-interfaces.ko.md) | [channel](../spec/cpp-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](../spec/cpp-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [ActorGateway relay](../spec/actor-gateway-session-relay.ko.md) | [Stage wrapper](../spec/stage-wrapper-on-spot.ko.md) | [STREAM](../spec/cpp-stream.ko.md) | [STREAM decisions](./stream-open-items.ko.md) | [STREAM Connector 가이드](../../connector/doc/guide/INDEX.ko.md) | [HTTP Client](../../http-client/doc/spec/cpp-http-client.ko.md) | [HTTP Hosting](../spec/cpp-http-hosting.ko.md) | [Embedded HTTP Server](../spec/cpp-embedded-http-server.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [Monitoring](../spec/cpp-monitoring.ko.md) | [Registry](../spec/cpp-registry.ko.md)

# Draft -- ZLink Framework For C++

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++`에서 `ZLink Framework`를 어떤 모양으로 노출할지
> 정리하기 위한 문서다.

## 1. 목적

이 문서는 `C++` 바인딩 위에 올라가는 `ZLink Framework`의 `C++` 방향을 정리한다.
`C++`는 기존 대표 웹 프레임워크 위 adapter보다, zlink framework가 host/runtime
역할 일부를 직접 제공하는 standalone 형태로 설명하는 편이 맞다.

이 디렉토리의 문서는 `framework/doc/spec` 아래의 공통 framework 정책을 상위 기준으로
따른다. 언어별 스펙은 공통 정책을 반드시 반영해야 하며, `C++` 문서는 그 공통 의미를
`C++` 언어 특성에 맞게 구체화한다.

기능 기준은 현재 `.NET` framework다. `C++` framework는 같은 application model,
messaging model, handler model, `STREAM`, `SPOT`, ActorGateway session relay,
monitoring, graceful shutdown을 제공한다. 차이는 언어 표현과 ownership 모델뿐이다.
`.NET`의 네트워크 `Async()` 계열 호출은 C++20 `task_t<T>`를 돌려주는
`async()`와 `co_await`로 투영한다. 서버 framework public API에는 thread를 block하는
네트워크 `submit()` 표면을 두지 않는다. `.NET` DI scope는 C++ 자체 DI scope와 RAII lifetime으로
투영한다.

폴더 구조도 `.NET` framework의 역할 분리를 기준으로 맞춘다. `.NET`의
`Contracts/*`는 C++에서 설치되는 public header인
`framework/include/zlink/framework/contracts/*`에 대응하고, `.NET`의 `Runtime/*`는
C++에서 배포 대상이 아닌 `framework/src/runtime/*` 구현에 대응한다. C++는 파일명,
namespace, header 배치를 `snake_case`와 `.hpp` 중심으로 유지하되, public 계약과
runtime 구현을 같은 파일이나 같은 디렉토리에 섞지 않는다.

namespace 정의는 C++17 nested namespace 문법을 사용한다. `zlink`가 소유한 top-level
framework namespace block은 `namespace zlink::framework`, `namespace
zlink::framework::detail`, `namespace zlink::framework::runtime::messaging`처럼 전체 경로를
한 번에 연다. `namespace zlink { namespace framework { ... } }`처럼 계층을 여러 block으로
쪼개지 않는다. 이미 열린 `zlink::framework` block 안에서 짧은 local subnamespace를 열어야
하는 경우에도 닫는 주석은 실제 namespace 경로가 분명해야 한다.

이 분리는 기존 `bindings/cpp`보다 더 강하게 적용한다. binding은 native zlink API를
C++ 타입으로 감싸는 낮은 계층이므로 일부 thin wrapper와 inline 구현이 public header에
남을 수 있다. 하지만 framework는 application 개발자가 직접 쓰는 높은 계층이므로,
public header는 app, handler, call object, stream, spot, timer 같은 계약만 보여야 한다.
CAPI handle, dispatch callback, recv loop, frame codec, pending queue, thread/executor
구현은 framework runtime 내부에 숨긴다.

이 기준은 단순한 폴더 이름 규칙이 아니다. 각 기능을 구현하기 전에 먼저 public contract
owner와 runtime implementation owner를 나눈다. contract header는 사용자가 구현하거나
호출하는 shape만 설명하고, registry table, queue, dispatch projection, codec wiring,
native lifecycle은 runtime 구현이 소유한다. C++에서 concrete facade를 쓰더라도 내부
상태는 PIMPL 또는 type-erased state로 감추며, `contracts/detail/*`은 template 검사와
forwarding까지만 허용한다.

따라서 draft를 구현할 때는 구현 파일을 만들기 전에 다음 질문을 먼저 닫는다.

- 이 타입이 `.NET Contracts/*`에 대응하는 사용자 계약인가?
- 이 타입의 state, registry, queue, dispatcher, codec 구현은 어느 `src/runtime/*` 파일이
  소유하는가?
- public header가 native handle, socket, poller, callback userdata, dispatch token,
  frame codec 구현, timer token을 노출하지 않는가?
- template 때문에 header에 들어간 코드가 compile-time 검사와 forwarding을 넘어서지 않는가?
- layout/contract test가 public header와 runtime header의 경계를 확인하는가?

이 질문은 구현 중간에 확인하는 참고 사항이 아니라 구현 시작 전 gate다. 어떤 기능이
`.NET Contracts/*`에 대응하는지, 그 기능의 C++ public header가 어디인지, 그리고 같은
기능의 runtime 구현이 어느 `src/runtime/*` owner로 들어가는지 먼저 닫지 않으면 해당
goal 구현을 시작하지 않는다. C++에는 assembly `internal`이 없으므로 이 gate는 문서
규칙이 아니라 설치 header, CMake target, test include 경계로 강제해야 한다.

기본 물리 구조는 아래처럼 고정한다.

| 역할 | C++ 위치 | 설치/공개 여부 | 기준 |
|------|----------|----------------|------|
| framework contract | `framework/include/zlink/framework/contracts/*` | 설치 public header | `.NET Contracts/*` 대응 |
| framework facade | `framework/include/zlink/framework/*.hpp`, `zlink/framework.hpp` | 설치 public header | include 편의, 새 runtime 계약 금지 |
| framework runtime | `framework/src/runtime/*` | private build input | `.NET Runtime/*` 대응 |
| framework runtime backend contract | `framework/src/runtime/backend/contracts/*` | private build input | `.NET Runtime/Backend/Contracts/*` 대응 |
| connector contract | `connector/core/include/zlink/stream_connector/contracts/*` | 설치 public header | `.NET Stream Connector Contracts/*` 대응 |
| connector runtime | `connector/core/src/runtime/*` | private build input | connector receive/reconnect/codec 구현 |
| Unreal connector public | `connector/engines/unreal/Source/ZLinkStreamConnector/Public/*` | Unreal public header | Unreal 타입과 Blueprint/Game Thread 표면 |
| stream e2e client | `connector/e2e-client/*` | 선택 package | 서버 e2e/smoke/perf scenario helper |
| Unreal connector private | `connector/engines/unreal/Source/ZLinkStreamConnector/Private/*` | Unreal private implementation | 기본 connector 호출, Game Thread dispatch |

`.NET`의 현재 폴더 구조는 아래 C++ 구조와 1:1 의미로 맞춘다. C++는 언어 관례 때문에
파일명과 타입 이름은 다르게 쓸 수 있지만, 어느 쪽이 사용자가 보는 계약이고 어느 쪽이
runtime 구현인지에 대한 경계는 바꾸지 않는다.

| `.NET` framework | C++ framework | 의미 |
|------------------|---------------|------|
| `Contracts/Actors` | `contracts/actors` | actor와 bound session public 계약 |
| `Contracts/Assembly` | `contracts/assembly` | module/assembly discovery public 계약 |
| `Contracts/Channels` | `contracts/channels` | channel send/request/pub/sub public 계약 |
| `Contracts/Codecs` | `contracts/codecs` | serializer와 message codec public 계약 |
| `Contracts/Configuration` | `contracts/configuration` | app builder, DI, option public 계약 |
| `Contracts/Dispatch` | `contracts/dispatch` | task, coroutine submit, offload option public 계약 |
| `Contracts/Errors` | `contracts/errors` | error kind, exception, result public 계약 |
| `Contracts/Eventing` | `contracts/eventing` | monitoring event public 계약 |
| `Contracts/Handlers` | `contracts/handlers` | handler shape와 registry public 계약 |
| `Contracts/Registry` | `contracts/registry` | registry와 topology public 계약 |
| `Contracts/Spots` | `contracts/spots` | Spot actor, context, publish public 계약 |
| `Contracts/Streams` | `contracts/streams` | STREAM session과 endpoint public 계약 |
| `Contracts/Timers` | `contracts/timers` | timer option과 tick handler public 계약 |
| `Runtime/Actors` | `src/runtime/actors` | actor instance, mailbox, relay dispatch 구현 |
| `Runtime/Backend` | `src/runtime/backend` | zlink binding substrate 연결 구현 |
| `Runtime/Backend/Contracts` | `src/runtime/backend/contracts` | public이 아닌 backend 내부 계약 |
| `Runtime/Channels` | `src/runtime/channels` | runtime bundle, receive loop, message pump, correlation, send-ready 구현 |
| `Runtime/Codecs` | `src/runtime/codecs` | type-erased serializer map과 codec wiring |
| `Runtime/Configuration` | `src/runtime/configuration` | service registry, option materialization 구현 |
| `Runtime/Diagnostics` | `src/runtime/diagnostics` | logging, monitoring source, health 구현 |
| `Runtime/Dispatch` | `src/runtime/dispatch` | coroutine completion 구현 |
| `Runtime/Execution` | `src/runtime/execution` | offload executor와 drain 구현 |
| `Runtime/Handlers` | `src/runtime/handlers` | descriptor map, DI resolve, invoke 구현 |
| `Runtime/Host` | `src/runtime/host` | app lifecycle, graceful shutdown 구현 |
| `Runtime/Messaging` | `src/runtime/messaging` | call state, pending operation, retry hook 구현 |
| `Runtime/Registry` | `src/runtime/registry` | topology cache와 query owner 구현 |
| `Runtime/Spots` | `src/runtime/spots` | activation table, subscription pump 구현 |
| `Runtime/Streams` | `src/runtime/streams` | session table, frame codec, transport loop 구현 |
| `Runtime/Timers` | `src/runtime/timers` | native timer token과 tick drain 구현 |

public header에서 내부 상태가 필요하면 PIMPL, opaque state, type-erased handle 중 하나를
쓴다. 이때 state의 전방 선언은 public header에 둘 수 있지만, state 정의와 method 구현은
runtime owner에 둔다. 반대로 CAPI handle, socket owner, poller slot, dispatch callback,
pending queue, frame codec, timer token은 public data member, public 반환값, public
callback 인자로 올리지 않는다.

이 표에 맞는 owner를 정하지 못한 기능은 구현을 시작하지 않는다. 구현 중 public 타입이
추가로 필요하다는 사실을 발견하면, 코드를 먼저 늘리지 않고 이 draft의 owner 표와 해당
기능 문서를 먼저 갱신한다. 그 뒤 public header, runtime owner, test boundary가 함께
맞는지 확인하고 구현으로 넘어간다.

Stream Connector는 C++ framework 샘플이나 framework package가 아니다. C++용
Stream Connector는 별도 public header, 별도 CMake target, 별도 배포 단위를 가지는
client-side library로 둔다. Unreal용 connector도 별도 Unreal plugin 배포 단위로 두고,
Unreal 전용 함수와 타입을 제공한다. 자세한 내용은
[STREAM Connector 가이드](../../connector/doc/guide/INDEX.ko.md)를 따른다.

codec 구조는 binding, framework, connector가 같은 원칙을 따른다. base C++ binding은
raw `message_t`와 protocol enum만 제공하고 JSON, MessagePack, Protobuf dependency를
끌고 오지 않는다. codec 사용성은 `message_t` 중심 API로 맞추며, 각 codec 구현은 필요한
target과 header를 선택한 경우에만 활성화한다. framework와 connector는 이 원칙 위에서
JSON을 기본 사용성으로 제공한다. MessagePack과 Protobuf는 선택 기능으로 두고, LZ4는
connector build feature를 기본 ON으로 제공하되 실제 packet 압축은 호출자가 packet option으로
선택한다.

`C++`에는 `.NET`, `Java`, `Node.js`처럼 기준으로 삼을 메이저 애플리케이션
프레임워크가 없으므로, [C++ 정책](./cpp-framework-policy.ko.md)은 app, host, DI,
runtime, handler registry 같은 기반 프레임워크 설계 내용을 다른 언어 문서보다 더
많이 담는다. 이 내용은 공통 정책을 대체하지 않고, 공통 정책에서 다루지 않은
`C++` standalone framework 세부 스펙을 채우기 위한 것이다.

## 1.1 공통 정책 적용

이 디렉토리의 모든 문서는
[Framework Adapter 정책](../../../../doc/spec/README.ko.md)과 그 하위 문서를 그대로 따른다.
즉 `C++` 상세 문서는 공통 의미를 다시 정의하지 않고, 그 의미를 `C++`
host/runtime 표면으로만 구체화한다.

특히 아래 항목은 이 디렉토리 전체에 공통으로 적용된다.

- 네이밍 규칙은
  [doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
  `Naming Policy`를 그대로 따른다. `C++` 문서에서는 메서드는 `snake_case`,
  타입은 `_t` 접미사를 기준으로 적는다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다. 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- send/publish의 async submit과 backpressure 의미는
  [framework 공통 비동기 정책](../../../../doc/spec/async-execution-policy.ko.md)을
  따른다.
- `SPOT`을 지원하는 문서는 named spot factory 등록, `spot_name` 기준 생성,
  `spot_rid -> spot_name` 조회, lifecycle timer, 외부 spot publish 표면을
  공통 정책과 맞춰 설명해야 한다.
- actor/session relay는 application route mesh channel로 우회하지 않고, stream이
  붙은 local SpotNode의 ActorGateway 경로로 보낸다. `C++` framework에는 기존 host가
  없으므로 ActorGateway attach, actor factory, bound session 표면을 자체 runtime
  기능으로 만든다.
- Registry는 Spot remote address 조회 기본값으로 사용하고, session actor relay hot
  path의 actor route store로 쓰지 않는다.
- monitoring을 지원하는 문서는 socket/discovery/registry/spot runtime event를
  typed event와 등록 표면으로 설명해야 한다. SPOT timer handler failure는 snapshot
  interval을 기다리지 않는 point-in-time event로 설명한다.

## 2. 문서 구조와 역할 분담

### 2.1 기준 문서

| 문서 | 역할 |
|------|------|
| [cpp-framework-implementation-plan.ko.md](./cpp-framework-implementation-plan.ko.md) | draft 전체 내용을 goal 단위로 빠짐없이 구현하기 위한 실행 계획 |
| [cpp-framework-posd-refactoring-log.ko.md](./cpp-framework-posd-refactoring-log.ko.md) | 각 goal에서 수행한 POSD 기반 리팩토링 기록 |
| [cpp-framework-policy.ko.md](./cpp-framework-policy.ko.md) | `C++` zlink framework host의 제품 포지셔닝, 권장 모듈 구조, 라이브러리 정책, 구현 순서 |
| [cpp-application-framework.ko.md](../spec/cpp-application-framework.ko.md) | `.NET Core`를 주 벤치마크로 하고 `ASP.NET Core Minimal API`를 HTTP 기준으로 삼는 application framework 기능 축과 회귀 테스트 매트릭스 |
| [cpp-framework-interfaces.ko.md](../spec/cpp-framework-interfaces.ko.md) | C++ binding public API를 기반으로 한 framework public interface 설계 |
| [handler-interfaces.ko.md](../spec/handler-interfaces.ko.md) | 기존 `C++` adapter 세부 인터페이스 초안. zlink framework host 정책에 맞춰 정렬해야 할 대상 |

### 2.2 주제 문서

| 문서 | 다루는 범위 |
|------|------------|
| [cpp-channel-messaging.ko.md](../spec/cpp-channel-messaging.ko.md) | app host, channel 등록, handler dispatch, outbound client |
| [cpp-spot.ko.md](../spec/cpp-spot.ko.md) | `SPOT` runtime, publish/subscribe, spot-to-spot |
| [actor-gateway-session-relay.ko.md](../spec/actor-gateway-session-relay.ko.md) | STREAM session과 actor를 ActorGateway로 bind/relay하는 C++ standalone runtime 초안 |
| [cpp-stream.ko.md](../spec/cpp-stream.ko.md) | framework Header 기반 packet stream과 handler 통합 |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | STREAM 결정 기록 |
| [connector/doc/guide/INDEX.ko.md](../../connector/doc/guide/INDEX.ko.md) | C++용 Stream Connector 별도 라이브러리, 배포 단위, Unreal/Godot/Axmol adapter 기준 |
| [cpp-http-client.ko.md](../../http-client/doc/spec/cpp-http-client.ko.md) | C++ framework 샘플과 HTTP e2e에서 쓰는 fluent HTTP/JSON client |
| [cpp-http-hosting.ko.md](../spec/cpp-http-hosting.ko.md) | ASP.NET Core Minimal API에 대응하는 HTTP hosting과 zlink request 연동 |
| [cpp-embedded-http-server.ko.md](../spec/cpp-embedded-http-server.ko.md) | 내장 HTTP 웹서버 runtime 개발 기준 |
| [cpp-monitoring.ko.md](../spec/cpp-monitoring.ko.md) | runtime monitoring 등록, typed event, 운영 샘플 |
| [stage-wrapper-on-spot.ko.md](../spec/stage-wrapper-on-spot.ko.md) | stage 같은 상위 모델을 `SPOT` 위에 감쌀 때의 조건 |
| [cpp-registry.ko.md](../spec/cpp-registry.ko.md) | embedded registry, query, topology 조회 |

### 2.3 샘플 문서

| 문서 | 다루는 범위 |
|------|------------|
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | host bootstrap, handler registry, outbound client 샘플 |
| [spot-samples.ko.md](./spot-samples.ko.md) | `SPOT` request/subscribe/publish 샘플 |
| [stream-samples.ko.md](./stream-samples.ko.md) | framework Header 기반 packet stream 샘플 |

### 2.4 리뷰 샘플

C++ framework의 전반 동작 리뷰 샘플은 `Bingo`와 `TicTacToe` 두 개로 둔다.

| 샘플 | 역할 | 포함 범위 |
|------|------|----------|
| `Bingo` | channel/SPOT/session stream 기반 기본 실시간 메시징 샘플 | `Server/Configuration`, `Client/Configuration`, `Shared/Contracts`, `Client`, `Server/Registry/*HostFactory`, `Server/Api/*HostFactory`, `Server/Api/Handlers`, `Server/Play/*HostFactory`, `Server/Play/Actors`, `Server/Play/Handlers`, `Server/Play/BingoRoomSpots`, `Server/Play/BingoRoomSpots/Handlers`, `Server/Play/EntrySpot`, `Server/Play/EntrySpot/Handlers`, `Server/Session/*HostFactory` 파일 분리, `.NET` Bingo packet 이름과 handler 흐름 |
| `TicTacToe` | HTTP 시작 요청, STREAM, ActorGateway 기반 actor/session relay 샘플 | `Server/Configuration`, `Client/Configuration`, `Shared/Contracts`, `Client`, `Server/Registry/*HostFactory`, `Server/Api/*HostFactory`, `Server/Api/Handlers`, `Server/Play/*HostFactory`, `Server/Play/EntrySpot`, `Server/Play/EntrySpot/Handlers`, `Server/Play/GameSpots`, `Server/Play/GameSpots/Handlers`, `Server/Play/Handlers`, `Server/Session/*HostFactory` 파일 분리, `.NET` TicTacToe의 `POST /games` HTTP 시작 흐름, packet 이름과 handler 흐름 |

`Bingo`도 `.NET` Bingo와 같은 session stream 역할을 포함한다. `TicTacToe`는 `.NET`
TicTacToe처럼 HTTP `POST /games`로 게임을 만들고, 응답으로 받은 STREAM endpoint에
connector가 붙는 흐름을 포함한다. STREAM과 ActorGateway 기반 actor/session relay를 더
직접적으로 검토하는 기준 샘플이며, 별도 접미사는 붙이지 않는다.

## 3. 핵심 방향

- zlink framework가 application host/runtime 역할 일부를 직접 제공한다.
- 기능과 사용성 개념은 `.NET` framework와 동일하게 잡고, C++20 표현으로만 바꾼다.
- channel messaging 기본 호출은 `channel name` 기준이다.
- channel capability는 startup 시점에 등록한다.
- 일반 request/send handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)`는 reply correlation 경로로 본다.
- packet key 기본값은 payload 타입 이름을 쓴다.
- `rid` 직접 지정은 `SPOT` spot-to-spot 경로와 Entry Spot join 같은 actor lifecycle
  경로에만 남긴다.
- STREAM session에서 actor로 넘기는 요청은 route mesh channel이 아니라
  ActorGateway attach + logical actor handle을 사용한다.
- user Spot timer는 같은 core SPOT dispatch boundary에서 순서 정책을 따르고, Entry
  Spot timer는 Entry Spot actor packet, lifecycle callback, request continuation과
  같은 Entry Spot 실행 줄에서 처리한다.
