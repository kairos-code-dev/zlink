# Maintenance and recovery runtime

[Stateful object runtime](04-stateful-object-runtime.ko.md) ·
[STREAM session runtime](06-stream-session-runtime.ko.md) ·
[Location runtime](../target-spec/07-location-maintenance.ko.md)

## 1. Host operation

Host coordinator는 `Preparing`, `Serving`, `Draining`, `Stopped`, `Error`를 소유한다. `Retire`는 continuity preflight와
transfer를 수행하고 `Shutdown`은 새 transfer를 만들지 않는다. 최초 accepted intent와 positive deadline이 shared
operation을 정하며 caller cancellation은 waiter만 끝낸다.

Process에 MeshNode가 여러 개 있어도 maintenance barrier는 하나다. Barrier가 object, session과 accepted operation
inventory를 고정한다. Preflight 실패와 Captured 전 abort는 host를 `Serving`으로 유지하고 admission을 복원한다.

## 2. Preflight와 seal

Preflight는 Store availability, transfer policy, compatible target capability와 bounded headroom을 확인한다. Final
target reservation은 reversible seal 뒤 exact inventory로 만든다. User Spot instance와 User Spot member Actor는
`TransferDisabled` blocker다. Actor transfer 대상은 source Entry Spot member뿐이다.

Source는 새 application·timer admission을 seal하고 active turn을 마친다. Connection-bound source에서 수락한 모든
send·request와 bound-session request는 Captured 전에 terminal drain한다. 이 work는 durable journal에 넣지 않는다.
Deadline 안에 끝나지 않으면 pre-Captured abort와 `Blocked/TransferDisabled`로 끝낸다. Frozen journal은 exact
lease-backed source OwnerId·OwnerLeaseGeneration이 있는 record만 포함한다.

## 3. Checkpoint durability boundary

Runtime은 exact participant boundary를 계산하고 deterministic logical stream을 immutable chunk와 root manifest로
기록한다. `Captured` CAS가 complete root를 authority에 연결한 시점부터 crash recovery의 durability가 시작된다.
Preparing 또는 checkpoint Put 중 source가 종료되면 transfer를 abort하고 연결되지 않은 data는 orphan cleanup
대상이다. 이 구간의 request는 일반 disconnect, timeout 또는 cancellation 계약을 따른다.

Checkpoint body는 `framework-json-v1` typed contract를 사용한다. Framework envelope, chunk·manifest layout,
checksum과 upper bound는 [Service wire protocol](02-wire-protocol.ko.md)의 schema와 golden fixture가 정한다.
Application bytes는 validation 뒤 그대로 보관하며 Provider는 내용을 해석하지 않는다.

## 4. Transfer authority와 peer data

`TransferId`는 CSPRNG로 만든 non-zero 128-bit stable identity다. Active transfer와 retained checkpoint root의
collision을 검사하고 충돌하면 새 값을 만든다. `TargetAttemptGeneration`은 같은 transfer에서 target을 교체한
attempt만 구분한다. TransferId와 checkpoint root는 target replacement로 바뀌지 않는다.

| phase | main owner와 처리 |
|---|---|
| `Preparing`, `Captured` | immutable source, target attempt 없음 |
| `Prepared` | source owner와 exact target reservation |
| `Committed` | owner와 object fence를 target으로 atomic 변경 |
| `Activating`, `Activated`, `Cleaning` | target sealed activation과 durable source cleanup |
| `Completed` | route ACK와 steady normalization까지 완료 |
| `Aborted` | source owner 유지, abort ACK와 normalization 뒤 admission 복원 |

`TransferData`는 checkpoint에도 있는 lease-backed frozen record를 target staging으로 보낸다. Deduplication key는
TransferId, TargetAttemptGeneration, participant ID와 sequence다. Attempt는 peer staging fence이며 durable terminal
identity는 TransferId와 OperationId다. Owner commit 전 staging은 checkpoint에서 재구성할 수 있는 cache다.

Request terminal result는 immutable completion chunk와 새 manifest를 먼저 쓰고 authority CAS로 root, checksum,
completion count와 pending relay count를 함께 바꾼 뒤 relay한다. Source는 OperationId별 terminal-once를 적용하고
`replyRelayAck`의 `terminalReceived` 또는 `alreadyTerminal`로 확인한다. Current source lease의 exact expiry만
`sourceLeaseExpired` proof가 된다.

## 5. Actor activation과 source cleanup

Actor target offer는 target Entry Spot identity를 reservation에 포함한다. Committed CAS가 Actor owner,
AuthorityOwnerGeneration과 current target Entry membership을 한 번에 바꾼다. Target factory·restore, target
`OnJoined`, journal replay 순서로 실행한다. Source `OnLeave`와 old Entry cleanup은 durable source cleanup이다.

`Activated`는 Ready가 아니다. Target은 source cleanup, Completed CAS, bound-session route commit·ACK와 steady
authority normalization을 모두 끝낼 때까지 sealed 상태를 유지한다. Resolver도 transfer payload가 남은 authority를
Ready로 투영하지 않는다.

## 6. Recovery

Captured 전 source failure는 fenced abort다. Captured 뒤 recovery coordinator는 current StoreVersion과 exact owner
lease를 확인해 phase를 재개한다. Commit 전 failure는 source authority를 유지할 수 있고 commit 뒤에는 target 또는
successor target에서 activation을 완료한다. Target replacement는 TargetAttemptGeneration과 reservation만 바꾸며
stale attempt completion을 거부한다.

Factory, restore와 handler side effect는 process failure 경계에서 반복될 수 있다. Framework는 external side effect의
exactly-once를 보장하지 않는다. Replay cursor CAS는 application replay progress만 확정한다.

Precommit abort는 durable Aborted CAS, session abort route ACK, reservation·orphan cleanup, steady source
normalization 순서로 진행한 뒤 admission을 연다. Aborted 결정 전에 session route를 되돌리거나 source admission을
열지 않는다.

## 7. Deadline과 terminal result

Preflight와 Captured 전 deadline은 source state를 복원하고 `Blocked/DeadlineExceeded`로 끝난다. Captured 뒤
deadline은 durability를 취소하지 않으며 bounded teardown 뒤 `ForceStopped/DeadlineExceeded`로 한 번만 끝난다.
`Draining` 이후 transfer나 cleanup failure도 `ForceStopped`다.

Termination completion은 신규 admission·timer·socket callback 차단, pending operation terminal 처리와 owned
resource release까지 기다린다. 반환하지 않는 application callback의 immutable input만 tombstone에 보관하며 callback은
runtime, provider와 executor를 다시 열 수 없다.

## 8. 검증 기준

- Captured 전 connection-bound work가 durable journal에 들어가지 않는다.
- Captured CAS 전 crash는 continuity replay가 아니라 fenced abort다.
- Stable TransferId가 target replacement, completion과 cleanup에서 유지된다.
- Actor owner와 target Entry membership이 atomic하게 commit된다.
- `Activated` 뒤 cleanup·Completed·route ACK·steady normalization 전 target admission이 열리지 않는다.
- Pre-Captured와 post-Captured deadline 결과가 서로 구분된다.
