<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Pub/Sub](config-3-pubsub.ko.md) | [다음: Resilience](config-5-resilience-lifecycle.ko.md)
<!-- framework-adapter-nav:end -->

# Config 4 — 등록·codec 변주 배포

handler를 등록하는 방식과 codec(직렬화 방식)을 이리저리 바꿔 보면서, **방식이 달라져도 결과는
같은가**를 확인하는 config다.

먼저 알아 둘 실제 제약 두 가지:

- 같은 channel에 같은 kind+packet name을 두 번 등록하면 startup에서 예외가 난다. 그래서 등록
  방식별 variant는 **variant마다 다른 packet name(또는 다른 channel)**으로 분리한다.
- codec은 channel마다 따로 거는 게 아니라 host **전역 registry**(`options.Codecs`)에 둔다. payload
  타입/content-type을 보고 codec이 골라지는데, Protobuf는 `IMessage` 타입만, MessagePack은
  `[MessagePackObject]` 타입만 매칭하고, 매칭이 안 되는 타입은 JSON으로 fallback한다. 그래서
  codec 검증은 **codec별 전용 DTO**를 쓰고, reply의 `context.ContentType`까지 함께 본다.

## 1. 목적과 범위

- 다룬다: 자동/attribute/수동 등록(각 request·send), DI lifecycle, filter ordering, 등록 검증(startup), 전역 codec registry에서 JSON/Protobuf/MessagePack 공존(payload 타입/content-type 기반).
- 여기서 다루지 않는 것: 연결·scale·resolve(Config 1), spot/stream(Config 2), pub/sub(Config 3).

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| server | 1 (`reg-codec-node`) | 전역 codec registry에 JSON(기본)·Protobuf·MessagePack codec을 등록. 등록 variant를 variant별 packet name으로 한 channel에 둔다(`EchoAuto`, `EchoAttr`, `EchoManual` …). codec별 전용 DTO 핸들러 포함. `/evidence`·`/health`. |
| client | 시나리오별 | variant별 packet name·DTO로 같은 의미의 request/send를 보낸다. |

handler 의미(공유): 등록 방식이 다른 handler들도 전부 `Echo*Req(value)` → `echo:{value}` reply를
같은 의미로 구현한다. codec variant handler는 codec별 전용 DTO를 받아 같은 의미를 구현한다.
즉 이 config가 보려는 핵심은 하나다 — "등록 방식이나 codec이 달라도 같은 reply·evidence가
나오는가".

연결 전제: 이 config는 위치 resolve를 다루지 않으므로 client는 server endpoint에 직접
연결한다(수동 연결). 공유 location store는 등록하지 않는다. 단일 process smoke에서 위치 조회를
추가로 검증할 때만 process-local `IZLinkLocationStore` 구현체를 `AddLocationStore(instance)`로
등록한다.

## 3. 실행 모델

`run_e2e.sh`가 server를 시작하고, client 시나리오가 variant별 packet/DTO로 messaging을 실행해 결과가
같은지와 content-type을 확인한다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — handler 등록

#### RC-A1 assembly·module 자동 등록

우선순위: `P0`

**검증 질문:** 자동 스캔만으로 등록된 handler가, 수동 등록 없이도 request·send를 제대로 처리하는가.

- 절차: 자동 스캔으로 등록된 handler(packet `EchoAuto`)로 request와 send를 보낸다.
- 검증: 수동 등록 없이도 request reply와 send evidence가 정상이다.
- 세부 동작: 자동 등록 경로(request·send).

#### RC-A2 attribute 등록 (.NET)

우선순위: `P0`

**검증 질문:** attribute로 표시한 handler가, 자동/수동 등록과 똑같은 결과를 내는가.

- 절차: `[ZLinkHandlerGroup]` + `[ZLinkRequest]`/`[ZLinkSend]`로 표시한 handler(packet `EchoAttr`)로 request와 send를 보낸다.
- 검증: attribute 기반 등록이 동작하고 자동/수동과 같은 reply·evidence 의미. (annotation/decorator는 .NET이 아닌 다른 언어 표면 — 언어별 mapping은 공통 spec에서 다룬다.)
- 세부 동작: attribute 등록(request·send).

#### RC-A3 수동 handler 등록

우선순위: `P0`

**검증 질문:** 코드로 직접 등록한 handler가 자동/attribute 경로와 똑같은 결과를 내는가(대조군).

- 절차: 코드로 명시 등록한 handler(packet `EchoManual`)로 request와 send를 보낸다.
- 검증: 자동/attribute 경로와 동일한 reply·evidence.
- 세부 동작: 명시 등록(대조군, request·send).

#### RC-A4 DI lifecycle

우선순위: `P1`

**검증 질문:** 요청마다 scoped 의존성은 새로 생기고 singleton은 그대로이며, 공개 DI 표면이 제공하는 수명 정리 증거가 일관적인가.

- 절차: scoped/singleton 의존성을 가진 handler로 연속 request를 보낸다. handler는 주입된 의존성의 instance id와 `IDisposable`/`IAsyncDisposable` dispose 횟수를 evidence에 남긴다.
- 검증: dispatch마다 새 scope 또는 그 언어의 같은 의미를 가진 request context가 생기므로 scoped 의존성 instance id가 요청마다 달라지고, singleton은 같다. DI 컨테이너가 공개 API로 dispatch scope dispose를 보장하면 scoped 의존성의 dispose 카운터가 요청 수와 일치해야 한다.
- Node/Nest처럼 public `ModuleRef`가 request context 생성과 resolve는 제공하지만 dispatch 뒤 context dispose API를 제공하지 않는 경우, E2E는 per-dispatch scoped instance 분리와 singleton 안정성을 검증한다. 내부 wrapper 저장소 삭제나 테스트 전용 adapter로 dispose counter를 맞추지 않는다.
- 세부 동작: handler DI 수명(instance id·공개 수명 정리 evidence).

#### RC-A5 filter ordering

우선순위: `P1`

**검증 질문:** 여러 filter를 등록한 순서대로 before/after가 호출되는가.

- 절차: 여러 `IZLinkHandlerFilter`를 `UseFilter<TFilter>()`로 등록한 channel로 request를 보낸다.
- 검증: 등록 순서대로 filter before/after가 호출되고, evidence에 호출 순서가 남는다. (interceptor 개념은 지원이 확인된 언어에서만 별도 항목으로 둔다.)
- 세부 동작: filter 순서.

#### RC-A6 잘못된 등록은 시작 단계에서 차단

우선순위: `P0`

**검증 질문:** 잘못된 등록은 런타임까지 가지 않고 시작 단계에서 명확한 오류로 막혀, 기동 자체가 실패하는가.

- 절차: 잘못된 registration(같은 channel에 같은 kind+packet 중복, 잘못된 handler group, 미지원 channel kind 조합)을 가진 server를 기동한다.
- 검증: 런타임이 아니라 **시작 단계**에서 명확한 registration 검증 오류로 기동이 실패한다. 정상 등록만 있으면 정상 기동. (handler의 미해결 DI 의존성은 startup이 아니라 dispatch 시점 실패이므로 별도 — 필요 시 명시적 DI validation 설정으로 분리.)
- 세부 동작: registration 검증 fail-fast.

### Track B — codec

#### RC-B1 JSON codec

우선순위: `P0`

**검증 질문:** JSON DTO가 정확히 왕복하고, reply content-type이 `application/json`인가.

- 절차: JSON DTO(POCO)로 request와 send를 보낸다.
- 검증: 정확히 직렬화·역직렬화되어 같은 reply·evidence가 나오고, reply `context.ContentType`이 `application/json`이다.
- 세부 동작: JSON codec round-trip + content-type.

#### RC-B2 Protobuf codec

우선순위: `P0`

**검증 질문:** Protobuf DTO가 JSON fallback이 아니라 실제 Protobuf로 왕복하는가(content-type `application/x-protobuf` 확인).

- 절차: Protobuf `IMessage` 타입 DTO로 request와 send를 보낸다.
- 검증: 같은 메시지가 동일 의미로 왕복하고, `context.ContentType`이 `application/x-protobuf`다(JSON fallback이 아님).
- 세부 동작: Protobuf codec round-trip + content-type.

#### RC-B3 MessagePack codec

우선순위: `P1`

**검증 질문:** MessagePack DTO가 실제 MessagePack으로 왕복하는가(content-type `application/x-msgpack` 확인).

- 절차: `[MessagePackObject]` 타입 DTO로 request와 send를 보낸다.
- 검증: 같은 메시지가 동일 의미로 왕복하고, `context.ContentType`이 `application/x-msgpack`다.
- 세부 동작: MessagePack codec round-trip + content-type.

#### RC-B4 전역 registry에서 codec 공존

우선순위: `P0`

**검증 질문:** 세 codec을 한 host에 같이 등록해 두고 타입이 다른 메시지를 섞어 보내면, 각자 맞는 codec으로 골라져 서로 간섭 없이 왕복하는가.

- 절차: 한 host의 전역 codec registry에 JSON·Protobuf·MessagePack을 함께 등록하고, payload 타입이 다른 메시지(POCO / `IMessage` / `[MessagePackObject]`)를 같은 server로 보낸다.
- 검증: 각 메시지가 payload 타입/content-type에 맞는 codec으로 처리되어 서로 간섭 없이 정확히 왕복한다(content-type별 분기). 미지원 타입은 JSON fallback이라는 정해진 규칙을 따른다.
- 세부 동작: 전역 registry의 타입/content-type 기반 codec 선택. (channel별 codec 지정 API는 없음.)

#### RC-B5 codec registry 불일치 (peer 간)

우선순위: `P1`

**검증 질문:** 서로 codec 등록이 다른 두 서비스가 통신할 때 보낸 쪽의 명시적 content-type을 받는 쪽이
모르면 `PayloadDecodeFailed`로 끝나고 정상 codec 트래픽은 유지되는가.

- 절차: server는 JSON만 등록하고, client는 MessagePack(또는 Protobuf) 전용 DTO로 같은 server에 request를 보낸다(=받는 쪽이 그 content-type codec을 안 가진 상황).
- 검증: 명시된 non-JSON content-type에 맞는 codec이 없으면 JSON으로 해석하지 않고 public error kind
  `PayloadDecodeFailed`로 terminal 완료한다. 같은 server의 정상 JSON 트래픽은 영향받지 않는다. JSON
  fallback은 outbound에서 명시적 non-JSON content-type을 선택하지 않은 미지원 타입에만 적용한다.
- 세부 동작: peer 간 codec registry 불일치의 `PayloadDecodeFailed` 분류와 정상 JSON 격리.

## 5. 완료 기준

- Track A·B의 `P0` 시나리오가 모두 통과한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.
- 변주(등록·codec)가 달라도 같은 reply·evidence 의미가 나오고, codec은 content-type으로 실제 선택을 확인한다.
