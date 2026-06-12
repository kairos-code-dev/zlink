# Spot Actor Disconnected Callback 전환 계획

이 문서는 **구현 전 초안**이며 **현재 공개 계약이 아니다**.
정식 공개 계약은 구현과 테스트가 끝난 뒤 공통 framework spec, 언어별 spec, guide,
sample 문서에 나누어 반영한다.

이 문서는 C++, Java/Kotlin, .NET, Node framework에서 spot actor disconnected 처리를
handler 등록 방식에서 spot lifecycle callback 방식으로 옮기는 계획을 정리한다.

## 1. 배경

현재 framework의 spot actor lifecycle은 두 종류의 표면이 섞여 있다.

| 이벤트 | 현재 주된 표면 | 성격 |
|--------|----------------|------|
| actor join admission | spot callback | actor가 spot에 들어갈 수 있는지 결정한다. |
| actor joined | spot 또는 entry spot callback | actor가 spot membership에 추가된 뒤 실행된다. |
| actor left | spot 또는 entry spot callback | actor가 spot membership에서 제거된 뒤 실행된다. |
| actor disconnected | handler 등록 | actor의 현재 bound session 연결이 끊겼을 때 실행된다. |

`actor disconnected`는 packet dispatch가 아니다. packet 이름, payload type, handler group,
codec 선택으로 결정되는 동작이 아니라 actor binding/session lifecycle 변화다. 따라서
`onActorLeft`와 같은 계열의 callback으로 노출하는 것이 더 일관된다.

## 2. 목표

1. actor disconnected를 모든 언어에서 spot lifecycle callback으로 제공한다.
2. disconnected handler 등록 API와 annotation, attribute, method registration 표면을 삭제한다.
3. packet handler 등록은 유지한다. `JoinGameReq`, `PlaceMarkReq`, timer, channel request/send
   같은 dispatch 대상은 계속 handler registry가 처리한다.
4. `onActorDisconnected`는 actor가 spot을 떠난다는 뜻이 아님을 문서화한다.
5. sample에서 disconnected handler 방식 코드가 새 표면에 남지 않도록 테스트로 막는다.
6. 언어별 스타일은 유지하되 의미는 같게 맞춘다.

## 3. 비목표

- actor packet handler 등록을 제거하지 않는다.
- stream session의 `onDisconnected` callback을 제거하지 않는다.
- disconnect가 자동 leave를 의미하도록 바꾸지 않는다.
- compatibility shim을 남기지 않는다. 기존 disconnected handler 방식은 삭제한다.
- core C API 계약을 이 계획에서 직접 변경하지 않는다.

## 4. 용어와 의미

| 용어 | 의미 |
|------|------|
| actor disconnected | actor의 현재 bound session 또는 session binding이 끊어진 상태다. actor가 spot에서 제거됐다는 뜻은 아니다. |
| actor left | actor가 spot membership에서 제거된 상태다. |
| lifecycle callback | spot 또는 entry spot 객체가 직접 구현하는 메서드다. framework가 lifecycle 변화 시 호출한다. |
| packet handler | packet 이름과 message type을 기준으로 runtime이 찾아 호출하는 application handler다. |

`onActorDisconnected` 기본 동작은 no-op이다. application이 disconnect 시 spot membership도
정리해야 하면 callback 안에서 `leaveActor` 또는 해당 언어의 같은 의미 API를 호출한다.
framework가 disconnect를 자동 leave로 해석하면 재접속, stale disconnect, multi-session
binding 정책이 흐려지므로 기본 정책으로 두지 않는다.

## 5. 제안 표면

### 5.1 공통 의미

Spot callback:

```text
onActorDisconnected(actor, cancellation)
```

Entry spot callback:

```text
onActorDisconnected(actor, cancellation)
```

호출 조건:

1. session actor disconnect 알림이 runtime에 들어온다.
2. runtime이 actor binding을 해제할 때, 끊긴 binding이 actor의 현재 binding과 일치한다.
3. stale disconnect가 새 binding을 지우지 못한 경우에는 callback을 호출하지 않는다.

stream socket close 자체가 곧바로 spot actor disconnected callback을 뜻하지는 않는다. 언어별
stream session runtime이 bound actor에 대해 disconnected 알림을 보내는 경로를 제공할 때만
spot actor disconnected lifecycle로 이어진다. application이 직접 actor disconnected 알림을
보내는 API를 가진 언어에서는 그 API도 같은 current binding 검사를 거쳐야 한다.

호출 순서:

1. framework가 actor binding을 current-token 조건으로 해제한다.
2. actor가 user spot에 join된 상태이면 그 spot의 `onActorDisconnected`를 호출한다.
3. actor가 user spot에 join되지 않았고 entry spot lifecycle surface에만 있으면 entry spot의
   `onActorDisconnected`를 호출한다.
4. 같은 disconnect 알림으로 user spot callback과 entry spot callback을 모두 호출하지 않는다.
5. callback 실패는 lifecycle failure로 기록하고, packet reply로 변환하지 않는다.

`onActorLeft`와의 관계:

- disconnect만 발생하면 `onActorLeft`를 호출하지 않는다.
- leave만 발생하면 `onActorDisconnected`를 호출하지 않는다.
- disconnect callback 안에서 application이 leave를 요청하면 그 leave 흐름에서
  `onActorLeft`가 별도로 호출된다.

### 5.2 .NET

추가할 callback:

```csharp
ValueTask OnActorDisconnectedAsync(
    TActor actor,
    CancellationToken cancellationToken)
{
    return ValueTask.CompletedTask;
}
```

대상 interface:

- `IZLinkSpot<TActor>`
- `IZLinkEntrySpot<TActor>`

삭제할 표면:

- `IZLinkActorHandlerRegistry.AddActorDisconnected<THandler, TActor>()`
- `ZLinkSpotActorDisconnectedAttribute`
- `IZLinkSpotActorDisconnectedHandler<...>`
- `IZLinkEntrySpotActorDisconnectedHandler<...>`
- disconnected handler descriptor, scanner, invoker, activation registration 코드
- sample과 test fixture의 disconnected handler 등록 코드

### 5.3 Java/Kotlin

Java callback:

```java
default void onActorDisconnected(
    ZLinkActor actor,
    CancellationToken cancellationToken) {
}
```

Kotlin coroutine wrapper:

```kotlin
open suspend fun onActorDisconnectedSuspending(
    actor: ZLinkActor,
    cancellationToken: CancellationToken
) {
}
```

대상 interface:

- `ZLinkSpot`
- `ZLinkEntrySpot`
- Kotlin spot wrapper

삭제할 표면:

- `@ZLinkSpotActorDisconnected`
- `ZLinkSpotActorDisconnectedHandler`
- `ZLinkEntrySpotActorDisconnectedHandler`
- `ZLinkSpotHandlerRegistry.addActorDisconnected(...)`
- scanner와 runtime의 disconnected handler catalog
- Kotlin suspend annotation handler 지원 코드

### 5.4 C++

추가할 callback:

```cpp
void on_actor_disconnected(const actor_t &actor);
```

C++는 기존 `on_post_actor_joined`와 `on_actor_left` callback 스타일에 맞춘다.
첫 구현은 synchronous member callback을 기준으로 하고, framework가 이미 lifecycle
callback에서 `task_t<void>` 반환을 지원한다면 같은 규칙을 재사용한다. concrete actor type
overload는 기존 actor lifecycle callback과 같은 방식으로 허용한다.

삭제할 표면:

- `add_actor_disconnected<&T::method>()`
- disconnected actor handler descriptor와 registry entry
- handler layout contract에서 disconnected registration 항목
- sample과 unit test fixture의 disconnected method registration

### 5.5 Node

추가할 callback:

```ts
onActorDisconnected?(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
```

대상 interface:

- `ZLinkSpot`
- `ZLinkEntrySpot`

삭제할 표면:

- `ZLinkActorHandlerRegistry.addActorDisconnected(...)`
- `ZLinkSpotActorDisconnected()` decorator
- disconnected handler descriptor, dispatcher, NestJS scanner 지원
- disconnected handler contract test fixture

## 6. 구현 단계

### Phase 0: 기준선 확인

작업:

1. 네 언어에서 disconnected handler public surface를 검색한다.
2. runtime에서 disconnected 이벤트가 어디서 발생하는지 추적한다.
3. `actor disconnected`와 `actor left`가 섞인 테스트가 있는지 분리한다.
4. 샘플에서 disconnected handler 방식을 사용하는 코드를 목록화한다.

필수 검색:

```bash
rg -n "ActorDisconnected|actor disconnected|addActorDisconnected|AddActorDisconnected|add_actor_disconnected|ZLinkSpotActorDisconnected" framework/languages
rg -n "onActorLeft|OnActorLeft|on_actor_left|onActorDisconnected|OnActorDisconnected|on_actor_disconnected" framework/languages
```

완료 조건:

- 삭제 대상 API와 runtime 코드 목록이 언어별로 정리되어 있다.
- callback으로 유지할 lifecycle 경로와 삭제할 handler 경로가 분리되어 있다.

### Phase 1: 공통 의미 고정

작업:

1. stale disconnect에서는 callback을 호출하지 않는 규칙을 각 runtime test에 먼저 추가한다.
2. disconnect가 leave를 자동 호출하지 않는 규칙을 test로 고정한다.
3. callback 실패가 packet reply로 바뀌지 않는 규칙을 test로 고정한다.

완료 조건:

- 기존 handler 방식으로는 새 테스트가 실패한다.
- callback 구현으로 전환해야 통과하는 테스트가 준비되어 있다.

### Phase 2: .NET 전환

작업:

1. `IZLinkSpot<TActor>`와 `IZLinkEntrySpot<TActor>`에 `OnActorDisconnectedAsync`를 추가한다.
2. runtime disconnected notifier가 handler registry 대신 spot callback을 호출하게 바꾼다.
3. `AddActorDisconnected`, `ZLinkSpotActorDisconnectedAttribute`, disconnected handler interface를 삭제한다.
4. descriptor, scanner, invoker, activation registry의 disconnected handler 경로를 삭제한다.
5. E2E fixture와 contract test를 callback 방식으로 바꾼다.
6. handler 방식 API가 남아 있으면 실패하는 검색 기반 test를 추가한다.

필수 테스트:

```bash
dotnet test framework/languages/dotnet/Zlink.Framework.sln
```

필수 검색:

```bash
rg -n "AddActorDisconnected|ZLinkSpotActorDisconnectedAttribute|IZLinkSpotActorDisconnectedHandler|IZLinkEntrySpotActorDisconnectedHandler" framework/languages/dotnet/src framework/languages/dotnet/samples framework/languages/dotnet/tests
```

완료 조건:

- 검색 결과가 문서의 삭제 설명을 제외하고 없다.
- spot callback test와 entry spot callback test가 모두 통과한다.

### Phase 3: Java/Kotlin 전환

작업:

1. `ZLinkSpot`, `ZLinkEntrySpot`에 `onActorDisconnected` 기본 메서드를 추가한다.
2. Kotlin coroutine spot wrapper에 suspending callback을 추가한다.
3. runtime disconnected notifier가 handler catalog 대신 callback을 호출하게 바꾼다.
4. `@ZLinkSpotActorDisconnected`, disconnected handler interface, registry API를 삭제한다.
5. Java와 Kotlin annotation scanner에서 disconnected handler shape 검사를 삭제한다.
6. Java/Kotlin sample과 test fixture를 callback 방식으로 바꾼다.
7. 삭제된 API가 남아 있으면 실패하는 release gate 또는 contract test를 추가한다.

필수 테스트:

```bash
cd framework/languages/java
./gradlew test integrationTest contractTest fakeBackendTest
```

필수 검색:

```bash
rg -n "ZLinkSpotActorDisconnected|addActorDisconnected|ActorDisconnectedHandler" framework/languages/java/zlink-framework-core framework/languages/java/zlink-framework-kotlin framework/languages/java/samples
```

완료 조건:

- Java와 Kotlin 모두 callback으로 disconnected 흐름을 검증한다.
- annotation과 handler interface가 public source에 남지 않는다.

### Phase 4: C++ 전환

작업:

1. spot와 entry spot base contract에 `on_actor_disconnected` callback을 추가한다.
2. actor gateway 또는 actor runtime의 disconnected notifier가 callback을 호출하게 바꾼다.
3. `add_actor_disconnected` registration API와 descriptor를 삭제한다.
4. layout contract, header contract, unit test fixture를 callback 방식으로 바꾼다.
5. sample에 disconnected cleanup이 있으면 callback으로 옮긴다.
6. 삭제된 API가 남아 있으면 실패하는 contract header test를 추가한다.

필수 테스트:

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

필수 검색:

```bash
rg -n "add_actor_disconnected|actor_disconnected_handler" framework/languages/cpp
```

완료 조건:

- `add_actor_disconnected`는 삭제 설명 문서 외에는 남지 않는다.
- `on_actor_disconnected` callback test가 통과한다.

### Phase 5: Node 전환

작업:

1. `ZLinkSpot`, `ZLinkEntrySpot`에 `onActorDisconnected` callback을 추가한다.
2. runtime actor dispatcher가 disconnected handler registry 대신 callback을 호출하게 바꾼다.
3. `addActorDisconnected`, `ZLinkSpotActorDisconnected()` decorator와 관련 descriptor, scanner,
   NestJS test fixture를 삭제한다.
4. contract test에 callback invocation, stale disconnect no-op, no auto leave 규칙을 추가한다.
5. TicTacToe sample 정리와 충돌하지 않도록 spot packet handler와 lifecycle callback을 분리한다.

필수 테스트:

```bash
npm run typecheck
npm run test
npm run build
npm run verify:samples
```

필수 검색:

```bash
rg -n "addActorDisconnected|ZLinkSpotActorDisconnected|ActorDisconnectedHandler" framework/languages/node/packages framework/languages/node/samples framework/languages/node/test
```

완료 조건:

- `addActorDisconnected`는 public source와 sample에 남지 않는다.
- callback 기반 disconnected test가 통과한다.

### Phase 6: 샘플 정리

작업:

1. TicTacToe, Bingo, SupportChat 등 spot actor lifecycle을 사용하는 sample을 점검한다.
2. disconnected handler class가 있으면 spot 또는 entry spot callback으로 옮긴다.
3. packet handler는 spot/entry spot 내부 등록으로 유지한다.
4. main/server bootstrap에 spot actor packet handler가 직접 provider 또는 handler로 나열되지 않게 한다.
5. sample regression test에 삭제 API 검색을 추가한다.

완료 조건:

- sample에 disconnected handler 방식 코드가 없다.
- packet handler와 lifecycle callback의 책임이 파일 구조에서 구분된다.
- 각 언어의 sample runner가 통과한다.

### Phase 7: 문서 반영

작업:

1. 공통 framework spec에 actor disconnected callback 의미를 반영한다.
2. 언어별 spot spec과 handler interface spec에서 disconnected handler 등록 설명을 삭제한다.
3. guide에는 사용자가 언제 `onActorDisconnected`를 구현해야 하는지 예시를 넣는다.
4. internals에는 stale disconnect, binding token, callback 호출 순서를 설명한다.
5. sample 문서에는 packet handler와 lifecycle callback의 차이를 명확히 적는다.
6. 삭제된 handler 방식은 migration note에만 짧게 언급하고, guide의 기본 사용법으로 남기지 않는다.

수정 대상:

- `framework/doc/spec/`
- `framework/languages/dotnet/doc/spec/`
- `framework/languages/java/doc/spec/`
- `framework/languages/cpp/doc/spec/`
- `framework/languages/node/doc/spec/`
- 각 언어 `doc/guide/*spot*.ko.md`
- 각 언어 sample README와 sample regression 문서

완료 조건:

- 정식 spec에는 구현된 public surface만 남는다.
- draft와 정식 spec이 서로 다른 계약을 말하지 않는다.
- guide에 내부 runtime 구현 설명이 섞이지 않는다.

## 7. 테스트 계획

공통 테스트 항목:

| 항목 | 기대 결과 |
|------|-----------|
| current binding disconnect | spot callback이 한 번 호출된다. |
| stale binding disconnect | callback이 호출되지 않는다. |
| disconnect 후 actor membership | actor는 자동으로 spot에서 제거되지 않는다. |
| callback에서 leave 호출 | leave 흐름에서 `onActorLeft`가 별도로 호출된다. |
| entry spot actor disconnect | entry spot callback이 호출된다. |
| user spot actor disconnect | user spot callback이 호출된다. |
| callback exception | lifecycle failure로 관측되고 packet reply로 변환되지 않는다. |
| old handler API search | 삭제 대상 API가 source, sample, test fixture에 남으면 실패한다. |

언어별 최소 테스트 위치:

| 언어 | 테스트 위치 |
|------|-------------|
| .NET | `framework/languages/dotnet/tests/Zlink.Framework.*Tests` |
| Java/Kotlin | `framework/languages/java/zlink-framework-core/src/test`, `integrationTest`, `fakeBackendTest`, Kotlin test |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.*` |
| Node | `framework/languages/node/test/contract` |

## 8. 삭제 기준

아래 이름은 구현 완료 뒤 code, sample, test fixture에 남으면 안 된다.
문서에서 migration 이력으로 언급할 때만 허용한다.

```text
AddActorDisconnected
addActorDisconnected
add_actor_disconnected
ZLinkSpotActorDisconnected
ZLinkSpotActorDisconnectedAttribute
IZLinkSpotActorDisconnectedHandler
IZLinkEntrySpotActorDisconnectedHandler
ZLinkSpotActorDisconnectedHandler
ZLinkEntrySpotActorDisconnectedHandler
ActorDisconnectedHandler
```

삭제 기준은 compatibility shim에도 적용한다. 새 callback으로 감싼 deprecated handler 등록
API를 남기면 같은 lifecycle 의미를 두 public surface가 동시에 표현하게 되므로 허용하지 않는다.

## 9. 완료 기준

전체 작업은 아래 조건이 모두 만족될 때 완료한다.

1. 네 언어 public source에서 disconnected handler 등록 표면이 삭제됐다.
2. 네 언어 spot/entry spot public contract에 disconnected callback이 있다.
3. stale disconnect와 no auto leave 규칙이 네 언어 테스트에 있다.
4. sample에 disconnected handler 방식 코드가 남지 않는다.
5. 언어별 build/test/sample runner가 통과한다.
6. 공통 spec, 언어별 spec, guide, sample 문서가 같은 의미를 설명한다.
7. `rg` 검색으로 삭제 대상 이름이 source와 sample에 남지 않음을 확인했다.
