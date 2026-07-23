# C++ exact public interface

[C++ 계약 목차](../README.ko.md)

이 디렉토리는 ZLink Framework 11.0.0 server의 exact C++ public interface를 기능별로 소유한다. 공통
Framework spec이 동작을 정하고 다음 문서가 namespace, type, member, template constraint와 기본값을
고정한다.

| 문서 | 소유하는 계약과 installed public header |
|---|---|
| [Common runtime](01-common-runtime.ko.md) | `dispatch`, `errors`, `messaging`, `codecs`, `workers` |
| [Configuration과 host](02-configuration-host.ko.md) | `configuration`, `http`, host·DI·module·lifecycle |
| [Channel messaging](03-channel-messaging.ko.md) | `channels`, `handlers`, topology builder, object role·capacity·weight와 automatic RID |
| [Spots](04-spots.ko.md) | global SpotRid·SpotRef, Spot relocation adapter, Entry Spot relocation callback, Instance fluent cold activation과 User Spot manager |
| [Actors](05-actors.ko.md) | global ActorId·ActorRef, Actor relocation adapter, ID-only messaging, manager create와 exact mutation·bind |
| [STREAM session](06-stream-session.ko.md) | `streams`, packet session과 bound session 연동 |
| [Location Store·Redis](07-location-store.ko.md) | Location record, descriptor·capacity와 Redis provider |
| [Maintenance provider](07-location-maintenance.ko.md) | authority, generic creation Reserve·Commit·Abort와 Relocation capability |
| [Monitoring](08-monitoring.ko.md) | `monitoring`, `eventing`, snapshot·event·health |

`zlink/framework.hpp`는 위 installed header를 모으는 facade다. Application-facing API에는 Core service
handle, claim, receive batch, reply token, service liveness command와 authority/relocation 내부 transaction을
노출하지 않는다. Framework runtime은 설치된 C++ binding의 public raw socket API만 사용한다.

Public generation, revision, epoch와 sequence ordinal의 유효 범위는
`1..9223372036854775807`이다. C++ type이 `std::uint64_t`여도 이 범위를 넓히지 않는다. 최대값에 도달하면
Framework는 wrap이나 값 재사용 없이 terminal exhaustion으로 처리한다. `0`은 값이 확정되지 않은 상태를
표현하도록 해당 계약이 명시한 경우에만 사용한다.

## 공개 표면

이 문서 집합에 선언한 Channel, Spot, Actor, STREAM, handler, builder, host, DI, maintenance와 state relocation
member가 C++ 11.0 public contract다. Core service handle, dispatch record와 service liveness interval·deadline은
이 계약에 포함하지 않는다.
