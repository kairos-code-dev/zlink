<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Pub/Sub](config-3-pubsub.ko.md) | [다음: Resilience](config-5-resilience-lifecycle.ko.md)
<!-- framework-adapter-nav:end -->

# Config 4 — 등록·codec 변주 배포

handler 등록 방식과 codec을 바꿔 가며, 변주가 실 배포에서 같은 의미로 동작하는지 검증한다.

두 가지 실제 제약을 전제한다:

- 같은 channel에 같은 kind+packet name을 두 번 등록하면 startup 예외다. 그래서 **등록 variant는
  variant별 packet name(또는 variant별 channel)**으로 분리한다.
- codec은 channel별 설정이 아니라 host **전역 registry**(`options.Codecs`)다. payload 타입/
  content-type으로 codec이 선택되며, Protobuf는 `IMessage` 타입만, MessagePack은
  `[MessagePackObject]` 타입만 매칭하고 미지원 타입은 JSON으로 fallback한다. 그래서 codec
  검증은 **codec별 전용 DTO**를 쓰고 reply의 `context.ContentType`을 함께 본다.

## 1. 목적과 범위

- 다룬다: 자동/attribute/수동 등록(각 request·send), DI lifecycle, filter ordering, 등록 검증(startup), 전역 codec registry에서 JSON/Protobuf/MessagePack 공존(payload 타입/content-type 기반).
- 범위 밖: registry scale(Config 1), spot/stream(Config 2), pub/sub(Config 3).

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| server | 1 (`reg-codec-node`) | 전역 codec registry에 JSON(기본)·Protobuf·MessagePack codec을 등록. 등록 variant를 variant별 packet name으로 한 channel에 둔다(`EchoAuto`, `EchoAttr`, `EchoManual` …). codec별 전용 DTO 핸들러 포함. `/evidence`·`/health`. |
| client | 시나리오별 | variant별 packet name·DTO로 같은 의미의 request/send를 보낸다. |

handler 의미(공유): 각 등록 variant handler는 `Echo*Req(value)` → `echo:{value}` reply를
같은 의미로 구현한다. codec variant handler는 codec별 전용 DTO를 받아 같은 의미를 구현한다.
검증의 핵심은 "등록 방식이나 codec이 달라도 같은 reply·evidence가 나오는가"다.

## 3. 실행 모델

`run_e2e.sh`가 server를 띄우고, client 시나리오가 variant별 packet/DTO로 messaging을 실행해
결과 동등성과 content-type을 확인한다.

## 4. 시나리오

### Track A — handler 등록

#### RC-A1 assembly·module 자동 등록

우선순위: `P0`

- 절차: 자동 스캔으로 등록된 handler(packet `EchoAuto`)로 request와 send를 보낸다.
- 검증: 수동 등록 없이도 request reply와 send evidence가 정상이다.
- 세부 동작: 자동 등록 경로(request·send).

#### RC-A2 attribute 등록 (.NET)

우선순위: `P0`

- 절차: `[ZLinkHandlerGroup]` + `[ZLinkRequest]`/`[ZLinkSend]`로 표시한 handler(packet `EchoAttr`)로 request와 send를 보낸다.
- 검증: attribute 기반 등록이 동작하고 자동/수동과 같은 reply·evidence 의미. (annotation/decorator는 .NET이 아닌 다른 언어 표면 — 언어별 mapping은 공통 spec에서 다룬다.)
- 세부 동작: attribute 등록(request·send).

#### RC-A3 수동 handler 등록

우선순위: `P0`

- 절차: 코드로 명시 등록한 handler(packet `EchoManual`)로 request와 send를 보낸다.
- 검증: 자동/attribute 경로와 동일한 reply·evidence.
- 세부 동작: 명시 등록(대조군, request·send).

#### RC-A4 DI lifecycle

우선순위: `P1`

- 절차: scoped/singleton 의존성을 가진 handler로 연속 request를 보낸다. handler는 주입된 의존성의 instance id와 `IDisposable`/`IAsyncDisposable` dispose 횟수를 evidence에 남긴다.
- 검증: dispatch마다 async scope가 생기므로 scoped 의존성 instance id가 요청마다 달라지고, singleton은 같다. scoped 의존성의 dispose 카운터가 요청 수와 일치한다(누수 없음).
- 세부 동작: handler DI 수명(instance id·dispose 카운터 evidence).

#### RC-A5 filter ordering

우선순위: `P1`

- 절차: 여러 `IZLinkHandlerFilter`를 `UseFilter<TFilter>()`로 등록한 channel로 request를 보낸다.
- 검증: 등록 순서대로 filter before/after가 호출되고, evidence에 호출 순서가 남는다. (interceptor 개념은 지원이 확인된 언어에서만 별도 항목으로 둔다.)
- 세부 동작: filter 순서.

#### RC-A6 잘못된 등록은 시작 단계에서 차단

우선순위: `P0`

- 절차: 잘못된 registration(같은 channel에 같은 kind+packet 중복, 잘못된 handler group, 미지원 channel kind 조합)을 가진 server를 기동한다.
- 검증: 런타임이 아니라 **시작 단계**에서 명확한 registration 검증 오류로 기동이 실패한다. 정상 등록만 있으면 정상 기동. (handler의 미해결 DI 의존성은 startup이 아니라 dispatch 시점 실패이므로 별도 — 필요 시 명시적 DI validation 설정으로 분리.)
- 세부 동작: registration 검증 fail-fast.

### Track B — codec

#### RC-B1 JSON codec

우선순위: `P0`

- 절차: JSON DTO(POCO)로 request와 send를 보낸다.
- 검증: 정확히 직렬화·역직렬화되어 같은 reply·evidence가 나오고, reply `context.ContentType`이 `application/json`이다.
- 세부 동작: JSON codec round-trip + content-type.

#### RC-B2 Protobuf codec

우선순위: `P0`

- 절차: Protobuf `IMessage` 타입 DTO로 request와 send를 보낸다.
- 검증: 같은 메시지가 동일 의미로 왕복하고, `context.ContentType`이 `application/x-protobuf`다(JSON fallback이 아님).
- 세부 동작: Protobuf codec round-trip + content-type.

#### RC-B3 MessagePack codec

우선순위: `P1`

- 절차: `[MessagePackObject]` 타입 DTO로 request와 send를 보낸다.
- 검증: 같은 메시지가 동일 의미로 왕복하고, `context.ContentType`이 `application/x-msgpack`다.
- 세부 동작: MessagePack codec round-trip + content-type.

#### RC-B4 전역 registry에서 codec 공존

우선순위: `P0`

- 절차: 한 host의 전역 codec registry에 JSON·Protobuf·MessagePack을 함께 등록하고, payload 타입이 다른 메시지(POCO / `IMessage` / `[MessagePackObject]`)를 같은 server로 보낸다.
- 검증: 각 메시지가 payload 타입/content-type에 맞는 codec으로 처리되어 서로 간섭 없이 정확히 왕복한다(content-type별 분기). 미지원 타입은 JSON fallback이라는 정해진 규칙을 따른다.
- 세부 동작: 전역 registry의 타입/content-type 기반 codec 선택. (channel별 codec 지정 API는 없음.)

## 5. 완료 기준

- Track A·B의 `P0` 시나리오가 모두 통과한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.
- 변주(등록·codec)가 달라도 같은 reply·evidence 의미가 나오고, codec은 content-type으로 실제 선택을 확인한다.
