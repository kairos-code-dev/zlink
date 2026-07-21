# Framework Service 11.0 공개 계약 migration crosswalk

이 디렉터리의 `01`부터 `08`까지는 Core 10 service 의미를 Framework 11.0 공개 계약의 소유 문서와 연결하는
대조 자료다. 공개 계약의 단일 기준은 [Framework 정식 spec](../../../framework/spec/README.ko.md)과
[다섯 언어 exact interface](../../../framework/spec/server/languages/README.ko.md)다. 이 묶음은 별도의 공개
계약을 정의하지 않는다.

정식 spec과 이 묶음의 내용이 다르면 정식 spec을 적용하고 같은 candidate에서 이 crosswalk를 수정한다.
`scripts/verify-framework-doc-contracts.sh`는 정식 소유 문서 링크와 이 소유권 규칙을 검사한다. 진행 상태,
언어별 구현 차이와 review 증거는 11.0 execution ledger와 implementation gap 문서가 소유한다. Core raw 계약의
정본은 `core/doc/spec/core/09-runtime-boundary.ko.md`이며 `09-core-raw-runtime-boundary.ko.md`는 그 위치만
가리킨다.

## 1. 대조 경계

Service 의미는 Framework가 소유한다. C++, .NET, JVM(Java·Kotlin), Node.js는 각 언어 package 안에
service runtime을 구현하고, 해당 언어 binding의 public raw socket API만 사용한다. Framework 전용 공통
native runtime, private Core C ABI와 binding 내부 진입점은 두지 않는다.

언어별 public signature는
`framework/doc/framework/spec/server/languages/<lang>/interfaces/`가 소유한다. 이 묶음은 application이
관찰하는 동작, 오류, ownership, ordering, timeout, cancellation, fencing, recovery와 limit을 정의하며
언어별 signature를 반복하지 않는다.

Runtime 이관은 기존 Framework public API를 바꾸는 근거가 아니다. 11.0의 의도적인 public contract 추가는
다음 범위로 제한한다.

- Host의 logical continuity 종료와 bounded 종료를 구분하는 `Retire`·`Shutdown`과 terminal result
- Actor·Instance Spot type 등록에 연결하는 maintenance transfer policy
- Location provider가 구현하는 owner authority compare-exchange와 Checkpoint Store capability
- Target eligibility에 필요한 `ApplicationVersion`과 maintenance wave 설정

11.0에서 제거하는 중복 표면은 별도 상태 기계를 공개하던 Actor·Instance phase Store, Channel 호출의
MeshName+ChannelName 선택 overload, 역할 선택 전 Server 설정, handler base의 MeshName과 factory policy와
분리된 transfer registry다. 기존 호환 호출을 유지해야 하는 언어는 새 상태와 결과를 만들지 않는 deprecated
facade로만 둘 수 있으며, 새 code와 guide의 기준으로 사용하지 않는다.

Core service handle, claim, receive batch, reply token, transfer token, operation poll 순서와 wire frame은
Framework public API로 옮기지 않는다. 각 언어 runtime이 이 정보를 내부에 감추고 공통 protocol schema,
golden fixture와 동작 trace로 같은 결과를 증명한다.

## 2. 문서와 정식 계약 대조 위치

| 대조 문서 | 확인할 질문 | 정식 계약 소유 문서 |
|---|---|---|
| [MeshNode와 channel topology](01-mesh-node.ko.md) | Service node가 물리 topology, channel membership과 readiness를 어떻게 소유하는가 | [Channel topology](../../../framework/spec/server/10-channel-topology.ko.md), [MeshNode](../../../framework/spec/server/21-mesh-node.ko.md), [Transport liveness](../../../framework/spec/server/55-transport-liveness.ko.md) |
| [Dispatch](02-dispatch.ko.md) | Message admission, handler turn과 terminal completion을 어떻게 보장하는가 | [Interaction model](../../../framework/spec/02-interaction-model.ko.md), [Async 실행 정책](../../../framework/spec/04-async-execution-policy.ko.md), [Channel 메시징](../../../framework/spec/server/11-channel-messaging.ko.md) |
| [Spot](03-spot.ko.md) | Spot lifecycle, direct call, multicast, subscription과 timer를 어떻게 처리하는가 | [Spot 메시징](../../../framework/spec/server/20-spot-messaging.ko.md), [Spot Actor](../../../framework/spec/server/23-spot-actor.ko.md), [Spot 주소 메시징](../../../framework/spec/server/24-spot-address-messaging.ko.md) |
| [Actor](04-actor.ko.md) | Actor identity, membership, queue와 transfer ordering을 어떻게 보존하는가 | [Actor 모델](../../../framework/spec/server/22-actor-model.ko.md), [Spot Actor](../../../framework/spec/server/23-spot-actor.ko.md), [Session Actor dispatch](../../../framework/spec/server/31-session-actor-dispatch.ko.md), [Host maintenance](../../../framework/spec/server/54-graceful-drain-handoff.ko.md) |
| [STREAM session](05-stream-session.ko.md) | Packet session, Actor binding과 transfer barrier를 어떻게 처리하는가 | [STREAM session](../../../framework/spec/server/30-stream-session.ko.md), [Session Actor dispatch](../../../framework/spec/server/31-session-actor-dispatch.ko.md), [Host maintenance](../../../framework/spec/server/54-graceful-drain-handoff.ko.md) |
| [Instance Spot](06-instance-spot.ko.md) | Logical address의 owner claim, activation, close와 maintenance materialization을 어떻게 처리하는가 | [Spot 주소 메시징](../../../framework/spec/server/24-spot-address-messaging.ko.md), [Location runtime](../../../framework/spec/server/40-location-runtime.ko.md), [Host maintenance](../../../framework/spec/server/54-graceful-drain-handoff.ko.md) |
| [Location과 maintenance](07-location-maintenance.ko.md) | Durable authority, `Retire`·`Shutdown`, transfer와 recovery를 어떻게 조정하는가 | [Location runtime](../../../framework/spec/server/40-location-runtime.ko.md), [Redis location store](../../../framework/spec/server/41-location-store-redis.ko.md), [Host maintenance](../../../framework/spec/server/54-graceful-drain-handoff.ko.md) |
| [Liveness와 observability](08-liveness-observability.ko.md) | Connection 생존 상태, snapshot, event, metric과 trace를 어떻게 관측하는가 | [Runtime monitoring](../../../framework/spec/server/50-runtime-monitoring.ko.md), [Runtime metrics](../../../framework/spec/server/51-runtime-metrics.ko.md), [Message flow tracing](../../../framework/spec/server/52-message-flow-tracing.ko.md), [Flow correlation](../../../framework/spec/server/53-flow-correlation.ko.md), [Transport liveness](../../../framework/spec/server/55-transport-liveness.ko.md) |
| [Core raw runtime 정본](09-core-raw-runtime-boundary.ko.md) | Core 11 raw runtime 계약 정본은 어디에 있는가 | [Core 11 raw 공개 경계](../../../../../core/doc/spec/core/09-runtime-boundary.ko.md) |

## 3. Core Service 원문 no-loss 표

아래 표는 `core/doc/spec/core/service/` 원문의 각 절이 어느 대조 문서에서 다뤄지는지 나타낸다.
C ABI type layout, `struct_size`, opaque token과 함수 signature는 Framework application 계약 대상이 아니다.
그 안에 담긴 identity, ownership, state, 오류와 ordering 의미는 §2가 가리키는 정식 spec에서 확인한다.

| Core 원문 | 보존 문서 | 보존하는 의미 |
|---|---|---|
| `README` Service 구조 | [README](README.ko.md), [Dispatch](02-dispatch.ko.md) | Runtime 책임 분리, message ownership과 오류 분류 |
| `README` Versioned 구조체 공통 계약 | [Dispatch §6](02-dispatch.ko.md#6-message와-callback-ownership) | Caller와 runtime의 storage 수명, partial output 금지, stale view 차단. C ABI layout은 Framework public contract 대상이 아님 |
| `01-mesh-node` §1 범위와 불변 조건 | [MeshNode §1~§2](01-mesh-node.ko.md#1-책임과-불변-조건) | MeshName, RID, 물리 topology와 channel membership 경계 |
| `01-mesh-node` §2 공개 상수와 타입 | [MeshNode §2](01-mesh-node.ko.md#2-identity와-lifecycle), [Observability §3](08-liveness-observability.ko.md#3-snapshot) | Node·peer state, identity, metadata와 bounded limit 의미. C type layout은 Framework public contract 대상이 아님 |
| `01-mesh-node` §3 생성과 lifecycle | [MeshNode §2~§3](01-mesh-node.ko.md#2-identity와-lifecycle), [Maintenance §2](07-location-maintenance.ko.md#2-host-lifecycle과-result) | Startup, ready, drain, stop과 child resource 순서 |
| `01-mesh-node` §4 Channel membership | [MeshNode §4](01-mesh-node.ko.md#4-channelname-role과-membership) | Client·Server role, immutable membership, mutable weight와 revision |
| `01-mesh-node` §5 Peer connection과 admission | [MeshNode §5](01-mesh-node.ko.md#5-discovery와-peer-admission), [Liveness §2](08-liveness-observability.ko.md#2-transport-liveness) | Manual·automatic discovery, identity 검증, duplicate peer와 generation 교체 |
| `01-mesh-node` §6 Node와 Channel 메시징 | [Dispatch §3~§5](02-dispatch.ko.md#3-one-way-submit), [MeshNode §6](01-mesh-node.ko.md#6-readiness와-target-선택) | Node direct, Channel select-one, local target, FIFO와 request completion |
| `01-mesh-node` §6.1 Node application claim 수신 | [Dispatch §2](02-dispatch.ko.md#2-application과-infrastructure-progress) | Node application turn과 infrastructure completion 분리 |
| `01-mesh-node` §7 Logical Multicast publisher | [Spot §5](03-spot.ko.md#5-channel-scoped-logical-multicast) | Target snapshot, target별 admission, partial success와 detail |
| `01-mesh-node` §8 Application metadata와 wire message | [Dispatch §6](02-dispatch.ko.md#6-message와-callback-ownership), [Observability §5](08-liveness-observability.ko.md#5-message-flow와-correlation) | Immutable metadata, complete message admission과 correlation 은닉 |
| `01-mesh-node` §9 Option과 handle 지원 | [MeshNode §8](01-mesh-node.ko.md#8-listener와-runtime-limit), [Dispatch §7](02-dispatch.ko.md#7-backpressure와-limit) | HWM·timeout·message size·mailbox budget의 Framework 의미 |
| `01-mesh-node` §10 Status와 query | [Observability §3](08-liveness-observability.ko.md#3-snapshot) | Bounded node·peer snapshot과 caller-owned immutable 결과 |
| `01-mesh-node` §11 Thread safety와 오류 우선순위 | [Dispatch §8](02-dispatch.ko.md#8-동시성-종료와-오류-우선순위) | Concurrent call, lifecycle race와 validation 우선순위 |
| `02-dispatch` §1 공개 타입과 상수 | [Dispatch §1~§2](02-dispatch.ko.md#1-책임-경계) | Owner, application·infrastructure domain, operation과 terminal result taxonomy |
| `02-dispatch` §2 Ready handler | [Dispatch §2](02-dispatch.ko.md#2-application과-infrastructure-progress) | Lost-wakeup 없는 ready signal과 callback 해제 barrier |
| `02-dispatch` §3 Ready batch와 claim | [Dispatch §2·§6](02-dispatch.ko.md#2-application과-infrastructure-progress) | Single owner turn, bounded fairness와 claim lifetime. Batch API는 runtime 내부에 둠 |
| `02-dispatch` §4 Receive batch | [Dispatch §6](02-dispatch.ko.md#6-message와-callback-ownership) | Complete multipart/message atomicity, retain·copy와 view lifetime |
| `02-dispatch` §5 Operation과 reply | [Dispatch §4~§5](02-dispatch.ko.md#4-request와-terminal-completion) | Operation correlation, one-shot reply와 terminal completion 한 번 |
| `02-dispatch` §6 Close와 progress | [Dispatch §8](02-dispatch.ko.md#8-동시성-종료와-오류-우선순위), [Maintenance §10](07-location-maintenance.ko.md#10-abort-shutdown과-cleanup) | Accepted work 진행, claim revoke와 safe cleanup |
| `03-spot` §1 책임 경계 | [Spot §1](03-spot.ko.md#1-책임과-spot-종류) | Spot logical owner, Actor queue와 classic PUB/SUB 분리 |
| `03-spot` §2 공개 타입 | [Spot §2](03-spot.ko.md#2-identity와-generation), [Observability §3](08-liveness-observability.ko.md#3-snapshot) | Spot kind, identity, generation과 status 의미 |
| `03-spot` §3 생성, 조회와 종료 | [Spot §3](03-spot.ko.md#3-생성-조회와-종료), [Instance Spot §3](06-instance-spot.ko.md#3-activation-시작-표면) | Entry·User Spot의 local-only create/get-or-create, Instance address cold activation과 handle 수명 |
| `03-spot` §4 Channel send와 request | [Spot §4](03-spot.ko.md#4-direct와-channel-호출), [Dispatch §3~§5](02-dispatch.ko.md#3-one-way-submit) | Spot-originated Channel call, completion owner와 timeout |
| `03-spot` §5 Spot direct send와 request | [Spot §4](03-spot.ko.md#4-direct와-channel-호출), [Instance Spot §5](06-instance-spot.ko.md#5-address-send와-request) | Exact activation 대상, stale generation과 자동 재제출 금지 |
| `03-spot` §6 Logical Multicast publish | [Spot §5](03-spot.ko.md#5-channel-scoped-logical-multicast) | ChannelName·topic scope, per-target ordering와 result detail |
| `03-spot` §7 Local subscription | [Spot §6](03-spot.ko.md#6-subscription과-dispatch) | Node-local exact·prefix match와 atomic registration change |
| `03-spot` §8 Receive record와 control lane | [Spot §6](03-spot.ko.md#6-subscription과-dispatch), [Dispatch §2](02-dispatch.ko.md#2-application과-infrastructure-progress) | Spot payload와 Actor control 분리, application turn 직렬화 |
| `03-spot` §9 Spot timer | [Spot §7](03-spot.ko.md#7-timer와-lifecycle-fence) | Spot generation에 귀속되는 timer, callback ordering와 stale tick 차단 |
| `03-spot` §10 Option과 thread safety | [Spot §8](03-spot.ko.md#8-limit-동시성과-종료) | Timeout owner, concurrent call과 destructive close race |
| `04-actor` §1 공개 타입 | [Actor §2](04-actor.ko.md#2-identity와-authority-fence) | Actor identity, ObjectGeneration과 authority fence 의미 |
| `04-actor` §2 생성, 조회와 종료 | [Actor §3](04-actor.ko.md#3-create-resolve와-destroy) | Atomic create, factory admission, lookup와 destroy fence |
| `04-actor` §3 Spot membership | [Actor §4](04-actor.ko.md#4-spot-membership) | Join accept·reject, leave, lifecycle callback와 authority CAS |
| `04-actor` §4 Actor 메시징 | [Actor §5](04-actor.ko.md#5-messaging과-turn) | Actor direct queue, source completion owner, FIFO와 metadata |
| `04-actor` §5 Actor claim | [Actor §5](04-actor.ko.md#5-messaging과-turn), [Dispatch §2](02-dispatch.ko.md#2-application과-infrastructure-progress) | Actor application turn과 infrastructure progress 분리 |
| `04-actor` §6 Transfer fence | [Actor §6](04-actor.ko.md#6-maintenance-transfer), [Maintenance §6~§8](07-location-maintenance.ko.md#6-retire-preflight와-reversible-seal) | Authority, seal, checkpoint, commit, activation, replay와 stale-owner fencing |
| `04-actor` §7 Shutdown과 thread safety | [Actor §8](04-actor.ko.md#8-bound-session과-abort), [Maintenance §10](07-location-maintenance.ko.md#10-abort-shutdown과-cleanup) | Lifecycle mutation serialization, accepted work와 transfer terminal 처리 |
| `05-stream-session` §1 책임과 handle | [STREAM §1~§2](05-stream-session.ko.md#1-책임-경계) | Raw STREAM과 packet session 경계, session identity와 ownership |
| `05-stream-session` §2 lifecycle | [STREAM §3](05-stream-session.ko.md#3-session-lifecycle) | Start, connect, disconnect, drain과 resource cleanup |
| `05-stream-session` §3 Session과 Actor binding | [STREAM §5](05-stream-session.ko.md#5-actor-binding-authority) | Binding uniqueness, token generation, bind·unbind와 stale protection |
| `05-stream-session` §4 Session에서 Actor로 보내기 | [STREAM §6](05-stream-session.ko.md#6-session에서-actor로-dispatch) | Complete packet, Actor queue admission, FIFO와 reply correlation |
| `05-stream-session` §5 Actor에서 bound session으로 보내기 | [STREAM §7](05-stream-session.ko.md#7-actor에서-bound-session으로) | Current binding snapshot, push·close와 stale token 차단 |
| `05-stream-session` §6 Actor 이동 barrier | [STREAM §8](05-stream-session.ko.md#8-actor-transfer-barrier), [Actor §6](04-actor.ko.md#6-maintenance-transfer) | Session sequence barrier, bounded pending admission, target activation ordering |
| `05-stream-session` §7 Thread safety와 오류 | [STREAM §9](05-stream-session.ko.md#9-동시성-종료와-오류), [Dispatch §8](02-dispatch.ko.md#8-동시성-종료와-오류-우선순위) | Session별 serialization, lifecycle race와 typed failure |

## 4. Crosswalk 동기화 gate

이 묶음은 구현 뒤 다른 디렉터리로 이동하지 않는다. 다음 조건을 같은 candidate에서 확인하여 정식 계약과
migration 대조 자료가 어긋나지 않게 유지한다.

- Core 10 service 원문의 application 의미마다 §2의 정식 spec 소유 문서와 언어별 exact interface가 있다.
- Target 문서는 정식 spec에 없는 공개 동작을 추가하지 않는다.
- 정식 spec이나 exact interface가 바뀌면 관련 no-loss 행과 target 설명을 같은 변경에서 수정한다.
- 내용이 충돌하면 정식 spec을 적용하며 target 문서만 근거로 구현이나 contract test를 변경하지 않는다.
- `scripts/verify-framework-doc-contracts.sh`가 link, 필수 소유권 문장과 문서 집합을 검증한다.
