# .NET exact public interface

[.NET 계약 목차](../README.ko.md)

이 디렉토리는 ZLink Framework 11.0.0 server의 exact C# public interface를 기능별로 소유한다. 공통
Framework spec이 동작을 정하고 다음 문서가 type, member, generic constraint, nullable annotation과 기본값을
고정한다.

| 문서 | 소유하는 계약 |
|---|---|
| [Common runtime](01-common-runtime.ko.md) | metadata, call, async result와 공통 option |
| [Configuration과 host](02-configuration-host.ko.md) | package, ASP.NET Core host, DI와 startup |
| [Topology configuration](03-configuration-topology.ko.md) | RouteMesh·ClientServer·fanout builder와 runtime option |
| [Channel messaging](04-channel-messaging.ko.md) | Node direct, ChannelName과 Logical Multicast call·handler |
| [Spots](05-spots.ko.md) | Entry·User·Instance Spot lifecycle, client, manager와 timer |
| [Actors](06-actors.ko.md) | Actor factory, context, client, manager와 state transfer policy |
| [Bound STREAM session](07-bound-stream-session.ko.md) | Actor가 소유한 bound session call |
| [STREAM session](07-stream-session.ko.md) | STREAM server session과 handler |
| [Location과 maintenance](08-location-maintenance.ko.md) | descriptor, lease, owner authority와 provider interface |
| [Authority와 transfer](08-authority-transfer.ko.md) | opaque authority CAS와 24시간 transfer retention |
| [Location provider와 Redis](08-location-provider-redis.ko.md) | change stamp와 공식 Redis provider |
| [Routing ID identity](09-routing-id-allocation.ko.md) | automatic RID 생성, prefix와 descriptor owner claim |
| [Host와 topology monitoring](10-topology-monitoring.ko.md) | state, termination, topology snapshot과 metric |
| [Monitoring과 오류](10-monitoring-errors.ko.md) | monitoring source와 Framework 오류 |
| [Serialization](11-serialization.ko.md) | typed JSON 기본 경로 |
| [Dispatch ownership](12-dispatch-ownership.ko.md) | dispatch와 message ownership |
| [Configuration 예제](13-examples.ko.md) | public builder 사용 예제 |

Application-facing API는 bindings의 service object, native handle, authority version, transfer phase와 transfer
reference를 노출하지 않는다. Framework 구현은 bindings package가 제공하는 public raw socket API만 사용하며
private member, reflection, native symbol 직접 호출과 Core service C API에 의존하지 않는다.

Public generation, revision, epoch와 sequence ordinal의 유효 범위는
`1..9223372036854775807`이다. .NET type이 `ulong`이어도 이 범위를 넓히지 않는다. 최대값에 도달하면
Framework는 wrap이나 값 재사용 없이 terminal exhaustion으로 처리한다. `0`은 값이 확정되지 않은 상태를
표현하도록 해당 계약이 명시한 경우에만 사용한다.
