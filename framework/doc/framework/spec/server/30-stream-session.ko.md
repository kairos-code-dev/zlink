# STREAM 서버 세션 — 공통 스펙

[스펙 목차](../README.ko.md) | [이전: Stage Wrapper On SPOT](25-stage-wrapper-on-spot.ko.md) | [다음: Session Actor Dispatch](31-session-actor-dispatch.ko.md)

이 문서는 ZLink Framework 11.0.0의 서버 쪽 STREAM session 공개 계약을 정의한다. 대상 독자는 서버 session
표면, dispatch, 등록, codec과 오류 경계를 구현하는 framework 개발자다. Client 쪽 계약은
[Stream Connector 공통 스펙](../stream-connector/32-stream-connector.ko.md)이 소유하며 두 문서는 같은 wire
계약을 공유한다. 언어별 타입과 시그니처는 `languages/<lang>/`의 STREAM 문서가 고정한다.

## 1. 목적

STREAM은 일반 request-response와 성격이 다르다. 다음이 훨씬 중요한 축이 된다.

- 연결 수명
- peer 식별
- packet framing
- session lifecycle

**framework는 STREAM을 "header 기반 packet session"으로 다룬다.** raw byte stream을 그대로
application에 넘기지 않는다.

## 2. 기본 방향

- **framework가 stream header를 decode한 뒤** dispatch context와 payload를 session callback에
  전달한다.
- **header는 framework 내부에서 packet name과 metadata로 해석한다.** application은 packet name을
  보고 필요한 타입으로 decode한다.
- **payload decode를 transport 본체에 섞지 않는다.** framework runtime이 등록된 codec registry로
  message를 만들고, application은 필요한 packet만 decode한다(§5).
- session lifecycle callback(**연결·연결 해제**)을 기본 표면으로 올린다.
- **오류 callback은 application 예외가 아니라, 그 session에 귀속되는 transport 오류만** 올린다(§6).

**범위에서 제외하는 것:**

- **application이 직접 recv loop를 돌리는 방식**(§4)
- **raw chunk 직접 처리**
- **사용자 정의 header framing** — header binary 형식은 framework와 connector가 공유하는 내부
  프로토콜로 고정된다. **application은 이 형식을 바꾸는 설정을 갖지 않는다.**

## 3. Dispatch 모델

**session callback은 transport callback을 직접 실행하지 않는다.** framework가 관리하는 queue를
거쳐 dispatch한다. 그래야 dispatch·DI·logging을 일관되게 묶을 수 있다. **handler filter
파이프라인은 ChannelName dispatch 전용이며 Node direct·Spot·Actor·STREAM session dispatch에는 적용하지 않는다**
([Framework API §8](../05-framework-api.ko.md#8-handler-등록과-dispatch)).

- session callback이 받는 것은 **dispatch context**(packet name, metadata, request 정보 등)와
  **payload**다.
- **request header 값은 runtime이 dispatch context 안에서 보존한다.** application이 header 객체를
  만들거나 relay 호출에 다시 넘기지 않는다.
- transport callback에서 받은 **peer 식별 값(routing id)은 session dispatch까지 정보 손실 없이**
  전달된다.

### 3.1 reply 상관관계

**session이 만드는 `Response`·`Error`는 원본 request의 request sequence를 그대로 되돌린다.**
client는 그 sequence만으로 pending request를 찾는다.

- **`Response`·`Error` header에 packet name을 담지 않는다.** 응답은 handler를 고르지 않으므로 그
  필드가 필요 없고, 언어마다 다른 값을 채워 넣으면 진단만 어긋난다.
- typed reply의 decode 타입은 client가 호출 시 지정한 타입이다. 이름으로 고르지 않는다.
- `Error`도 같은 sequence로 되돌린다.

전체 규칙은 [03 message model](../03-message-model.ko.md)의 "reply 상관관계"가 소유한다.

## 4. recv loop를 기본 표면에서 빼는 이유

recv 방식은 low-level binding에서는 의미가 있다. 하지만 framework 공개 표면에 그대로 노출하면
문제가 생긴다.

- framework가 dispatch·DI·logging을 일관되게 묶기 어려워진다.
- **application이 loop·취소·backpressure를 직접 떠안게 된다.**
- header 기반 packet dispatch를 일관된 모델로 설명하기 어려워진다.

**recv 기반 사용을 금지하는 것이 아니다. framework의 기본 application 표면으로 올리지 않는다는
뜻이다.**

## 5. Codec 계층 분리

**framework 기본 표면은 session·session context·stream·message까지만 유지한다.**

- 객체 변환은 raw transport message가 아니라 **framework message와 별도 codec extension**이
  맡는다.
- **raw transport나 framework 기본 runtime에 특정 codec 구현을 직접 섞지 않는다.**
- **session handler는 codec별 helper를 직접 호출하지 않는다.** JSON·Protobuf·MessagePack·custom
  codec을 바꿔도 업무 코드는 같은 decode 표면을 쓴다.
- server framework, HTTP client host와 stream connector는 codec 번호, content-type과 typed payload 선택
  계약을 공유하지만 registry instance는 공유하지 않는다. Server는 server root별 registry, HTTP client는
  host별 registry([HTTP Client §5](../http-client/12-http-client.ko.md#5-codec)), connector는
  connector instance별 typed codec option([Stream Connector §5.4](../stream-connector/32-stream-connector.ko.md#54-codec))을 소유한다.

## 6. 오류 경계

| 오류 | 어디로 가는가 |
|---|---|
| **그 session에 귀속되는 transport 오류** | **session 오류 callback** |
| handshake 실패 | **runtime monitoring** — session callback에 올리지 않는다 |
| socket·node 단위 오류 | **runtime monitoring** — session callback에 올리지 않는다 |
| application handler 예외 | session 오류 callback이 아니다. handler 예외 처리 경로를 따른다 |

**session 오류 callback은 monitor에서 관찰 가능한 transport 오류를 session 단위로 다시 올려주는
축으로만 제한한다.**

세션이 닫힐 때의 종료 사유는 [Stream Connector §6.3](../stream-connector/32-stream-connector.ko.md#63-종료-사유)의 닫힌 집합과
정합하며, 계기는 [runtime-metrics §4](51-runtime-metrics.ko.md#4-object와-stream-계기)가 소유한다.

## 7. 등록 모델

**stream node 등록은 명시적으로 한다.** attribute·decorator 기반 암시 등록으로 열지 않는다.

등록 표면의 축:

| 축 | 의미 |
|---|---|
| **stream node 이름** | node 식별 |
| **bind endpoint** | **반드시 있어야 한다** |
| **session 타입 등록** | **한 stream node에는 session을 하나만 둔다** |

### 7.1 TLS

stream node는 **TLS를 켤 수 있다.** 켜면 **인증서 경로와 키 경로를 함께 지정해야 한다.**
client 인증서를 요구할지는 같은 server TLS 설정에서 선택한다. 기본값은 요구하지 않는 것이며,
요구하도록 설정하면 client certificate 검증에 실패한 연결은 session을 만들기 전에 거부한다.
client 쪽 transport 선택은 endpoint scheme이 결정한다([Stream Connector §3](../stream-connector/32-stream-connector.ko.md)).

### 7.2 Startup validation

다음은 host 시작 **전에** 설정 오류로 실패한다.

| 조건 | 결과 |
|---|---|
| **stream node 이름이 비어 있음** | 설정 오류 |
| **같은 stream node 이름을 두 번 등록** | 설정 오류 — node 이름은 runtime 식별자다 |
| **bind endpoint가 없음** | 설정 오류 |
| **같은 session 타입을 중복 등록** | 설정 오류 |
| **한 node에 session을 둘 이상 등록** | 설정 오류 |
| **TLS를 켰는데 인증서 경로가 비어 있음** | 설정 오류 |
| **TLS를 켰는데 키 경로가 비어 있음** | 설정 오류 |
| **client 인증서 요구를 TLS server 설정 없이 사용** | 설정 오류 |

등록 시점에 **이 node가 header 기반 packet 경로라는 사실이 분명히 드러나야 한다.**

## 8. Session에서 actor로

session이 받은 packet을 actor로 넘기는 계약은
[session-actor-dispatch](31-session-actor-dispatch.ko.md)가 소유한다.

**session callback은 spot 상태를 직접 만지지 않는다.** actor dispatch나 spot 호출을 제출하는
데까지만 책임진다([stage-wrapper-on-spot §3](25-stage-wrapper-on-spot.ko.md)).

Actor가 다른 MeshNode에 있어도 physical STREAM socket과 session object는 현재 session owner에 유지된다.
Framework는 bind control, Actor ingress와 Actor push만 MeshNode 사이 raw ROUTER service record로 전달한다.
Application에는 target Node RID, binding generation, authority fence와 command 24·36·38 codec을 노출하지 않는다.
Session close는 current binding generation의 tombstone을 제출하며 이전 bind의 늦은 close가 새 binding을
해제하지 못한다.

## 9. 검증 요구

| 항목 | 검증 |
|---|---|
| dispatch 경로 | session lifecycle과 packet dispatch가 transport callback을 직접 실행하지 않고 managed queue를 거친다 |
| peer 식별 보존 | transport callback의 routing id가 session dispatch까지 손실 없이 전달된다 |
| 등록 검증 | 같은 node에 session을 둘 이상 등록하면 startup에서 실패한다 |
| 오류 경계 | handshake·socket 오류가 session 오류 callback으로 올라오지 않는다 |
| 인증과 dispatch | connector와 session node 사이에서 인증과 packet dispatch가 완료된다 |
| 종료와 재개 | stream 종료로 pending request가 실패하고, 새 session의 인증·bind 뒤 messaging이 재개된다 |
| reply 상관관계 | `Response`·`Error` header에 packet name이 없고, client가 sequence 단독으로 매칭해 정상 완료한다 |
