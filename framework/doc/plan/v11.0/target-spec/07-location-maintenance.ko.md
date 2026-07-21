# Location, host maintenance와 stateful recovery

이 문서는 ZLink Framework 11.0 service runtime의 Location Store, host lifecycle, Actor·Instance Spot authority,
stateful transfer와 recovery 계약을 정의한다. 각 언어 runtime은 같은 공개 의미를 제공하며 Core raw transport에
Location key, object authority와 transfer lifecycle을 추가하지 않는다.

## 1. Location data 경계

Location Store는 ephemeral discovery data와 durable object authority를 분리한다.

| Data | Identity와 주요 값 | Lifetime |
|---|---|---|
| MeshNode descriptor | MeshName, RID, opaque LifecycleGeneration, DescriptorRevision, endpoint, Channel, capability, owner lease token | host owner lease에 종속 |
| ClientServer server descriptor | ChannelName, RID, opaque LifecycleGeneration, DescriptorRevision, endpoint, weight, owner lease token | host owner lease에 종속 |
| Fanout publisher descriptor | ChannelName, RID, opaque LifecycleGeneration, DescriptorRevision, endpoint, owner lease token | host owner lease에 종속 |
| Actor authority | `(MeshName, ActorId)`, type, ObjectGeneration, owner와 current Spot, authority metadata | explicit fenced delete까지 durable |
| Spot authority | `(MeshName, SpotRid)`, kind, type, ObjectGeneration, owner, authority metadata | explicit fenced delete까지 durable |
| Routing allocation | allocation group, slot과 exact host owner lease token | host owner lease에 종속 |

Fanout subscriber descriptor는 게시하지 않는다. Subscriber는 해당 ChannelName의 publisher descriptor만 발견하고
publisher마다 전용 SUB socket 하나를 사용한다.

Descriptor는 startup에서 complete value를 만든 뒤 atomic validation을 통과해야 한다. Encoded descriptor는 최대
1 MiB이고 type capability, stateful capability와 type별 readable state contract vector는 각각 최대 1024개다.
초과한 descriptor를 truncate, split하거나 일부 publish하지 않는다.

Descriptor page는 request item 1..1000개, encoded 최대 4 MiB다. Framework는 scope change stamp를 page 열거
전후에 읽고 값이 같을 때만 complete page set을 적용한다. Continuation token은 provider-issued opaque value다.
Provider가 unbounded list를 먼저 만들거나 Redis `SCAN` 결과를 그대로 공개하면 안 된다.

LifecycleGeneration은 모든 topology에서 non-zero opaque equality token이다. Store-backed descriptor는 exact owner
lease와 descriptor token으로 stale lifecycle을 fence한다. Manual/no-Store peer는 runtime이 CSPRNG nonce를 만들고
current physical connection handover와 service liveness로 stale peer를 fence한다. Application이 generation을
설정하지 않으며 숫자 크기로 lifecycle을 비교하지 않는다.

DescriptorRevision만 같은 descriptor lifecycle 안에서 strictly increasing order를 사용한다. Framework caller가
발급하며 provider global counter가 아니다. 값이 `2^63-1`에서 다음 revision을 요구하면 wrap하지 않고 host를
`Error`로 seal하며 publish하지 않는다.

## 2. Host lifecycle과 result

Host lifecycle은 `Preparing`, `Serving`, `Draining`, `Stopped`, `Error` 순서로 진행한다. `IsReady`는
`Serving`에서만 true다. `Retire`와 `Shutdown`은 동시에 실행하지 않으며 이미 시작한 operation의 terminal result를
공유한다.

Termination result는 정식 [Host Retire, Shutdown & Handoff spec](../../../framework/spec/server/54-graceful-drain-handoff.ko.md)의
closed outcome과 reason을 그대로 사용한다.

| 값 | Outcome | 허용 reason | 의미 |
|---:|---|---|---|
| 0 | `Stopped` | `None` | 요구한 host 종료 절차를 정상 완료함 |
| 1 | `Blocked` | `TargetUnavailable`, `StoreUnavailable`, `TransferDisabled`, `StateIncompatible`, `DeadlineExceeded`, `RuntimeNotReady` | Retire preflight 또는 pre-Captured 단계에서 admission과 runtime state를 유지하거나 복원함 |
| 2 | `ForceStopped` | `DeadlineExceeded`, `TransferFailed`, `TeardownFailed` | Admission을 닫은 뒤 bounded teardown으로 host resource를 정리함 |

Reason wire 값은 `None=0`, `TargetUnavailable=1`, `StoreUnavailable=2`, `TransferDisabled=3`,
`StateIncompatible=4`, `DeadlineExceeded=5`, `TransferFailed=6`, `TeardownFailed=7`,
`RuntimeNotReady=8`이다. 이 표에 없는 outcome과 reason 조합은 protocol 오류다.

Teardown을 정상 완료할 수 없어도 별도 outcome을 만들지 않는다. Bounded teardown을 수행한 뒤
`ForceStopped/TeardownFailed`로 끝낸다.

Preflight 또는 seal 전에 deadline이 끝나면 `Blocked/DeadlineExceeded`이며 host, descriptor와 admission을 변경하지
않는다. Seal 뒤 deadline이 끝나 bounded teardown을 수행하면 `ForceStopped/DeadlineExceeded`다.

## 3. Host owner lease와 local deadline

한 host process lifecycle은 `(OwnerId, LeaseGeneration)` token 하나를 사용한다. 같은 host의 모든 descriptor,
routing allocation, Actor·Spot authority와 maintenance role이 이 token을 공유한다. OwnerId는 Framework가 만드는
재사용 불가능한 값이고 LeaseGeneration은 provider transaction domain의 durable global counter가 발급한다.

Provider는 exact Claim, Read, Renew와 Release만 제공한다. Claim과 expired-row Takeover가 새 LeaseGeneration을
발급해야 하는데 counter가 `2^63-1`이면 closed `GenerationExhausted`를 반환한다. 이 결과는 non-retriable이며
row, index와 counter의 mutation·consumption이 0이다. Renew와 Release는 이 결과를 반환하지 않는다.

Location Store를 사용하는 모든 host는 startup에서 다음 관계를 검증한다.

```text
OwnerLeaseRenewInterval + OwnerLeaseRenewTimeout
    < OwnerLeaseTtl - OwnerLeaseFencingMargin
```

공통 기본값은 renew interval 5초, TTL 15초, renew timeout 3초, fencing margin 5초다. Store read를 시작한 local
monotonic 시각을 보존하고 `max(0, ExpiresAt - StoreNow - margin)`을 더해 admission deadline을 계산한다. 응답을
받은 시점부터 TTL 전체를 다시 부여하지 않는다.

이 deadline 하나가 descriptor publish, Actor·Spot·Instance message·timer·factory, transfer CAS와 target
reservation을 함께 fence한다. `StoreFailureGrace`는 discovery reconcile과 새 outbound connect만 유예하며 owner,
coordinator와 authority deadline을 연장하지 않는다.

## 4. Durable authority Store

Authority Read는 `Missing(StoreNow)` 또는 `Found`의 closed union이다. Provider Found는 opaque payload,
StoreVersion, ObjectGeneration, AuthorityOwnerGeneration과 StoreNow만 반환한다. Framework가 opaque payload를
decode해 OwnerId, OwnerLeaseGeneration, owner node와 object state를 얻는다. Missing은
synthetic version이나 generation을 만들지 않는다. Authority row는 TTL을 사용하지 않고 explicit expected-version
CAS delete까지 유지한다.

Generation은 다음 책임을 갖는다.

| Value | Responsibility |
|---|---|
| ObjectGeneration | delete 뒤 같은 canonical key로 새 object가 생긴 incarnation |
| AuthorityOwnerGeneration | 같은 object incarnation의 authority owner 변경 순서 |
| StoreVersion | exact CAS expectation의 opaque row revision |
| OwnerLeaseGeneration | current host process lifecycle fence |

ObjectGeneration, AuthorityOwnerGeneration과 StoreVersion용 StoreRevision은 provider transaction domain의 durable
global counter를 사용하고 maximum은 `2^63-1`이다. 필요한 counter가 maximum이면 authority write는
`GenerationExhausted`를 반환하며 row, index와 모든 counter를 변경하거나 소비하지 않는다. Routing allocation의
`GroupExhausted`와 구분한다.

CAS expectation은 `Missing` 또는 `Found(StoreVersion)`이다. Mutation은 Preserve, NewOwner, NewObject와 Delete의
closed set이다. NewOwner는 ObjectGeneration을 유지하고 새 AuthorityOwnerGeneration을 발급한다. NewObject는
Missing에서만 두 generation을 함께 발급한다. Delete는 current index를 제거하지만 per-key counter, version
tombstone이나 generation key를 남기지 않는다.

Authority scan은 1..1000 item, encoded 4 MiB와 최대 4096-byte opaque cursor를 사용한다. 첫 page가 provider
snapshot watermark와 scan lease를 만들며 그 watermark에 존재한 row incarnation을 canonical key byte 순서로 한
번 반환한다. Framework는 scan item에 의존해 바로 write하지 않고 exact Read와 expected StoreVersion CAS를 다시
수행한다.

## 5. Instance Spot cold activation recovery

Instance authority key는 `(MeshName, SpotRid)`이고 Spot kind, registered Instance type, ObjectGeneration, current
owner와 state를 저장한다. Descriptor capability는 Instance type, transfer policy와 Snapshot일 때 readable state
contract를 게시하지만 authority를 대신하지 않는다.

Missing Instance를 만드는 유일한 logical operation은 `InstanceSpotAddress` send 또는 request다. Source는 complete
descriptor와 exact target lease를 검증하고 outbound wire 전에 `Missing` expectation의 NewObject CAS로
`ColdActivating` row를 만든다. Target은 claim하지 않고 exact authority를 재검증한 뒤 factory, initialize와 Ready
barrier를 수행한다. Claim 이후 모든 state는 같은 non-zero ObjectGeneration을 유지한다.

Durable `ColdActivating` row가 activation intent다. Exact target owner host는 Serving 전 initial authority scan과
Serving 중 bounded background scan/watch reconcile에서 자신 소유의 row를 발견하면 original source message 없이
factory와 Ready barrier를 idempotent하게 재개한다. Late submit과 scan은 object key, ObjectGeneration,
AuthorityOwnerGeneration과 owner token으로 key를 정한 local activation registry 하나로 수렴한다.

Target lease가 stale이면 caller 또는 recovery coordinator가 expected StoreVersion NewOwner CAS로 새 eligible owner를
선택하며 ObjectGeneration을 유지한다. Source submit이 target에 도달하지 않았으면 original payload와 handler call을
숨겨서 재제출하지 않는다. Caller는 normal timeout 또는 failure terminal을 받는다.

Factory, initialize 또는 Ready commit이 실패하면 local barrier를 failed·sealed로 고정한다. Queued/current request는
같은 typed failure로 terminal-once 완료하고 one-way call은 drop event와 metric만 기록한다. Runtime은 같은
StoreVersion, ObjectGeneration, AuthorityOwnerGeneration, OwnerId와 OwnerLeaseGeneration expectation으로 row를
fenced delete한다. Cancellation, timeout 또는 response loss는 exact Read로 reconcile한다.

Delete가 확정될 때까지 registry는 failed 상태를 유지하고 이후 caller에게 같은 failure를 반환한다. Factory와
payload를 숨겨서 다시 실행·제출하지 않는다. Missing을 확인한 뒤 registry, scope와 capacity slot을 정리하며 그
다음 caller만 NewObject claim으로 새 generation을 발급한다. Delete 전 process crash로 scan recovery가 factory를
재개할 수 있으므로 factory·initialize callback은 retry-safe해야 한다.

## 6. Retire preflight와 reversible seal

Process의 모든 MeshNode는 maintenance barrier 하나를 공유한다. Preflight는 capability, eligible target과 bounded
headroom만 확인하고 final reservation을 만들지 않는다. Barrier 획득이 object, membership, session과 operation
inventory의 linearization point다.

Blocker가 있으면 reservation을 만들지 않고 host state, descriptor, readiness와 admission을 그대로 유지한 채
`Blocked`로 끝낸다. Preflight가 성공하면 object마다 다음 순서를 수행한다.

1. 신규 message·timer admission을 reversible하게 seal한다.
2. Seal 전에 수락한 active turn과 timer turn을 source에서 완료한다.
3. Connection-bound source에서 수락한 모든 work와 모든 bound-session request를 terminal drain한다. 이 work는
   durable journal에 넣지 않는다. Deadline 안에 끝나지 않으면 pre-Captured abort,
   `Blocked/TransferDisabled`와 admission 복원으로 끝낸다.
4. Exact participant boundary와 checked message·byte count를 계산하고 Preparing CAS를 수행한다.
5. Deterministic checkpoint stream을 immutable chunk와 root manifest로 쓰고 Captured CAS로 complete root를
   authority에 연결한다.
6. Exact inventory로 target offer·accept·reservation ACK를 끝내고 Prepared CAS를 수행한다.
7. 모든 object가 Prepared가 된 뒤 host state와 descriptor를 Draining으로 publish한다.

Durable accepted journal의 모든 frozen record는 exact lease-backed source OwnerId·LeaseGeneration을 포함한다.
Connection-bound source record는 send/request 여부와 관계없이 capture하지 않는다. Lease-backed non-session
request만 reply relay ACK와 source lease expiry barrier에 들어갈 수 있다.

Accepted journal의 crash replay 보장은 complete root가 Captured CAS로 연결된 뒤부터만 성립한다. Preparing 또는
checkpoint Put 중 source crash는 transfer를 fenced abort한다. Original request는 normal connection failure, timeout
또는 cancellation terminal을 따르며 accepted replay나 hidden remote creation을 보장하지 않는다. Unlinked Put은
orphan cleanup 대상이다.

## 7. Checkpoint format과 retention

Checkpoint logical stream은 최대 256 GiB, immutable chunk data는 최대 64 MiB, root manifest chunk reference는
최대 4096개다. Manifest는 logical version, total length, total CRC32C와 ordered chunk reference·length·CRC32C를
포함한다. Provider는 bytes를 opaque하게 보관한다.

Retention은 24시간, renew threshold는 12시간이다. Captured와 Prepared CAS 직전에 complete staged tree의 remaining
lifetime이 12시간보다 큰지 확인하고 필요하면 전체 tree를 renew한다. Missing 또는 partial renew는 fail-closed
precommit abort다. Authority의 root reference를 바꿀 때는 immutable root를 먼저 만든 뒤 expected StoreVersion CAS
한 번으로 root, checksum, journal count, terminal count와 pending relay count를 함께 교체한다.

Snapshot state는 `framework-json-v1` typed profile을 사용한다. 64-bit integer는 canonical decimal string, 32-bit
이하는 JSON integer, enum은 case-sensitive name, bytes는 padded RFC4648 Base64다. Duplicate property, missing
required property, non-finite number와 implicit DateTime·decimal·UUID는 허용하지 않는다. Validation 뒤 application
bytes는 original bytes 그대로 opaque하게 저장한다.

## 8. Transfer authority와 target replacement

Stable `TransferId`는 checkpoint, journal, replay, completion과 terminal identity다. `TargetAttemptGeneration`은 같은
transfer의 target reservation attempt fence일 뿐이며 target replacement만으로 checkpoint를 다시 쓰지 않는다.

| Phase | Required authority shape |
|---|---|
| Preparing, Captured | main owner와 immutable source token은 source이며 target은 없음 |
| Prepared | main owner는 source, exact target attempt·owner lease·node·reservation과 checkpoint가 존재 |
| Committed, Activating, Activated, Cleaning | main owner는 current target이며 같은 attempt·reservation·checkpoint 유지 |
| Completed | target activation과 durable source cleanup이 terminal이고 admission은 아직 sealed |
| Aborted | main owner는 source이며 abort route ACK, cleanup과 steady normalization 전까지 sealed |

Prepared→Committed는 NewOwner CAS 한 번이다. Source owner token과 source node identity는 terminal까지 immutable하다.
Target replacement는 target attempt, target owner·node와 reservation만 교체한다. Post-commit replacement는
Committed로 재진입하고 stale attempt callback은 completion commit이나 admission을 열 수 없다.

Factory와 restore callback은 attempt 사이에서 at-least-once로 실행되고 stale attempt와 겹칠 수 있다. Application은
retry-safe callback을 구현해야 하며 Framework는 public TransferId나 external side effect의 exactly-once를 보장하지
않는다.

## 9. Completion, route barrier와 Ready

Target은 restore와 journal replay가 끝나도 Activated에서 application admission과 resolver Ready를 열지 않는다.
Source cleanup result를 authority에 durable하게 기록하고 Completed CAS를 수행한 뒤, bound-session route switch와
각 `sessionTransferRouted` ACK를 완료한다. 마지막으로 maintenance transfer state를 제거해 steady target authority로
normalize한 뒤에만 application admission과 Ready projection을 연다. Maintenance authority는 phase와 관계없이
resolver에서 Ready가 아니다.

Accepted request 수와 terminal completion 수는 같아야 한다. Pending relay count는 delivery가 남은 completion
수다. Late completion은 새 immutable root를 만든 뒤 expected StoreVersion CAS로 연결한다. CAS loser의 root는
orphan이다.

Reply delivery state는 `pending → terminalReceived | alreadyTerminal | sourceLeaseExpired`만 허용한다.
`replyRelayAck`는 stable TransferId와 OperationId, exact request-source lease fence를 사용하고 payload·metadata를
포함하지 않는다. Physical connection close와 reconnect는 terminal proof가 아니다. Source lease가 유효한 채 Retire
deadline을 넘으면 `ForceStopped`로 한 번 완료하고 root와 reply bytes를 24시간 유지한다.

## 10. Abort, Shutdown과 cleanup

Commit 전 abort는 source admission을 sealed 상태로 유지한 채 먼저 durable Aborted CAS를 수행한다. 그 뒤에만
session abort route와 ACK, reservation·checkpoint orphan cleanup, steady source normalization을 수행하고 admission을
다시 연다. Durable Aborted 전 route switch나 reopen은 허용하지 않는다.

Commit 뒤에는 source로 rollback하지 않는다. Current target 또는 recovery coordinator가 activation, cleanup,
Completed, route ACK와 steady normalization을 이어간다. `transferComplete`는 durable sourceCleanupState가
`completed` 또는 `sourceLeaseExpired`일 때만 유효하다.

Shutdown은 새 transfer를 시작하지 않는다. 신규 admission과 reservation을 닫고 이미 시작한 transfer,
Actor handoff와 STREAM barrier를 deadline까지 처리한 뒤 object, descriptor, routing allocation과 owner lease를
역순으로 해제한다. Deadline을 넘으면 callback·timer·socket admission을 차단하고 `ForceStopped`를 한 번
완료한다. `RemoveAllByOwner`는 exact host token의 ephemeral descriptor와 routing allocation만 제거하며 durable
authority를 삭제하지 않는다.

## 11. Provider failure와 cancellation

Provider call 시작 전 cancellation은 I/O와 commit을 막을 수 있다. 시작 뒤 cancellation, timeout, exception은
commit 여부가 ambiguous하다. Authority mutation은 exact Read로 reconcile하고 content-addressed checkpoint Put은
verify 또는 idempotent retry한다. 연결되지 않은 Put은 orphan이다.

Async input은 completion까지 immutable하고 유효해야 한다. Provider가 input을 보존하면 복사한다. Result bytes는
immutable stable snapshot이다. Store outage가 admission seal 전 발생하면 Retire는 `Blocked/StoreUnavailable`로
끝난다. Seal 뒤 authority 진행을 증명할 수 없으면 deadline까지 recovery하고 이후 bounded force stop으로 끝낸다.

## 12. 검증 요구

- Manual/no-Store lifecycle token을 숫자 순서로 비교하지 않고 restart·handover stale connection을 거부한다.
- Owner lease Claim·Takeover와 authority write의 `GenerationExhausted`가 atomic no-write·no-consume다.
- Instance source가 NewObject CAS를 outbound 전에 수행하고 target은 claim하지 않는다.
- Lost Instance submit 뒤 target scan이 exact owned `ColdActivating` intent를 Ready로 수렴시키되 original payload를
  재제출하지 않는다.
- Retire preflight가 실패하거나 pre-Captured request drain이 끝나지 않으면 state와 admission을 복원한다.
- Captured 전 source crash에 durable replay를 주장하지 않고 Captured 뒤 complete root에서만 recovery한다.
- Checkpoint staging lease가 Captured·Prepared 직전에 fail-closed로 검증된다.
- Activated, Cleaning과 Completed에서 target admission이 닫혀 있고 route ACK·steady normalization 뒤에만 열린다.
- Physical connection close가 request completion의 terminal proof로 사용되지 않는다.
- Commit 전 abort와 commit 뒤 recovery가 서로 다른 방향으로 수렴한다.
- Shutdown deadline 뒤 terminal result와 cleanup이 한 번만 완료된다.
