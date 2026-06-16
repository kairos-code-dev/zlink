# Framework object messaging surface 초안

> 이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
> 현재 공개 계약은 각 언어의 정식 spec 문서와 실제 공개 코드가 기준이다.

## 목적

이 초안은 framework의 high-level 메시징 API 표면을 한 방향으로 정리한다.

여기서 high-level API는 애플리케이션이 직접 호출하는 outbound surface를 뜻한다.
handler가 typed 값을 반환하는 표면과 stream / session이 explicit reply builder를 쓰는
표면은 따로 구분해 다룬다.

정리 대상은 아래 application-initiated outbound API와 explicit reply API다.
언어에 따라 fanout client의 함수 이름이 조금 다를 수 있지만, 의미상 같은 송신 계열 표면도
같은 원칙으로 본다.

- `requestToChannel`
- `sendToChannel`
- `publish`
- `sendToSpot`
- `requestToSpot`
- `joinSpot`
- stream / session의 explicit `reply`

핵심 결정은 하나다.

**애플리케이션이 직접 사용하는 high-level 메시징 API는 `Message`를 받지 않고
업무 객체를 받는다. codec 선택과 payload 직렬화는 framework 내부에서 처리한다.**

## 배경

현재 저장소는 언어별 표면이 서로 다르다.

- 일부 언어는 channel / fanout 호출에서 업무 객체를 직접 받는다.
- 일부 언어는 `Message.from(...)`, `.ToJson()`, `.ToProto()` 같은 호출을 sample에서
  직접 사용한다.
- 일부 언어는 공개 인터페이스는 `Message`인데 sample과 문서는 업무 객체를 넘긴다.
- `joinSpot`은 channel 계열과 다르게 raw `Message` 또는 `message_t`가 남아 있는
  언어가 있다.

이 상태는 사용자가 배워야 할 규칙을 늘린다.

사용자는 원래 "어느 채널로 어떤 요청 객체를 보내고 어떤 응답 객체를 받는가"만
알면 된다. 그런데 현재 표면이 섞이면 아래 정보까지 호출자가 알아야 한다.

- 이 언어는 `Message.from(...)`가 필요한가
- 이 경로는 `.ToJson()`을 써야 하는가 `.ToProto()`를 써야 하는가
- `joinSpot`만 왜 다른 타입을 받는가
- packet name은 타입에서 자동 추론되는가, 직접 넣어야 하는가

이 초안은 이런 내부 결정을 호출자 코드에서 걷어내는 것을 목표로 한다.

## 목표

1. high-level framework 메시징 API를 업무 객체 기반 표면으로 통일한다.
2. request와 reply를 같은 원칙으로 다룬다.
3. channel, fanout, Spot outbound, actor join 표면을 같은 규칙으로 맞춘다.
4. codec 선택은 runtime 설정과 registry 안으로 숨긴다.
5. packet name은 기본적으로 타입 정보에서 자동 추론한다.
6. 자동 추론이 불가능한 경우에만 fluent override를 허용한다.
7. 대상 표면의 sample에서 codec helper와 raw payload helper를 제거한다.
8. 언어별 public surface가 서로 같은 뜻을 갖게 만든다.

## 비목표

- bindings의 low-level `Message` 자체를 제거하지 않는다.
- stream header, multipart payload, raw relay 같은 low-level transport API를
  이 문서에서 바꾸지 않는다.
- codec 패키지의 내부 구현 세부를 이 문서에서 정하지 않는다.
- 현재 ABI 호환성을 유지하기 위한 wrapper를 기본 목표로 두지 않는다.
- application이 의도적으로 raw bytes를 다루는 low-level surface를 금지하지 않는다.
- Spot create / Spot actor admission lifecycle callback 표면
  (`OnCreateAsync`, `OnActorJoinAsync`, `CreateAsync`, `GetOrCreateAsync` 등)은
  이 초안의 1차 대상에 포함하지 않는다.

## 기본 원칙

### 1. high-level API는 업무 객체를 받는다

호출자는 아래처럼 업무 객체를 넘긴다.

```ts
const allocated = await client
  .requestToChannel("play", new AllocateBingoRoomReq(actorId, mode))
  .submit<AllocateBingoRoomRes>();
```

```csharp
var allocated = await client
    .RequestToChannel("play", new AllocateBingoRoomReq { ActorId = actorId, Mode = mode })
    .Async<AllocateBingoRoomRes>(cancellationToken);
```

```java
var allocated = client
    .requestToChannel("play", new AllocateBingoRoomReq(actorId, mode))
    .await(AllocateBingoRoomRes.class);
```

호출자는 `Message.from(...)`, `.ToJson()`, `.ToProto()`, `to_stream_payload(...)` 같은
직렬화 helper를 직접 호출하지 않는다.

### 2. codec 결정은 framework 내부 책임이다

호출자가 넘긴 객체를 어떤 codec으로 직렬화할지는 framework가 runtime registry를 통해
결정한다. bindings extension은 framework가 고른 codec의 encode/decode 구현을 제공한다.

- JSON codec을 쓰는 runtime이면 JSON으로 직렬화한다.
- Protobuf codec을 쓰는 runtime이면 Protobuf로 직렬화한다.
- MessagePack codec을 쓰는 runtime이면 MessagePack으로 직렬화한다.

codec 선택은 아래 같은 runtime 설정 단계에서 끝나야 한다.

- `codecs.useJson()`
- `codecs.useProtobuf()`
- `codecs.useMessagePack()`
- `codecs.addJson(...)`
- `codecs.addProtobuf(...)`
- `codecs.addMessagePack(...)`

정확한 함수 이름은 언어별로 다를 수 있지만, **codec 선택이 호출부가 아니라
runtime 구성 단계에 있어야 한다**는 원칙은 같아야 한다.

### 3. reply도 같은 원칙을 따르되, 두 표면을 구분한다

request만 객체형이고 reply가 raw `Message`이면 표면이 반쪽이다.

아래 두 방향을 모두 framework 내부에서 처리해야 한다.

- request 객체 -> encoded `Message`
- encoded reply -> reply 객체

먼저 channel / Spot outbound request call의 terminator는 항상 reply 타입을 기준으로
결과를 돌려준다.

```ts
const reply = await client.requestToChannel("api", request).submit<AuthenticatePlayerRes>();
```

```java
AuthenticatePlayerRes reply =
    client.requestToChannel("api", request).await(AuthenticatePlayerRes.class);
```

`joinSpot`의 reply도 같은 원칙을 따라야 한다.

```ts
const joined = await actor.context.joinSpot(roomRid, new BingoRoomJoinReq(...))
  .submit<BingoRoomJoinRes>();
```

```csharp
var joined = await actor.Context
    .JoinSpot(roomRid, new BingoRoomJoinReq(...))
    .Async<BingoRoomJoinRes>(cancellationToken);
```

다만 reply 표면은 현재 언어별로 두 갈래다.

1. request handler가 typed reply 객체를 반환하는 표면
2. stream / session runtime이 explicit `.reply(...)` builder를 호출하는 표면

이 초안은 두 갈래 모두에서 호출자 또는 handler가 업무 객체를 다루고, `Message`와
codec은 내부로 숨긴다는 원칙을 요구한다.

즉, handler return 표면에서는 typed return이 canonical이고, stream / session explicit
reply 표면에서는 `.reply(replyObject)`가 canonical이다.

### 4. `Message`는 low-level transport 표현으로 남긴다

`Message`는 framework 내부와 low-level bindings surface에서는 계속 필요하다.
하지만 application-facing high-level API의 기본 입력 타입이 되어서는 안 된다.

`Message`가 남아도 되는 곳:

- bindings low-level send / recv surface
- stream relay
- raw multipart payload 처리
- runtime backend adapter
- testkit의 low-level transport 주입 지점

`Message`가 sample 표면에 보이면 안 되는 곳:

- channel request / send / publish
- Spot outbound request / send / publish
- actor `joinSpot`
- stream / session explicit reply

이 목록은 이 초안의 대상 표면에만 적용한다. Spot create / admission lifecycle callback
표면은 별도 정리 전까지 여기 포함하지 않는다.

### 5. packet name은 자동 추론이 기본이다

기본 동작은 객체 타입에서 packet name을 추론하는 것이다. 언어별 표면이 달라도,
high-level framework가 따라야 하는 공통 판단 단계는 아래와 같다.

1. builder override가 있으면 그 값을 우선 사용한다.
2. payload가 직접 제공하는 이름 정보(method, property, attached metadata)를 본다.
3. payload 타입에 붙은 선언적 metadata(annotation, attribute, decorator, registry entry)를 본다.
4. 그래도 없으면 안정적인 nominal type 정보(class name 등)를 본다.
5. 여전히 결정할 수 없으면 fail-fast로 실패시킨다.

즉, 아래 호출이 기본 표면이다.

```ts
client.requestToChannel("play", request).submit<Reply>();
```

아래 호출은 예외 경로다.

```ts
client.requestToChannel("play", request).packetName("AllocateBingoRoomReq").submit<Reply>();
```

이 예외 경로는 plain object literal, dynamic object, anonymous payload처럼 타입에서
이름을 안정적으로 얻을 수 없는 경우를 위한 것이다.

TypeScript / JavaScript 표면은 이 규칙을 더 구체적으로 적어야 한다.

Node high-level outbound API에서 packet name 자동 추론은 아래 순서를 따른다.

1. builder의 `.packetName(...)`
2. payload의 `packetName(): string`
3. payload class의 `@ZLinkPacket(...)` 같은 decorator metadata
4. non-`Object` constructor name
5. 그래도 결정하지 못하면 fail-fast

따라서 Node에서 아래 payload는 기본 경로로 허용된다.

- class instance
- `packetName()`을 제공하는 object
- `@ZLinkPacket(...)` metadata가 붙은 class instance

반대로 plain object literal이나 plain object factory 결과는 constructor가 `Object`이므로,
위 규칙만으로 packet name을 결정할 수 없으면 `.packetName(...)` override가 필요하다.

이것은 plain object를 금지한다는 뜻이 아니다. 다만 packet name을 payload 자체에서
얻을 수 없는 outbound call에서는 명시 override가 필요하다는 뜻이다.

## 목표 표면

이 초안은 언어별 문법 차이는 허용하지만 의미는 아래 표면으로 통일한다.

| API family | 목표 입력 | 목표 출력 |
|-----------|-----------|-----------|
| `sendToChannel` | 업무 객체 | 없음 |
| `requestToChannel` | 업무 객체 | 업무 reply 객체 |
| `publish` | 업무 이벤트 객체 | 없음 |
| `sendToSpot` | 업무 객체 | 없음 |
| `requestToSpot` | 업무 객체 | 업무 reply 객체 |
| `joinSpot` | 업무 join 요청 객체 | 업무 join reply 객체 |
| request handler return | 업무 request 객체 | 업무 reply 객체 |
| stream / session explicit `reply(...)` | 업무 reply 객체 | 없음 |

모든 high-level 표면은 내부에서 같은 흐름을 따른다.

```text
business object
  -> packet name resolution
  -> codec selection
  -> payload encoding
  -> low-level Message transport
  -> payload decoding
  -> business object
```

## 언어별 적용 원칙

### Node / TypeScript

- `requestToChannel`, `sendToChannel`, `publish`, `joinSpot`, stream / session
  `reply(...)`의 입력을
  `Message`에서 업무 객체 generic 표면으로 바꾼다.
- `Message.from(...)`는 low-level bindings와 내부 runtime에서만 사용한다.
- packet name 추론은 builder override, `packetName()` 메서드, decorator metadata,
  non-`Object` constructor name, fail-fast 순서로 처리한다.
- plain object literal이나 plain object factory 결과처럼 이름을 확실히 알 수 없는
  경우에만 `.packetName(...)` override를 허용한다.
- packet name을 자동 추론해야 하는 표준 sample payload는 class instance 또는
  `packetName()`/decorator metadata를 제공하는 타입을 기준으로 정리한다.
- 표준 guide와 sample 예시는 익명 object literal 중심의 JavaScript 스타일 대신,
  이름이 있는 class payload와 decorator 기반 TypeScript 표면을 우선 사용한다.

### .NET

- channel / fanout / Spot outbound의 객체형 표면은 유지한다.
- `JoinSpot`도 같은 원칙으로 업무 객체를 직접 받도록 맞춘다.
- `.ToJson()`, `.ToProto()` 같은 codec helper는 high-level sample에서 제거한다.
- Spot create / Spot admission lifecycle callback의 `Message` 표면은 이 초안의 1차
  migration 범위에 포함하지 않는다.
- packet name 자동 추론은 builder override, payload method/property, attribute 또는
  registry metadata, concrete type 정보 순서로 맞춘다.

### Java

- `ZLinkClient`, `ZLinkFanoutClient`, `ZLinkSpotOutbound`, `ZLinkActorContext.joinSpot`
  표면을 업무 객체 기반으로 통일한다.
- 현재 sample과 문서가 이미 보여 주는 객체형 표면을 정식 공개 계약으로 끌어올린다.
- `Message.from(...)`는 bindings low-level 예제와 내부 runtime에만 남긴다.
- packet name 자동 추론은 builder override, payload method/property, annotation 또는
  registry metadata, concrete class 정보 순서로 맞춘다.

### Kotlin

- Java core와 같은 의미를 갖도록 객체형 표면을 사용한다.
- coroutine extension도 `Message`를 받는 wrapper가 아니라 업무 객체를 받는
  high-level helper를 기준으로 맞춘다.
- packet name 자동 추론 규칙도 Java와 같은 판단 순서를 따른다.

### C++

- channel / fanout의 typed surface는 유지한다.
- `join_spot`도 `zlink::message_t`가 아니라 typed object를 받는 high-level
  wrapper를 표준 표면으로 삼는다.
- `to_stream_payload(...)`, `from_stream_payload(...)`는 low-level helper 또는
  내부 adapter로만 남긴다.
- Spot create / admission callback이 raw payload를 받는 표면은 별도 단계에서 다룬다.
- packet name 자동 추론은 builder override, payload type trait 또는 registry metadata,
  concrete type 정보 순서로 맞춘다.

## bindings extension 역할

codec 지원은 메시징 API 호출부가 아니라 bindings extension과 runtime registry가
흡수해야 한다.

예를 들어 Protobuf 지원은 아래처럼 보이는 것이 맞다.

1. runtime가 Protobuf codec extension을 등록한다.
2. framework가 요청 객체 타입과 reply 객체 타입을 보고 serializer를 찾는다.
3. serializer가 low-level `Message` payload를 만든다.
4. transport는 `Message`만 본다.
5. reply 수신 시 같은 registry로 reply 객체를 복원한다.

이 구조에서는 application이 codec helper를 호출할 이유가 없다.

## 제거 대상 패턴

이 초안이 구현되면 아래 패턴은 대상 표면의 high-level sample과 공개 guide에서
제거 대상이다.

- `requestToChannel(channel, Message.from(...))`
- `sendToChannel(channel, Message.from(...))`
- `publish(channel, topic, Message.from(...))`
- `joinSpot(rid, something.ToJson())`
- `joinSpot(rid, something.ToProto())`
- `join_spot(..., to_stream_payload(...))`
- `submit<Buffer>().then(decode...)`
- `await(... raw Message ...).reply().value(...)`

이 패턴들은 모두 codec 또는 payload 경계를 호출자 코드로 밀어내기 때문이다.

단, Spot create / Spot admission lifecycle callback 표면의 raw payload 처리는 이 초안의
제거 대상 범위에 포함하지 않는다.

## 허용되는 예외

아래 경우에는 명시적인 packet name 또는 raw `Message` surface가 필요할 수 있다.

- packet 이름을 타입에서 추론할 수 없는 동적 payload
- 프레임 단위 테스트
- transport adapter 테스트
- raw relay
- bindings 수준의 low-level sample

하지만 이런 예외는 framework의 주력 sample과 guide에서 기본 사용 예시가 되어서는
안 된다.

## 호환성 결정

이 초안은 기존 `Message` 기반 high-level public API와의 호환성을 유지하지 않는 방향을
기본값으로 둔다.

이유는 다음과 같다.

1. 객체형 표면과 `Message` 표면을 함께 유지하면 규칙이 둘이 된다.
2. sample과 문서가 어떤 표면을 표준으로 설명해야 하는지 다시 흐려진다.
3. codec helper 우회 경로가 계속 남아 호출자 코드가 정리되지 않는다.

필요하면 migration 기간 동안 wrapper를 둘 수는 있지만, 정식 문서와 sample은 새 표면만
설명해야 한다.

## 문서 반영 원칙

구현이 끝난 뒤 문서는 아래 순서로 반영한다.

1. framework 공통 문서에 이 원칙을 기본 설계 원칙으로 기록한다.
2. 언어별 spec 문서에서 high-level messaging surface 시그니처를 맞춘다.
3. 대상 표면의 guide 예제에서 `Message.from(...)`, `.ToJson()`, `.ToProto()`를 제거한다.
4. low-level bindings guide에서는 `Message`를 계속 설명하되, "framework high-level API와
   역할이 다르다"는 점을 분명히 적는다.

## 문서 수정 계획

이 초안과 정식 문서가 오래 함께 어긋나 있지 않게, 문서 수정 순서를 아래처럼 고정한다.

1. framework 공통 설계 문서에 "high-level outbound API는 업무 객체를 받고 codec과
   packet name 정책은 내부에서 처리한다"는 원칙을 먼저 올린다.
2. 언어별 framework spec 문서에서 `requestToChannel`, `sendToChannel`, `publish`,
   `requestToSpot`, `sendToSpot`, `joinSpot`, explicit `reply(...)` 표면을 새 원칙에
   맞춰 갱신한다.
3. 언어별 framework guide와 sample 문서에서 `Message.from(...)`, codec helper,
   raw decode 체인을 기본 예시에서 제거한다.
4. bindings 공통 policy와 언어별 `codec.md`에는 "codec extension은 object <->
   Message 변환만 담당하고, packet name / serializer lookup / typed reply decode는
   framework 책임"이라는 경계를 같은 문장으로 반영한다.
5. low-level bindings guide는 raw `Message` 사용법을 유지하되, framework high-level
   API와 책임이 다르다는 경고를 함께 둔다.

## 검증 기준

구현이 끝나면 아래 검증이 필요하다.

1. 모든 framework 언어에서 channel request / send / publish sample이 업무 객체를 직접
   넘긴다.
2. 모든 framework 언어에서 `joinSpot` sample이 업무 객체를 직접 넘긴다.
3. request handler return 표면에서는 typed reply return이 유지된다.
4. stream / session explicit reply 표면에서는 `.reply(replyObject)`가 동작한다.
5. packet name 자동 추론이 표준 sample에서 동작한다.
6. packet name 추론이 불가능한 경우 `.packetName(...)` override가 동작한다.
7. JSON / Protobuf / MessagePack sample이 같은 high-level 호출 모양을 유지한다.
8. low-level bindings sample만 `Message`를 직접 다룬다.

## 구현 순서 제안

1. 공통 원칙과 target surface를 확정한다.
2. Node public surface를 객체형으로 바꾼다.
3. Java / Kotlin public surface를 sample과 같은 방향으로 맞춘다.
4. .NET / C++의 `joinSpot`을 같은 원칙으로 정리한다.
5. bindings extension registry에서 request / reply codec lookup을 통일한다.
6. sample과 guide를 새 표면으로 모두 교체한다.
7. 언어별 conformance test를 추가해 다시 드리프트하지 않게 막는다.

## 한 문장 요약

framework의 high-level 메시징 표면은 **업무 객체를 받고 업무 객체를 돌려주는 표면**으로
통일하고, `Message`와 codec 세부는 **bindings와 runtime 내부**로 내려보낸다.
