# Instance Spot service

이 문서는 ZLink Framework 11.0에서 Instance Spot logical address, existing-owner resolve,
cold activation, direct messaging, close와 host maintenance materialization을 정의한다. Instance Spot은
Actor membership을 제공하지 않는 Spot이다.

## 1. Logical address와 public capability

Instance Spot address는 `MeshName`, stable Instance type과 Spot routing ID로 구성하는 immutable logical
identity다. Address는 runtime instance가 존재하지 않아도 만들 수 있으며 owner node RID, endpoint, Spot
generation, transfer identity, lease와 owner token을 포함하지 않는다.

Spot key uniqueness는 `(MeshName, Spot RID)`가 소유한다. 같은 key에 Entry Spot, User Spot 또는 서로 다른
Instance type이 동시에 owner를 가질 수 없다.

Instance Spot handle은 하나의 exact ObjectGeneration과 current owner route를 보유하는 opaque capability다.
Handle의 MeshName과 ObjectGeneration은 바뀌지 않는다. 같은 object의 owner route가 갱신될 수는 있지만 close 뒤
만들어진 새 ObjectGeneration으로 handle을 자동 retarget하지 않는다.

Application public API는 Location owner ID, AuthorityOwnerGeneration, OwnerLeaseGeneration, StoreVersion, node
generation과 internal activation token을 노출하지 않는다. 이 값은 Store CAS, runtime activation record와
message fence에서만 사용한다.

## 2. Actor-free lifecycle과 registration

Instance Spot factory는 stable type과 concrete instance type을 startup 전에 등록한다. 같은 MeshNode에 같은
Instance type 또는 concrete class를 중복 등록하면 startup이 실패한다. Factory가 Actor membership lifecycle을
사용하거나 Instance handler에 Actor join·leave·transfer와 Logical Multicast subscription을 등록하면 startup
또는 activation 전에 거부한다.

Instance Spot은 direct packet handler, timer와 outbound Channel·Spot·Actor call을 사용할 수 있다. Factory가
instance를 만든 뒤 actor-free configure와 message가 없는 initialize를 실행한다. 첫 application message는
lifecycle callback argument가 아니다.

Location `Ready` commit과 Framework activation barrier가 끝난 뒤 첫 message를 일반 Instance Spot application
queue에 한 번 제출한다. Factory, initialize와 barrier가 실패하면 application handler를 시작하지 않는다.

Type option을 생략하면 local MeshNode와 Instance type마다 maximum active instance 4096개와 activation timeout
3초를 사용한다. 명시한 maximum과 timeout은 모두 0보다 커야 하며 0을 default sentinel로 사용하지 않는다.
Pending message·byte budget은 MeshNode service runtime이 소유하며 type별 public queue option으로 반복하지 않는다.

## 3. Activation 시작 표면

Instance Spot에는 application이 target MeshNode를 고르는 별도 `Create`나 `GetOrCreate`를 제공하지 않는다.
User Spot manager의 generic create 표면도 Instance lifecycle type을 받지 않는다. 따라서 caller가 factory class,
target node와 owner claim 순서를 조립할 수 없다.

Missing Instance를 만드는 public operation은 `InstanceSpotAddress`를 사용한 send와 request뿐이다. 이 call이
§6의 target 선택과 cold activation을 내부에서 시작한다. Generic Spot resolver는 compatible `Ready` owner만
opaque handle로 반환하며 missing address를 활성화하지 않는다. Instance callback은 자신의 context로 local
close를 요청할 수 있지만 다른 node에서 Instance 생성을 시작할 수 없다.

## 4. Existing-owner resolve와 route cache

Resolver는 logical key와 type이 일치하고 owner lease가 유효한 `Ready` activation만 handle로 반환한다.
`Activating`, `Closing`, expired, missing과 type-conflict는 각각의 typed result로 끝난다. Resolve는 생성
operation이 아니며 owner가 없을 때 target을 선택하거나 factory를 실행하지 않는다.

Normal handle call은 cached current route를 사용하며 매 호출마다 Store를 읽지 않는다. Location event,
periodic refresh와 stale-target 결과는 다음 call을 위한 같은 activation route refresh를 시작할 수 있지만 실패한
operation을 다시 제출하지 않는다. Close 뒤 fresh resolve가 새 generation을 찾을 수는 있지만 기존 handle은
바뀌지 않는다.

## 5. Address send와 request

Handle direct call은 exact activation route를 사용한다. Instance address direct call은 compatible existing owner를
먼저 resolve하고, owner가 없으면 §6의 명시적 cold activation을 수행한다. 이 activation 의미는 address를 받는
call 자체의 public 계약이며 generic resolve와 handle call에 적용하지 않는다.

Address one-way call은 location I/O가 필요할 수 있으므로 async submit만 제공한다. Cache 상태에 따라 의미가
달라지는 동기 `TrySubmit` 계열은 제공하지 않는다. Submit은 address resolve, candidate selection과 source local
outbound admission까지만 기다린다. Target factory, activation queue, first message 수락과 handler 실행은 submit
completion 조건이 아니다. 이후 activation failure는 drop event와 metric에 기록하며 완료한 submit result를
바꾸지 않는다.

Address request timeout은 resolve, owner claim, activation, first message, handler와 reply 전체를 포함한다.
Shared activation을 기다리는 짧은 request가 timeout 또는 cancellation으로 끝나도 다른 send와 더 긴 request의
activation을 abort하지 않는다.

Address send와 request는 선택한 route에 한 번만 제출한다. Stale route, timeout, disconnect, claim conflict와
handler failure 뒤 같은 operation을 current owner나 다른 owner에 다시 제출하지 않는다. Route refresh는 다음
application call에만 적용한다.

Framework는 target mailbox receipt를 public completion 단계로 추가하지 않는다. Request는 internal operation ID와
request에만 존재하는 opaque reply route를 사용한다. Reply, timeout, cancellation, shutdown과 recovery가 경쟁해도
operation ID별 terminal result 하나만 application에 전달한다. Maintenance transfer가 accepted request를 보존하면
reply route와 늦게 완료된 terminal result도 durable journal에 보존한다. Recovery가 같은 completion을 다시 relay해도
application completion을 반복하지 않는다.

Cold activation request의 reply relay는 maintenance transfer identity를 사용하지 않는다. Ready barrier completion과
activation failure completion은 모두 claim CAS가 발급한 같은 non-zero ObjectGeneration,
AuthorityOwnerGeneration, OwnerId, OwnerLeaseGeneration과 StoreVersion을 포함한다. Ready completion은 `Ready`
authority에서만, failure completion은 `Activating` authority에서 `OK`가 아닌 terminal result로만 처리한다.
두 경우 모두 request reply route에만 적용하며 operation ID별 terminal owner를 한 번만 확정한다.

## 6. Cold activation

Instance address call이 compatible `Ready` owner를 찾지 못하면 다음 절차로 cold activation을 요청한다.

1. 같은 MeshName에서 `Serving` 상태이고 drain 중이 아니며 requested Instance type을 제공하는 target 후보를
   complete descriptor와 exact owner lease로 선택한다.
2. Source runtime이 outbound wire를 보내기 전 `Missing` expectation으로 `NewObject` claim CAS를 수행한다.
   CAS는 `Activating` row, non-zero ObjectGeneration·AuthorityOwnerGeneration과 current target owner lease fence를
   한 번에 확정한다.
3. Source는 그 exact authority fence를 activation request에 담아 target에 한 번 제출한다. Target은 owner를
   claim하지 않고 type capability, exact owner lease와 authority fence를 다시 읽어 검증한 뒤 activation barrier를
   만든다.
4. Factory, configure와 initialize를 실행한다.
5. Location을 `Ready`로 commit하고 barrier를 연 뒤 first message를 application queue에 한 번 제출한다.

Candidate selection은 Framework policy다. Caller는 target RID, endpoint와 factory class를 지정하지 않는다.
Concurrent cold call은 authority owner 하나와 factory execution 하나로 수렴한다. Compatible `Activating` authority를
발견한 caller는 같은 shared activation에 합류하며 새 target을 선택하지 않는다.

`ColdActivating` row 자체가 durable activation intent다. Exact target owner host는 `Serving` 전 initial authority
scan과 `Serving` 중 bounded background scan 또는 watch reconcile에서 자신이 소유한 row를 찾는다. Original source
message가 없어도 같은 local activation key로 factory와 Ready barrier를 idempotent하게 재개한다. Late wire submit과
scan recovery는 ObjectGeneration, AuthorityOwnerGeneration과 exact owner lease가 같은 local barrier 하나로
수렴한다. 따라서 factory와 initialize는 process restart 뒤 다시 호출돼도 안전해야 한다.

Owner claim CAS에서 패한 caller가 target transport 제출을 아직 시작하지 않았다면 winner의 compatible `Ready`
route를 최초 target으로 선택할 수 있다. Target transport 제출을 시작한 뒤에는 winner를 포함한 다른 owner로
재제출하지 않는다. `Closing` authority가 있으면 close completion 전 새 activation을 시작하지 않는다.

Expired authority takeover는 Store CAS와 lease-derived local monotonic deadline으로 이전 owner의 factory,
message, timer와 completion admission을 먼저 fence한다. Process가 일시 정지되어 callback을 실행하지 못해도
deadline을 넘긴 이전 owner는 재개 뒤 새 owner state를 변경할 수 없다.

Runtime은 Store request를 시작한 monotonic 시각에
`max(0, leaseExpiryStore - storeNow - fencingMargin)`을 더해 local deadline을 계산한다. `fencingMargin`은
clock·network 불확실성만큼 admission을 먼저 닫는 양수 설정값이다. Store 응답을 받은 시각을 기준으로 남은 lease를
다시 부여하지 않는다. Admission은 이 deadline과 read가 반환한 opaque Store version·authority revision을 함께
확인한다.

Factory, initialize 또는 Ready commit이 실패하면 target은 local activation barrier를 failed·sealed로 고정한다.
Queued/current request는 같은 typed failure로 terminal-once 완료하고 one-way call은 drop event와 metric만 남긴다.
Runtime은 StoreVersion, ObjectGeneration, AuthorityOwnerGeneration, OwnerId와 OwnerLeaseGeneration을 모두 확인하는
fenced delete를 시도한다. Timeout, cancellation 또는 response loss가 있으면 exact Read로 delete 결과를
reconcile한다.

Delete가 확정되기 전 registry는 failed 상태를 유지하며 이후 caller에게 같은 failure를 반환한다. 이 구간에는
factory나 original payload를 숨겨서 다시 실행하거나 다른 target에 제출하지 않는다. Store가 `Missing`임을 확인한
뒤에만 barrier, handler scope와 capacity slot을 정리한다. 그 다음 caller만 새 `NewObject` CAS로 새
ObjectGeneration과 AuthorityOwnerGeneration을 발급할 수 있다.

## 7. Close와 generation fence

Instance Spot close는 다음 순서를 지킨다.

1. Expected StoreVersion, current OwnerId·AuthorityOwnerGeneration·OwnerLeaseGeneration과 ObjectGeneration을
   확인해 authority를 `Closing`으로 compare-exchange한다.
2. Local application admission을 seal하고 seal 전에 수락한 turn과 timer를 defined boundary까지 처리한다.
3. Handler scope, timer, waiter와 local activation resource를 한 번 정리한다.
4. 같은 exact authority fence를 확인해 row를 explicit CAS delete한다.

Seal 전에 수락한 operation은 current generation에서 완료할 수 있다. Seal 뒤 operation은 closing, stale 또는
shutdown 결과로 끝나며 새 generation에 전달하지 않는다. Repeated close와 close·timeout·handler 경쟁은
terminal result와 cleanup 하나로 수렴한다.

Close된 handle은 stale 상태를 유지한다. 이후 application이 address cold activation 또는 host
maintenance transfer 뒤 fresh resolve를 수행하면 새 ObjectGeneration handle을 얻을 수 있지만 이전 handle은
자동으로 갱신되지 않는다.

## 8. Host Retire materialization

Existing Instance owner를 다른 MeshNode로 materialize하는 operation은 host `Retire` transaction만 시작한다.
Owner가 없는 address를 활성화하는 cold activation과 existing owner transfer는 서로 다른 authority operation
kind를 사용한다. Resolve와 handle call은 maintenance materialization을 사용하지 않는다.

Factory registration의 transfer policy가 target materialization을 정한다.

| Policy | `Retire` 동작 |
|---|---|
| `Disabled` | Instance가 남아 있으면 preflight를 `Blocked/TransferDisabled`로 끝내고 source owner와 admission을 유지함 |
| `Recreate` | Target factory가 같은 logical address로 instance를 만든다. Framework는 Snapshot state를 만들거나 restore하지 않으며, factory가 필요하면 application 외부 durable state로 복구한다. Seal 전 accepted journal만 Framework checkpoint envelope로 보존할 수 있음 |
| `Snapshot` | Typed adapter로 application state를 capture·restore하고 Framework가 checkpoint와 serialization을 처리함 |

Source seal 전에 수락한 journal은 durable checkpoint에 포함한다. Owner commit 뒤 target factory와 restore를
실행하고 journal replay가 끝나기 전 application admission과 ready route를 공개하지 않는다. Commit 전 failure는
source owner를 유지하고 commit 뒤 failure는 target 또는 recovery successor activation으로 수렴한다.

Application close와 maintenance transfer는 같은 authority CAS에서 순서를 정한다. `Closing`이 먼저 commit되면
`Retire`는 close completion을 기다리고 transfer하지 않는다. Transfer `Prepared`가 먼저 commit되면 늦은 close는
`TargetMoving`으로 끝나며 Framework가 새 owner에 close를 자동 재제출하지 않는다.

## 9. 실패, ownership과 검증

Location owner lease와 Store-issued authority가 유효한 runtime만 factory completion, message, timer, close와 row
update를 적용할 수 있다. Application wall clock과 service liveness를 owner authority로 사용하지 않는다.

다음 조건을 검증한다.

- Instance Spot 전용 public `Create`·`GetOrCreate` 표면이 없고 User Spot manager가 Instance lifecycle type을
  받지 않는다.
- Resolver와 handle call이 missing owner에서 factory를 실행하지 않는다.
- Address cold activation만 missing logical address의 target을 선택하고 owner·factory 하나로 수렴한다.
- First message가 initialize와 `Ready` barrier 뒤 일반 handler에 한 번 전달된다.
- Target owner의 initial·background authority scan이 source 재전송 없이 owned `ColdActivating` row를 재개한다.
- Activation failure는 request terminal-once와 one-way drop event로 나뉘며, exact fenced delete가 `Missing`으로
  확인되기 전 factory와 payload를 다시 실행하지 않는다.
- Address one-way submit이 target factory와 handler 완료를 기다리지 않는다.
- Request timeout과 cancellation이 shared activation을 임의로 abort하지 않는다.
- Close seal 전 accepted work와 seal 뒤 rejected work의 boundary가 유지된다.
- Close된 handle이 새 ObjectGeneration으로 자동 retarget되지 않는다.
- `Retire` materialization이 existing-only resolve와 address cold activation의 의미를 바꾸지 않는다.
- Process pause, stale owner와 이전 transfer가 새 owner의 message, timer와 location을 변경하지 못한다.
