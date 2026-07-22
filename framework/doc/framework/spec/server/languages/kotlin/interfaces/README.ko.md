# Kotlin public interface 정식 계약

[Kotlin 계약 목차](../README.ko.md) · [Java interface](../../java/interfaces/README.ko.md)

Kotlin package는 Java와 JVM service runtime을 공유한다. 아래 문서는 Java type을 그대로 쓰는 범위와 Kotlin
전용 coroutine·DSL signature를 기능별로 고정한다.

- [공통 runtime](common-runtime.ko.md)
- [구성과 host](configuration-host.ko.md)
- [Channel messaging](channel-messaging.ko.md)
- [Spot](spots.ko.md)
- [Actor](actors.ko.md)
- [STREAM session](stream-session.ko.md)
- [Location과 maintenance](location-maintenance.ko.md)
- [Monitoring](monitoring.ko.md)

## 공개 API 구조

Kotlin application은 Java의 lifecycle, termination, transfer policy와 Location type을 직접 사용한다. Kotlin
package는 coroutine handler, suspending call, reified registration과 구성 DSL을 제공하며 같은 의미의 runtime
facade나 상태 type을 중복해서 정의하지 않는다.

Channel extension은 process-local ChannelName만 받으며 MeshName과 ChannelName을 함께 받는 선택 overload를
추가하지 않는다. Host `Retire`·`Shutdown`과 deprecated host drain은 Java 결과 type을
그대로 사용한다. Location provider의 authority CAS와 Transfer Store도 Java public interface가 정본이다.

각 기능 문서는 Kotlin source signature와 application이 실제로 link하는 generated JVM signature를 구분한다.
Default argument, suspend continuation, extension receiver와 generic bound는 두 표현 사이에서 손실 없이 대응해야
한다. Node를 직접 지정하는 extension의 첫 번째 `String` 인자는 Java 계약과 같이 MeshName이다.

Public generation, revision, epoch와 sequence ordinal은 Java 계약의 양수 `Long` 범위를 그대로 사용한다.
유효 범위는 `1..Long.MAX_VALUE`이며 최대값에서는 wrap이나 값 재사용 없이 terminal exhaustion으로 처리한다.
`0`은 값이 확정되지 않은 상태를 표현하도록 해당 계약이 명시한 경우에만 사용한다.

## RouteMesh 11 object runtime 기준

Kotlin exact interface는 Java와 같은 global ActorId·SpotRid, immutable `ActorRef`·`SpotRef`, ID-only 일반
messaging과 exact-ref mutation·session bind를 사용한다. Object operation은 single-use fluent
Create/GetOrCreate이며 Mesh object role은 None, Client, Server로 구분한다. 모든 server factory는 명시적인
transfer policy와 typed placement option을 받는다. Kotlin extension은 이 계약을 축약하거나 local fallback을
추가하지 않는다.

Global ref의 JSON field는 `actorId` 또는 `spotRid`, `objectGeneration`, `meshName`, `nodeRid`다.
`objectGeneration`은 decimal string이며 unknown field는 무시하지만 duplicate field, required field 누락,
0 또는 `Long.MAX_VALUE` 초과 값은 거부한다. String identity는 exact value를 유지하며 normalization하지 않는다.
허용하는 shape는 다음 두 가지다. `objectGeneration`은 leading zero가 없는
`"1"`..`"9223372036854775807"`이고 JSON number token은 거부한다.

```json
{"actorId":"actor-7","objectGeneration":"41","meshName":"game","nodeRid":"game-0123456789abcdef0123456789abcdef"}
```

```json
{"spotRid":"room-7","objectGeneration":"42","meshName":"game","nodeRid":"game-0123456789abcdef0123456789abcdef"}
```
