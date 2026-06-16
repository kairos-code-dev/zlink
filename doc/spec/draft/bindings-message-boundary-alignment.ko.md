# Bindings message boundary alignment 초안

> 이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
> 현재 공개 계약은 각 바인딩의 정식 spec 문서와 실제 공개 코드가 기준이다.

## 목적

이 초안은 bindings 라이브러리와 framework 라이브러리의 메시징 책임 경계를 다시
정리한다.

핵심 결정은 아래와 같다.

1. bindings의 기본 `Message` 타입은 low-level bytes container로 유지한다.
2. codec extension은 object <-> `Message` 변환만 담당한다.
3. packet name 추론, codec 선택, typed reply decode 같은 high-level 메시징 정책은
   framework가 담당한다.

이 초안의 목적은 모든 bindings가 같은 방향으로 동작하게 만들어, 어떤 언어에서도
"low-level binding은 얇고, high-level 객체 메시징은 framework가 담당한다"는 규칙이
갈라지지 않게 하는 것이다.

## 배경

현재 저장소에는 framework 메시징 표면을 객체 기반으로 정리하려는 변경과,
bindings `Message` 자체에 object payload 또는 packet name metadata를 싣는 변경이
함께 섞여 있다.

이 상태는 책임 경계를 흐리게 만든다.

- framework가 객체를 받는다면 bindings는 다시 bytes 중심 표면으로 얇아져야 한다.
- bindings가 object payload와 packet name 정책까지 흡수하면 framework와 역할이 겹친다.
- 일부 언어만 그런 기능을 가지면 cross-language 표면이 다시 갈라진다.

특히 아래 두 방향은 동시에 표준이 될 수 없다.

1. framework가 객체 메시징을 canonical API로 제공한다.
2. bindings `Message`가 object payload, packet name 추론, typed value 보관까지 직접
   담당한다.

이 초안은 첫 번째 방향을 선택하고, 두 번째 방향은 bindings public contract에서
걷어낸다.

## 목표

1. 모든 bindings의 기본 `Message` 계약을 같은 수준으로 맞춘다.
2. codec extension public contract를 "object <-> Message 변환"으로 고정한다.
3. framework가 맡아야 할 high-level 정책이 bindings public surface로 새지 않게 한다.
4. Node, Java, .NET, C++, Go, Python, Rust가 같은 책임 분할을 갖게 한다.
5. sample과 guide에서 bindings helper와 framework helper의 역할을 분명히 구분한다.

## 비목표

- core C `zlink_msg_t` / `message_t` 계약을 이 문서에서 바꾸지 않는다.
- low-level bindings sample에서 `Message`를 직접 다루는 사용법을 금지하지 않는다.
- 각 언어 codec 구현체의 내부 라이브러리 선택을 이 문서에서 강제하지 않는다.
- framework의 handler registration, packet scanner, serializer registry 설계를
  이 문서에서 자세히 정의하지 않는다.

## 기본 원칙

### 1. bindings base `Message`는 bytes container다

모든 bindings에서 기본 `Message` 표면은 아래 성격만 가져야 한다.

- payload bytes 보관
- payload copy / close / size / read
- native interop에 필요한 low-level property

즉, base `Message`는 transport payload container다.

아래 기능은 base `Message`의 기본 책임이 아니다.

- 업무 객체 보관
- typed value 복원
- packet name 자동 추론
- framework packet metadata 정책
- 어떤 codec을 선택할지 결정

### 2. codec extension은 object <-> Message 변환만 담당한다

JSON, Protobuf, MessagePack codec extension은 아래 책임만 가진다.

- 업무 객체를 받아 `Message` payload로 encode
- `Message` payload를 받아 업무 객체로 decode

codec extension은 framework packet 정책을 소유하지 않는다.

즉, codec extension은 아래 같은 정보까지 결정하지 않는다.

- 어떤 channel request가 어떤 codec을 써야 하는가
- packet name을 어떤 규칙으로 정할 것인가
- request reply 타입을 어떻게 고를 것인가
- actor join reply를 어디서 decode할 것인가

### 3. high-level 메시징 정책은 framework가 담당한다

아래는 framework 책임이다.

- 객체 입력을 받는 outbound API
- packet name 자동 추론
- `.packetName(...)` override
- request / reply typed decode
- `joinSpot` request / reply typed decode
- stream / session explicit reply에서 객체 입력 처리
- serializer registry lookup

framework가 존재하는 언어에서는 codec extension 문서가 아래 정책을 대신 정의하면 안 된다.

- high-level outbound API가 어떤 serializer를 고르는가
- packet name을 어떤 순서로 추론하는가
- request reply를 어떤 타입으로 decode하는가
- `joinSpot` reply를 어디서 decode하는가

이 구조에서는 bindings와 framework의 역할이 명확히 갈린다.

```text
binding Message
  = bytes container

codec extension
  = object <-> Message converter

framework
  = object messaging policy
```

## 표준 책임 분할

| 계층 | 책임 | 가지면 안 되는 책임 |
|------|------|---------------------|
| bindings base `Message` | bytes payload, copy, close, size, native property | packet name 정책, typed value 정책, outbound object messaging policy |
| bindings codec extension | encode/decode helper | requestToChannel 정책, joinSpot 정책, framework reply 정책 |
| framework | 객체 기반 outbound API, packet name, serializer lookup, typed reply | low-level native message storage 구현 |

## 제거 대상

이 초안이 구현되면 bindings public contract에서 아래 기능은 제거 대상이다.

- base `Message.from(object)` 같은 object-aware constructor
- base `Message.value<T>()` 같은 typed object recovery helper
- base `Message.packetName()` / `withPacketName()` 같은 framework packet metadata
- codec extension이 encode 시 packet name을 자동 부착하는 동작
- 한 언어 binding에만 있는 high-level object messaging shortcut

이 항목들은 모두 framework 정책을 bindings public surface로 끌어올리기 때문이다.

## 유지 대상

아래는 bindings 쪽에 유지해도 된다.

- `Message.from(bytes)` / `Message.from(string)` / `Message.allocate(...)`
- `Message.toBytes()` / `data()` / `size()` / `close()`
- JSON codec의 `toMessage(...)` / `parseJson(...)`
- Protobuf codec의 `toMessage(...)` / `parseProto(...)`
- MessagePack codec의 `toMessage(...)` / `parseMessagePack(...)`
- low-level sample과 contract test에서의 raw `Message` 사용

## 현재 변경분에 대한 정리 기준

현재 저장소에 이미 들어간 관련 변경은 아래 기준으로 분류한다.

### 유지

- `.NET` JSON codec 기본 옵션 정리
- codec extension의 encode/decode helper 자체
- low-level `Message`의 기존 bytes container 기능

### framework로 이동

- packet name 자동 추론 규칙
- packet name override와 default resolution 정책
- 객체 request / reply의 typed decode 정책
- object payload를 직접 받는 high-level outbound call

### 제거

- bindings base `Message`에 새로 추가된 framework packet metadata
- bindings base `Message`에 새로 추가된 object payload 보관 정책
- codec extension이 encode 시 packet name을 자동 부착하는 동작

## 언어별 적용 원칙

### Node

- bindings `Message`는 bytes 중심 표면으로 되돌린다.
- `Message.from(object)`, `value()`, `packetName()`, `withPacketName()` 같은
  high-level 기능은 bindings public contract에서 제거한다.
- object payload 직렬화와 packet name 결정은 framework로 올린다.

### Java

- low-level `Message.packetName()` / `withPacketName()`는 bindings public contract에서
  제거한다.
- JSON / Protobuf / MessagePack codec은 encode/decode helper로만 남긴다.
- codec이 encode 시 packet name을 자동 부착하는 정책은 제거한다.

### .NET

- 현재 방향을 유지한다. bindings codec은 encode/decode helper에 머문다.
- 필요하면 다른 언어를 .NET 수준으로 맞춘다. 반대로 .NET `Message`에 framework 정책을
  올리지 않는다.

### C++

- `message_t`와 codec helper는 low-level 표면으로 유지한다.
- framework typed wrapper가 맡아야 할 정책을 C++ bindings `message_t`에 넣지 않는다.

### Go / Python / Rust

- object payload convenience가 필요하더라도 base binding이 아니라 framework 또는
  framework 문서가 따로 정의한 high-level helper 계층에 둔다.
- base `Message` public contract는 bytes 중심으로 유지한다.

## spec 반영 원칙

구현이 끝난 뒤 spec 문서는 아래 순서로 반영한다.

1. `doc/spec/bindings/README*.md`에 bindings / framework 책임 분할 원칙을 기록한다.
2. 언어별 `codec.md` 문서에서 codec extension 역할을 encode/decode helper로 고정한다.
3. 언어별 bindings README에서 `Message`를 low-level payload container로 설명한다.
4. framework 문서에서는 객체 메시징 표면을 설명하되, 그것이 bindings 기본 계약이
   아니라는 점을 분명히 적는다.

## 문서 수정 계획

현재 저장소에는 예전 codec policy와 새 책임 분할 초안이 함께 남아 있으므로, 아래 문서를
같은 턴에 정리하는 것을 기본 계획으로 둔다.

1. `doc/spec/bindings/README.md`, `doc/spec/bindings/README.ko.md`
   - codec extension policy를 새 경계에 맞게 고친다.
   - serializer 선택 규칙을 codec extension 정책이 아니라 framework 문서가 다루도록
     문장을 바꾼다.
2. `doc/spec/bindings/{node,java,dotnet,cpp,go,python,rust}/codec.md`
   - codec extension은 object <-> `Message` encode/decode helper만 정의한다고
     명시한다.
   - packet name, high-level serializer lookup, typed request/reply policy는 이 문서의
     범위가 아니라고 적는다.
3. framework 공통 초안과 언어별 framework spec
   - 객체 기반 outbound API, packet name 자동 추론, typed reply decode를 framework
     책임으로 반영한다.
4. guide와 sample 문서
   - bindings low-level guide는 raw `Message` 표면을 유지한다.
   - framework guide와 sample 문서는 업무 객체 기반 표면만 기본 예시로 남긴다.

## 검증 기준

구현이 끝나면 아래를 확인해야 한다.

1. 모든 bindings의 base `Message` public surface가 bytes 중심 계약으로 정리된다.
2. packet name 정책은 framework에만 존재한다.
3. codec extension은 object <-> `Message` 변환만 담당한다.
4. 한 언어 binding에만 있는 object-aware `Message` shortcut이 남지 않는다.
5. framework sample은 객체 기반 표면을 쓰고, bindings sample은 raw `Message` 표면을 쓴다.
6. 언어별 contract test가 같은 책임 분할을 검증한다.

## 구현 순서 제안

1. bindings / framework 책임 분할 원칙을 확정한다.
2. Node, Java에서 bindings로 올라온 high-level 기능을 분리한다.
3. 필요한 로직을 framework serializer / packet name layer로 옮긴다.
4. codec extension이 packet name 정책을 가지지 않도록 정리한다.
5. bindings spec, codec spec, framework guide를 같이 맞춘다.
6. 언어별 contract test와 framework conformance test를 추가한다.

## 한 문장 요약

bindings는 low-level `Message`와 codec 변환까지만 담당하고, 객체 메시징 정책은
framework가 맡도록 모든 언어를 같은 경계로 정리한다.
