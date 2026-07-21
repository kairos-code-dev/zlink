# RouteMesh 11 service runtime 구현 crosswalk

이 디렉터리의 `01`부터 `08`까지는 Core 10 구현·test의 책임을 C++·.NET·JVM·Node.js runtime 작업과 연결하는
대조 자료다. 내부 불변 조건의 단일 기준은
[Framework 공통 internals](../../../framework/common/internals/README.ko.md)와
[service wire schema](../../../../runtime/protocol/service-wire-v1.schema.json)다. 이 묶음은 별도의 내부 계약이나
application 공개 계약을 정의하지 않는다.

정식 internals 또는 schema와 이 묶음의 내용이 다르면 정식 문서를 적용하고 같은 candidate에서 이 crosswalk를
수정한다. `scripts/verify-framework-doc-contracts.sh`는 정식 소유 문서 링크와 이 소유권 규칙을 검사한다. Core
raw 내부 구조의 정본은 `core/doc/internals/runtime-boundary.ko.md`이며 `09`는 그 위치만 가리킨다. 공개 동작과
exact interface는 항상 [Framework 정식 spec](../../../framework/spec/README.ko.md)이 소유한다.

## 1. 문서 집합

| 구현 대조 문서 | 설명 | 정식 내부 구조 소유 문서 |
|---|---|---|
| [01 Runtime architecture](01-runtime-architecture.ko.md) | 언어별 runtime 계층, aggregate와 책임 경계 | [Service runtime architecture](../../../framework/common/internals/service-runtime-architecture.ko.md) |
| [02 Wire protocol](02-wire-protocol.ko.md) | multipart schema, version, command와 protocol 오류 | [Service wire protocol](../../../framework/common/internals/service-wire-protocol.ko.md) |
| [03 Mailbox and dispatch](03-mailbox-dispatch.ko.md) | admission, ready, claim, request와 completion | [Service runtime architecture](../../../framework/common/internals/service-runtime-architecture.ko.md), [Service wire protocol](../../../framework/common/internals/service-wire-protocol.ko.md) |
| [04 Stateful object runtime](04-stateful-object-runtime.ko.md) | Spot·Actor·Instance activation과 fencing | [Stateful maintenance](../../../framework/common/internals/stateful-maintenance-runtime.ko.md) |
| [05 Maintenance and recovery](05-maintenance-recovery.ko.md) | Retire·Shutdown, authority CAS, checkpoint와 recovery | [Stateful maintenance](../../../framework/common/internals/stateful-maintenance-runtime.ko.md) |
| [06 STREAM session runtime](06-stream-session-runtime.ko.md) | raw STREAM, session binding과 transfer barrier | [Service runtime architecture](../../../framework/common/internals/service-runtime-architecture.ko.md), [Stateful maintenance](../../../framework/common/internals/stateful-maintenance-runtime.ko.md) |
| [07 Liveness and monitoring](07-liveness-monitoring.ko.md) | service probe, fanout beacon, reconnect, readiness와 관측 | [Transport liveness](../../../framework/common/internals/transport-liveness-runtime.ko.md) |
| [08 Concurrency and resources](08-concurrency-resources.ko.md) | executor, lifetime, lock과 bounded resource | [Service runtime architecture](../../../framework/common/internals/service-runtime-architecture.ko.md) |
| [09 Core raw runtime 정본](09-core-raw-runtime-boundary.ko.md) | Core raw internals 정본의 위치 | [Core raw internals](../../../../../core/doc/internals/runtime-boundary.ko.md) |

## 2. 구현 경계

C++·.NET·JVM·Node.js는 protocol schema와 golden frame, normalized behavior fixture만 공유한다. Java와 Kotlin은
JVM runtime 하나를 공유한다. 네 runtime은 source code, native binary, private header와 callback SPI를 공유하지
않는다.

각 Framework runtime은 설치된 언어 binding의 public raw API만 사용한다. Core service C ABI를 다시 만들거나,
Framework 전용 private C API를 추가하거나, binding 내부 symbol에 접근하지 않는다. 필요한 raw transport
primitive가 없다면 일반 raw socket 사용자에게도 유효한 공개 Core·binding 계약인지 먼저 확인한다.

## 3. Core service internals no-loss mapping

기존 Core service internals의 §1~§10은 다음 문서가 빠짐없이 이어받는다. C 구조체와 함수 모양은 이관하지 않고,
그 구조가 보장하던 ordering, ownership과 failure 의미만 언어별 runtime 내부 계약으로 옮긴다.

| Core 원본 절 | 보존할 책임 | 목적 문서 |
|---|---|---|
| §1 Source layout and boundaries | object state, wire codec·admission·ingress, dispatch, actor, transfer, monitor, STREAM, raw socket 분리 | [01](01-runtime-architecture.ko.md) §2·§4, [02](02-wire-protocol.ko.md), [Core raw internals](../../../../../core/doc/internals/runtime-boundary.ko.md) |
| §2 Object model | host/node aggregate, owner identity, generation, handle lifetime, child reference와 close 순서 | [01](01-runtime-architecture.ko.md) §3·§5, [04](04-stateful-object-runtime.ko.md) §2, [08](08-concurrency-resources.ko.md) §2·§4 |
| §3 Mailbox, ready and claim | application·infrastructure queue, admission gate, level-ready index, batch claim, wakeup mode | [03](03-mailbox-dispatch.ko.md) §2~§5, [08](08-concurrency-resources.ko.md) §3 |
| §4 Request, reply and completion | operation commit, one-shot reply, timeout race, terminal-once completion, lifecycle generation, STREAM close ordering | [03](03-mailbox-dispatch.ko.md) §6~§9, [06](06-stream-session-runtime.ko.md) §5, [08](08-concurrency-resources.ko.md) §5 |
| §5 Wire | ROUTER ingress, peer connection identity, duplicate RID handover, envelope, descriptor admission, remote request·reply | [02](02-wire-protocol.ko.md) §2~§8, [07](07-liveness-monitoring.ko.md) §2~§4 |
| §6 Logical multicast | local subscriber snapshot, remote node별 한 번 전송, partial result와 rollback 금지 | [03](03-mailbox-dispatch.ko.md) §10 |
| §7 Actor and transfer fence | membership commit, transfer participant, admission·claim·ready fence, ACK, reply relay와 source cleanup | [04](04-stateful-object-runtime.ko.md) §4~§7, [05](05-maintenance-recovery.ko.md) §5~§8 |
| §8 Monitor | bounded event queue, lifecycle event 보존, aggregation, observer isolation, raw monitor와 service monitor 분리 | [07](07-liveness-monitoring.ko.md) §5~§8, [08](08-concurrency-resources.ko.md) §6 |
| §9 Locks and threads | application executor, ingress loop, deadline scheduler, object timer, lock order와 send serialization | [08](08-concurrency-resources.ko.md) §2~§7 |
| §10 STREAM | raw STREAM session, 1:1:1 ownership, Actor binding CAS, generation과 movement barrier | [06](06-stream-session-runtime.ko.md) §2~§8 |

## 4. 구현 delta 분리

다음 항목은 Core service 구현을 그대로 옮기는 항목이 아니라 v11 목표 runtime에서 새로 채우는 delta다. 개별
internals는 최종 상태만 설명하므로 이 구분을 반복하지 않는다. 구현 상태와 증거는 execution ledger가 소유한다.

| delta | Core 포팅 입력 | v11 목표 |
|---|---|---|
| protocol 생성 | source에 분산된 command·layout 상수 | schema 한 곳에서 네 언어 상수와 codec table 생성 |
| capability | base descriptor admission | `framework-service-v11` required capability와 wire major 검증 |
| descriptor extension | service base field | `0x08` extension과 required runtime state·application version·capability |
| endpoint와 field bound | Core decoder가 가진 개별 검사 | endpoint 4,096 byte와 모든 atom의 strict allocation-before-validation |
| malformed input | Core wire path별 오류 처리 | command 비허용 flag, duplicate TLV, trailing byte와 invalid UTF-8의 공통 protocol error |
| Instance authority | 미완성 Core driver token과 callback | 각 언어 runtime이 Location Store에서 owner fence를 확인하고 activation barrier 적용 |
| reply operation | 성공한 raw admission에서 Core token 소비 | public reply operation은 첫 terminator가 한 번 소비하고 runtime이 bounded admission과 source terminal failure를 끝까지 소유 |
| authority provider | Actor·Instance phase별 Store method | Location Store의 opaque expected-version CAS 하나로 owner와 phase를 함께 갱신 |
| forced cleanup | Core claim lifetime storage | terminal host와 분리한 tombstone이 반환하지 않는 callback reference만 지연 회수 |
| runtime 배치 | Core 공통 service implementation | C++·.NET·JVM·Node.js별 source와 public raw binding 연결 |

## 5. Crosswalk 동기화 gate

`01`~`09`는 구현 기간에 대조 자료로 유지하며 다른 디렉터리로 이동하지 않는다. 다음 조건을 같은 candidate에서
확인한다.

- 각 owner, queue, fence, error와 종료 순서는 §1의 정식 internals 또는 schema에 연결한다.
- 네 runtime 구현에서 확인한 내부 불변 조건은 정식 internals에 먼저 반영하고 관련 crosswalk를 함께 수정한다.
- Target 문서와 정식 internals 또는 schema가 충돌하면 정식 문서를 적용한다.
- Core raw 정본과 raw timer·monitor 회귀는 execution ledger의 해당 gate가 검증한다.
- `scripts/verify-framework-doc-contracts.sh`가 link, 필수 소유권 문장과 문서 집합을 검증한다.
