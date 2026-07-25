# .NET exact public interface

[.NET 계약 목차](../README.ko.md)

이 디렉토리는 ZLink Framework 11.0.0 server의 exact C# public interface를 기능별로 소유한다. 공통
Framework spec이 동작을 정하고 다음 문서가 type, member, generic constraint, nullable annotation과 기본값을
고정한다.

| 문서 | 소유하는 계약 |
|---|---|
| [Common runtime](01-common-runtime.ko.md) | Metadata, call, async result와 공통 option의 public type을 정의한다. |
| [Configuration과 host](02-configuration-host.ko.md) | Package, ASP.NET Core host, DI와 startup interface를 정의한다. |
| [Topology configuration](03-configuration-topology.ko.md) | RouteMesh, ClientServer와 fanout builder 및 runtime option을 정의한다. |
| [Channel messaging](04-channel-messaging.ko.md) | Node direct, ChannelName과 Logical Multicast의 call과 handler를 정의한다. |
| [Spots](05-spots.ko.md) | Entry·User·Instance Spot lifecycle, relocation adapter, [Spot](../../../../01-glossary.ko.md#spot) 전용 fluent call, [User Spot](../../../../01-glossary.ko.md#entry-spot-user-spot과-instance-spot) manager와 timer를 정의한다. |
| [Actors](06-actors.ko.md) | Actor factory, context, client, manager, relocation adapter와 policy를 정의한다. |
| [Bound STREAM session](07-bound-stream-session.ko.md) | Actor가 소유한 bound session call을 정의한다. |
| [STREAM session](07-stream-session.ko.md) | STREAM server session과 handler interface를 정의한다. |
| [Location과 maintenance](08-location-maintenance.ko.md) | Descriptor, lease, owner authority와 provider interface를 정의한다. |
| [Authority와 relocation](08-authority-relocation.ko.md) | Provider가 payload를 해석하지 않는 [authority](../../../../01-glossary.ko.md#authority) CAS와 24시간 relocation retention을 정의한다. |
| [Location provider와 Redis](08-location-provider-redis.ko.md) | Change stamp capability와 공식 Redis provider를 정의한다. |
| [Routing ID identity](09-routing-id-allocation.ko.md) | Automatic RID 생성, prefix와 MeshNode descriptor [owner](../../../../01-glossary.ko.md#owner) claim을 정의한다. |
| [Host와 topology monitoring](10-topology-monitoring.ko.md) | Host state, termination, topology snapshot과 metric을 정의한다. |
| [Monitoring과 오류](10-monitoring-errors.ko.md) | Monitoring source와 Framework 오류를 정의한다. |
| [Serialization](11-serialization.ko.md) | Typed payload가 사용하는 JSON 기본 경로를 정의한다. |
| [Dispatch ownership](12-dispatch-ownership.ko.md) | Dispatch가 message ownership을 넘기는 규칙을 정의한다. |
| [Configuration 예제](13-examples.ko.md) | Public builder를 조합하는 최소 예제를 제공한다. |

Application-facing API는 bindings의 service object, native handle, authority version, relocation phase와 relocation
reference를 노출하지 않는다. Framework 구현은 bindings package가 제공하는 public raw socket API만 사용하며
private member, reflection, native symbol 직접 호출과 Core service C API에 의존하지 않는다.

Public generation, revision, epoch와 sequence ordinal의 유효 범위는
`1..9223372036854775807`이다. .NET type이 `ulong`이어도 이 범위를 넓히지 않는다. 최대값에 도달하면
Framework는 wrap이나 값 재사용 없이 terminal exhaustion으로 처리한다. `0`은 값이 확정되지 않은 상태를
표현하도록 해당 계약이 명시한 경우에만 사용한다.
