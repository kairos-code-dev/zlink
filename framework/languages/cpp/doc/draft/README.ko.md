<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [다음: Draft -- ZLink Framework C++ Channel Messaging Samples](./channel-messaging-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Framework Adapter 정책](../../../../doc/spec/README.ko.md) | [구현 계획](./cpp-framework-implementation-plan.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./cpp-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./cpp-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [ActorGateway relay](./actor-gateway-session-relay.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [STREAM](./cpp-stream.ko.md) | [STREAM decisions](./stream-open-items.ko.md) | [STREAM Connector](./cpp-stream-connector.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [Monitoring](./cpp-monitoring.ko.md) | [Registry](./cpp-registry.ko.md)

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
`.NET`의 `Task<T>`와 `SubmitAsync()`는 C++20 `task_t<T>`와 `submit()`/`co_await`로,
`.NET` DI scope는 C++ 자체 DI scope와 RAII lifetime으로 투영한다.

Stream Connector는 C++ framework 샘플이나 framework package가 아니다. C++용
Stream Connector는 별도 public header, 별도 CMake target, 별도 배포 단위를 가지는
client-side library로 둔다. Unreal용 connector도 별도 Unreal plugin 배포 단위로 두고,
Unreal 전용 함수와 타입을 제공한다. 자세한 내용은
[STREAM Connector](./cpp-stream-connector.ko.md)를 따른다.

codec 구조는 binding, framework, connector가 같은 원칙을 따른다. base C++ binding은
raw `message_t`와 protocol enum만 제공하고 JSON, MessagePack, Protobuf dependency를
끌고 오지 않는다. codec 사용성은 `message_t` 중심 API로 맞추며, 각 codec 구현은 필요한
target과 header를 선택한 경우에만 활성화한다. framework와 connector는 이 원칙 위에서
JSON을 기본 사용성으로 제공하고, MessagePack, Protobuf, LZ4는 선택 기능으로 둔다.

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
- send/publish는 기본 async submit으로 설명한다. backpressure는 public
  non-blocking 옵션이 아니라 framework 내부의 nonblocking send, pending queue,
  ready notification으로 처리한다.
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
| [cpp-framework-policy.ko.md](./cpp-framework-policy.ko.md) | `C++` standalone host/runtime의 제품 포지셔닝, 권장 모듈 구조, 라이브러리 정책, 구현 순서 |
| [cpp-framework-interfaces.ko.md](./cpp-framework-interfaces.ko.md) | C++ binding public API를 기반으로 한 framework public interface 설계 |
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | 기존 `C++` adapter 세부 인터페이스 초안. standalone framework 정책에 맞춰 정렬해야 할 대상 |

### 2.2 주제 문서

| 문서 | 다루는 범위 |
|------|------------|
| [cpp-channel-messaging.ko.md](./cpp-channel-messaging.ko.md) | app host, channel 등록, handler dispatch, outbound client |
| [cpp-spot.ko.md](./cpp-spot.ko.md) | `SPOT` runtime, publish/subscribe, spot-to-spot |
| [actor-gateway-session-relay.ko.md](./actor-gateway-session-relay.ko.md) | STREAM session과 actor를 ActorGateway로 bind/relay하는 C++ standalone runtime 초안 |
| [cpp-stream.ko.md](./cpp-stream.ko.md) | framework Header 기반 packet stream과 handler 통합 |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | STREAM 결정 기록 |
| [cpp-stream-connector.ko.md](./cpp-stream-connector.ko.md) | C++용 Stream Connector 별도 라이브러리와 배포 정책 |
| [cpp-monitoring.ko.md](./cpp-monitoring.ko.md) | runtime monitoring 등록, typed event, 운영 샘플 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | stage 같은 상위 모델을 `SPOT` 위에 감쌀 때의 조건 |
| [cpp-registry.ko.md](./cpp-registry.ko.md) | embedded registry, query, topology 조회 |

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
| `Bingo` | channel/SPOT 중심 기본 실시간 메시징 샘플 | app/host, DI, channel request/reply, publish/subscribe, callback submit, coroutine submit, handler error, user Spot, timer, monitoring, graceful shutdown, offload handler |
| `TicTacToe` | STREAM과 ActorGateway 기반 actor/session relay 샘플 | STREAM endpoint, ActorGateway attach, Entry Spot, actor factory, session actor bind, relay, bound session push, actor join/move, disconnect cleanup |

`Bingo`에는 STREAM ActorGateway relay 변형을 두지 않는다. 해당 방식의 기준 샘플은
`TicTacToe` 하나로 두고, 별도 접미사를 붙이지 않는다.

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
  Spot timer는 Entry Spot 전체를 전역 직렬화하지 않는다.
