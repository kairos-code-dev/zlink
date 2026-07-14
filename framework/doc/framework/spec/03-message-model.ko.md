<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: ZLink Framework Interaction Model](02-interaction-model.ko.md) | [다음: 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md)
<!-- framework-adapter-nav:end -->


[문서 묶음](../common/README.ko.md) | [개요](01-overview.ko.md) | [상호작용 모델](02-interaction-model.ko.md) | [channel topology](server/10-channel-topology.ko.md) | [framework API](05-framework-api.ko.md) | [공통 sample](../common/sample/README.ko.md) | [공통 E2E](../common/e2e/README.ko.md) | [.NET](../dotnet/README.ko.md) | [Java](../java/README.ko.md) | [Node.js](../node/README.ko.md) | [C++](../cpp/README.ko.md)

# ZLink Framework Message Model

## 1. 목적

`ZLink Framework`의 서버 간 기본 메시지 단위는 내부적으로 `header + payload`
멀티파트다. 이 구조는 codec 교체와 metadata 전달을 함께 설명하기 쉽고,
요청/응답과 이벤트를 같은 큰 틀 안에서 다루기 좋다. 특히 payload를 header와 함께
하나의 직렬화된 객체로 다시 감싸지 않는다는 점이 이 문서의 핵심 계약이다.

application이 직접 호출하는 framework messaging 빌더 API(`requestToChannel`/`sendToChannel`/
`publish` 등)는 typed payload object를 받고, 등록된 serializer/codec registry가 내부에서
byte payload와 packet name을 확정해 `Message`로 변환한다. request/reply handler도 dispatch가
끝난 뒤 typed payload를 받는다. (이미 만들어진 `Message`를 직접 운반하는 `Message` 인자
저수준 표면도 함께 있다.)

payload object를 byte payload로 바꾸고 packet name을 확정하는 책임은 등록된
serializer/codec registry에 둔다. application의 기본 업무 API는 `Message`를 직접 만들지
않고 typed payload object를 넘긴다. `Message.from(...)`, `Message.From(...)`,
`message_t::from(...)` 같은 언어별 factory는 명시 raw API, transport harness, codec
serializer 구현처럼 byte payload 경계를 직접 다루는 곳에서만 사용한다. 수신 payload는
등록된 handler metadata로 typed handler 인자로 decode한다.

## 2. 기본 구조

현재 스펙은 서버 간 framework message의 내부 wire 수준에서 기본적으로 2개 part를
전제로 한다.

1. `header`
2. `payload`

이 구조는 권장 구현 세부가 아니라 framework adapter의 서버 간 메시지 계약이다.
framework가 `DEALER/ROUTER`, routed channel, `SPOT` channel로 서버 사이에 메시지를 보낼 때는
header와 payload를 하나의 직렬화된 객체로 합치지 않는다.

**이 계약은 framework envelope 계층의 것이다.** actor gateway와 bound-session 전달은 core가
소유하는 **하위 전송 계층**이며, 그 계층은 이 envelope 앞에 자신의 routed control과 gateway
control part를 덧붙인다. 그 구성은 [31 §12.1](server/31-session-actor-dispatch.ko.md)이 소유하며, 그
계층의 payload part 안에 여기서 정의한 envelope 또는 완전한 stream frame이 들어간다. framework
handler가 보는 part 인덱스는 언제나 아래 표의 것이다.

기본 part 의미는 아래와 같다.

| part | 내용 | 처리 기준 |
|------|------|-----------|
| `parts[0]` | framework header | route, dispatch, timeout, correlation, codec, packet name 판단에 필요한 작은 metadata |
| `parts[1]` | payload | 등록된 codec이 만든 bytes. handler dispatch가 확정된 뒤 필요한 타입으로 decode한다 |
| `parts[2...]` | 선택적 추가 payload | attachment나 내부 확장이 필요할 때만 사용한다 |

payload가 없는 메시지도 기본적으로 빈 payload part를 둔다. 이렇게 하면 receive path가
항상 `parts[0]`과 `parts[1]`을 기대할 수 있어 분기가 줄고, 나중에 payload가 생겨도
wire shape가 바뀌지 않는다.

이 계약은 다음 이유 때문에 필요하다.

- route와 dispatch는 header만 읽으면 된다. payload를 함께 파싱하면 handler를 고르기
  전부터 큰 payload를 불필요하게 처리하게 된다.
- payload는 이미 codec이 만든 byte payload다. 이것을 다시 header object 안에 넣어
  JSON 문자열이나 base64 같은 중간 표현으로 감싸면 복사와 크기 증가가 생긴다.
- zlink binding과 core transport는 multipart를 지원한다. framework가 이를 사용해야
  transport가 제공하는 메시지 경계를 그대로 살릴 수 있다.
- header와 payload 소유권이 분리되어야 retry, timeout, dispose, attachment 확장이 한
  곳에서 명확해진다.

따라서 아래 형태는 framework 서버 간 wire 계약으로 금지한다.

- `parts[0]` 하나에 `{ header, payload }`를 함께 직렬화한 envelope
- binary payload를 header object 내부 `byte[]` 필드로 넣어 다시 직렬화하는 형태
- dispatch 전에 payload까지 파싱해야 packet name을 알 수 있는 형태

다만 이것이 "항상 part가 2개뿐이다"를 뜻하지는 않는다. 앞으로 attachment나 추가
payload part가 필요해질 수 있으므로, wire 수준에서는 `parts[2...]` 확장 여지를 남겨
둔다.

프레임워크 공용 API에서는 이 구조를 그대로 드러내지 않을 수 있다.

- request handler는 보통 decoded payload 하나를 받는다.
- response도 보통 typed object 하나를 반환한다.
- metadata는 context에서 접근한다.
- stream은 예외적으로 session packet과 connection, peer 정보가 먼저 보일 수 있다.

outbound messaging API는 typed object를 받고 framework 내부 serializer를 호출하는
표면을 기본으로 한다. 호출자가 이미 만든 byte payload나 `Message`를 전달하는 표면은
raw transport, codec 구현, 검증 도구처럼 byte 경계를 직접 다뤄야 하는 경우에만 둔다.
일반 application 호출부에서 codec 선택과 packet name 결정 규칙을
`send/request/reply/join`마다 반복하지 않는다.

## 2.1 STREAM packet과의 경계

`STREAM`은 서버 간 framework message와 다른 wire 경로다. stream connector와 stream
session은 하나의 stream packet을 보내고 받는다. 그 packet 내부에 stream header와
payload framing이 들어간다.

STREAM 경로의 기본 모양은 아래와 같이 본다.

| 경로 | wire message shape |
|------|--------------------|
| 서버 간 framework channel / route / SPOT / internal actor dispatch | multipart `framework header` + `payload` |
| STREAM client/server packet | 단일 packet message 안의 stream header/payload frame |

STREAM을 multipart로 쪼개지 않는 이유는 stream transport에서 packet framing 자체가
연결 세션의 wire 계약이기 때문이다. stream packet은 client connector, TLS/WS/TCP
transport, session request sequence가 같은 frame을 기준으로 맞물린다. 반대로 서버 간
framework route는 이미 zlink multipart message를 기본 단위로 다루므로 header와 payload를
별도 part로 유지해야 한다.

## 3. header가 담아야 할 정보

| 필드 | 용도 |
|------|------|
| `message-kind` | `Request=1`, `Response=2`, `Command=3`, `Publish=4`, `Error=5`로 고정한다. dispatch key 문맥은 `{Request, Command, Publish}` 셋이다. `Response`와 `Error`는 client측 reply correlation 전용이라 dispatch key로 노출하지 않는다. |
| `channel` | 논리 channel 이름 |
| `packet-name` | handler 선택에 쓰는 이름. **`Request`·`Command`·`Publish`에만 둔다** — `Response`와 `Error`는 handler를 고르지 않으므로 이 필드를 두지 않는다 |
| `content-type` | payload codec 식별 |
| `correlation-id` | **흐름 추적용 키**(로그·observer 상관). request/response 매칭 키가 **아니다** |
| `deadline` 또는 `timeout` | 시간 제한 전달 |
| `error-code` | 공통 에러 코드 |
| `error-message` | 실패 원인을 설명하는 문자열 |
| `source` | 호출자 식별 정보 |
| `target` | 필요할 때 명시적 대상 정보 |
| `flow-id` | 여러 단계 호출과 메시지 경계를 잇는 전역 추적 정보. 정확한 생성·전파 계약은 [메시지 흐름 상관관계](server/53-flow-correlation.ko.md)가 소유한다. |
| `causation-id` | 어떤 이전 메시지에서 파생됐는지 식별 |

모든 framework message는 `message-kind`와 `content-type`을 포함한다. **`packet-name`은 dispatch
key 문맥(`Request`·`Command`·`Publish`)에만 둔다** — `Response`와 `Error`는 packet name을 담지
않는다. channel 경로는 `channel`을 포함한다. **request sequence는 이 envelope header의 필드가 아니라
전송 계층이 소유한다**(아래 "reply 상관관계"). `correlation-id`는 흐름 추적을 켠 경우에만
채운다. 성공 결과는 `Response`, 실패 결과는 `Error`로
구분하며 별도의 `status` 필드는 두지 않는다. `error-code`와 `error-message`는
`Error`에만 둔다. route가 대상을 명시해야 하는 경로만 `source`와 `target`을
포함한다. deadline, flow-id와 causation-id는 해당 기능을 사용한 경우에만 포함한다.

`message-kind` 숫자와 필드 조합은 wire 계약이다. 수신자는 아래 조합을 유효한
메시지로 처리하고, 그 밖의 조합은 protocol 오류로 처리한다.

| `message-kind` | 숫자 | 용도 | 필드 제약 |
|----------------|------|------|-----------|
| `Request` | `1` | 응답을 요구하는 요청 | 전송 계층이 **request sequence**를 붙인다. `error-code`와 `error-message`를 두지 않는다. |
| `Response` | `2` | 성공한 요청의 결과 | 전송 계층이 원래 요청과 **같은 request sequence**를 되돌린다. **`packet-name`을 두지 않는다.** `error-code`와 `error-message`도 두지 않는다. |
| `Command` | `3` | 응답을 요구하지 않는 전송 | reply로 처리하지 않는다. `error-code`와 `error-message`를 두지 않는다. |
| `Publish` | `4` | 구독자에게 발행하는 메시지 | reply로 처리하지 않는다. `error-code`와 `error-message`를 두지 않는다. |
| `Error` | `5` | 실패한 요청의 결과 | 전송 계층이 원래 요청과 **같은 request sequence**를 되돌리고, 비어 있지 않은 `error-code`가 필요하다. **`packet-name`을 두지 않는다.** `error-message`에는 호출자에게 전달할 실패 설명을 둘 수 있다. |

### reply 상관관계 — sequence 단독

**응답이 어느 요청의 응답인지는 request sequence만으로 판정한다.** `Response`와 `Error`를 받은
쪽은 그 sequence로 pending request를 찾아 완료시킨다.

**sequence는 envelope header가 아니라 전송 계층이 소유한다.**

| 경로 | sequence를 소유하는 곳 |
|------|------------------------|
| channel / route (CS) | **core socket의 request/reply 상관관계**. framework envelope header에는 sequence 필드가 없다 |
| STREAM (SS) | **stream header의 `request_seq`**([32 §4](stream-connector/32-stream-connector.ko.md)) |

- **`Response`와 `Error`는 packet name을 담지 않는다.** 어느 요청의 응답인지는 sequence가 이미
  정하고, 응답은 handler를 고르지 않는다. 따라서 그 필드는 쓰이지 않는 잉여이며, 언어마다 다른
  값을 채워 넣어 진단만 어긋나게 만든다. **wire에서 뺀다.**
- **어떤 구현도 응답을 packet name으로 대조하지 않는다.** 필드 자체가 없으므로 대조할 수도 없다.
- **typed reply는 호출자가 지정한 reply 타입으로 바로 decode한다.** 이름으로 decode 타입을
  고르지 않는다.
- `Error`도 같은 sequence로 매칭한다. sequence가 없는 `Error`는 특정 request의 실패가 아니라
  연결·프로토콜 수준 오류다.
- 응답의 진단·로깅에는 packet name 대신 **원본 request의 이름**을 쓴다. 그 이름은 pending request
  항목이 이미 들고 있으므로 wire로 되돌릴 필요가 없다.

이 규칙은 channel request/reply와 STREAM session request/response에 **똑같이** 적용한다
([32 §5.2](stream-connector/32-stream-connector.ko.md)).

값 `0`, `1..5` 밖의 값, `status` 필드, `Response`에 오류 필드를 넣은 형태,
`Error`를 성공 payload처럼 사용하는 형태는 유효하지 않다. 이전의 `event` 이름이나
성공·실패를 `status`로 구분하는 envelope를 함께 해석하는 호환 decoder도 두지 않는다.

Spot worker offload에서 생긴 실패도 `error-code`에 보존한다. queue가 가득 찬 경우,
timeout이 난 경우, worker 함수가 예외를 낸 경우는 같은 `RequestFailed`로 뭉개지 않고
언어별 public 오류 분류로 전달되어야 한다. 이 구분이 있어야 caller가 재시도, 사용자
응답, 별도 service 위임 같은 후속 처리를 선택할 수 있다.

하지만 다시 강조하면, 이 필드들이 그대로 application handler 인자로
드러나는 것은 아니다. 현재 스펙은 아래 구분을 기본으로 본다.

- application handler 표면: typed payload + framework context
- adapter 내부 transport 표면: header + payload

## 4. payload codec 방향

`payload`는 특정 포맷으로 고정하지 않는다. framework의 기본 codec은 JSON이다.
Protobuf와 MessagePack은 framework core에 직접 넣지 않고 선택 extension package로 제공한다.
사용자가 만든 codec도 같은 extension 계약을 사용한다.

| codec | 설명 |
|-------|------|
| `json` | framework 기본값이다. 빠른 개발과 디버깅에 적합하다. |
| `protobuf` | schema가 분명한 typed contract에 적합하다. 선택 extension package로 추가한다. |
| `messagepack` | compact binary payload가 필요할 때 적합하다. 선택 extension package로 추가한다. |
| custom codec | Avro, Thrift, 사내 binary format처럼 사용자가 별도 package로 추가한다. |

이 문서의 기본 예시는 주로 `protobuf`와 `json`을 기준으로 설명한다.
다만 framework adapter는 transport 본체에 codec 구현을 직접 섞지 않고, framework codec
extension 계층으로 연결하는 방향을 기본으로 본다.

같은 extension은 framework, stream connector, HTTP client에서 공유한다. connector 전용 codec
package나 bindings codec package를 따로 두지 않는다. bindings는 raw `Message`, byte payload,
core protocol API만 제공한다.

`ZLink Framework`는 "payload가 어떤 codec인가"를 handler와 client가 알 수 있게
해 주되, core transport가 그 codec 내용을 직접 이해하려고 하지는 않는 방향이
맞다.

## 5. 요청과 응답의 기본 의미

### 5.1 request

- `message-kind = Request(1)`
- `correlation-id` 필수
- `packet-name` 필요
- local `ROUTER(server)`가 받은 request는 `packet-name` 기준으로 handler에 dispatch한다

### 5.2 response

- 성공이면 `message-kind = Response(2)`
- 같은 `correlation-id`를 되돌려 준다
- `status`, `error-code`, `error-message`를 넣지 않는다
- outbound client가 받은 response는 일반 handler dispatch 대상이 아니라,
  먼저 보낸 request의 pending reply를 완료하는 데 쓴다

### 5.3 error

- 실패하면 `message-kind = Error(5)`
- 같은 `correlation-id`와 비어 있지 않은 `error-code`를 되돌려 준다
- 호출자에게 전달할 설명이 있으면 `error-message`에 넣는다
- `Error`는 실패 reply이며 일반 handler dispatch 대상이 아니다

### 5.4 command

- `message-kind = Command(3)`
- `packet-name` 필요
- 응답 payload를 전제로 하지 않는다
- local `ROUTER(server)`가 받은 command는 `packet-name` 기준으로 send handler에
  dispatch한다

## 6. 발행 메시지의 기본 의미

발행 메시지는 응답을 기대하지 않으므로 아래가 핵심이다.

- `message-kind = Publish(4)`
- `packet-name` 필수
- 선택적 metadata

## 7. stream에 대한 별도 메모

`STREAM`은 다른 모델과 같은 `header + payload` 추상화로 모두 덮기 어려울 수 있다.
특히 아래 정보가 더 중요할 수 있다.

- peer 또는 session 식별값
- connection open/close 수명
- packet framing 규약

따라서 stream은 공통 message model을 일부 공유하더라도, framework 표면에서는
별도 context와 handler 계약을 둘 가능성이 높다.

여기서 중요한 점은 `STREAM`이 이 서버 간 envelope의 `message-kind`에 새 값을
추가하지 않는다는 점이다. 서버 간 envelope의 값은 `Request`, `Response`, `Command`,
`Publish`, `Error` 다섯 가지로 고정하고, `STREAM`은 별도 session contract로 설명한다.

## 8. 이 문서의 범위

- 이 문서는 공용 logical message kind와 header 의미만 정한다.
- header의 binary encoding 형식과 serializer 내부 규칙은 binding 또는 codec 확장
  문서에서 다룬다.
- payload 없는 메시지 표현은 현재 구현과 공개 계약이 필요로 할 때 별도 spec에서
  다룬다. 응답 성공 여부를 나타내는 별도 status code 체계는 이 envelope에 추가하지
  않는다.
- `STREAM` monitor 이벤트를 session error로 어디까지 승격할지는 `STREAM` 바인딩
  문서에서 다룬다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](README.ko.md) | [이전: ZLink Framework Interaction Model](02-interaction-model.ko.md) | [다음: 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md)
<!-- framework-adapter-nav:bottom:end -->
