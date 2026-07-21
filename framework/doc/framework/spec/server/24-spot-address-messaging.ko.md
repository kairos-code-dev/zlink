# Spot 주소 메시징 — 공통 스펙

[스펙 목차](../README.ko.md) · [SPOT 메시징](20-spot-messaging.ko.md) ·
[MeshNode](21-mesh-node.ko.md) · [Spot과 Actor membership](23-spot-actor.ko.md) ·
[Location runtime](40-location-runtime.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 논리 Spot identity를 resolve하고 직접 호출하는 공개 계약을 정의한다.
User Spot의 local 생성, existing-owner resolve·messaging, `InstanceSpotAddress`의 명시적 cold activation과 host
maintenance materialization을 서로 다른 operation으로 구분한다.

Core는 raw socket과 transport만 제공하며 Spot kind·type, logical address, owner claim, generation, activation과
maintenance authority를 해석하지 않는다. Target 선택, Store CAS와 activation barrier는 Framework가 소유한다.

Location row, lease와 resolver backend는 [40 Location runtime](40-location-runtime.ko.md), Spot handler와 실행
순서는 [20 SPOT 메시징](20-spot-messaging.ko.md), MeshName 경계는 [21 MeshNode](21-mesh-node.ko.md)가 소유한다.

## 2. SpotHandle과 InstanceSpotAddress

`SpotHandle`은 하나의 `(MeshName, Spot RID, Spot generation)` activation과 current owner route snapshot을 보유하는
불투명한 capability다. Application은 owner RID, endpoint, lease와 transport route를 조립하지 않는다.

- Handle의 MeshName과 Spot generation은 바뀌지 않는다.
- 같은 activation의 owner route가 이동하면 location event 또는 resolver refresh로 snapshot을 교체할 수 있다.
- Spot이 close된 뒤 다른 generation으로 만들어져도 이전 handle을 새 activation으로 자동 retarget하지 않는다.
- Handle은 동시 호출에 안전하며 background listener 수명 관리를 호출자에게 요구하지 않는다.

`InstanceSpotAddress`는 runtime instance 존재 여부와 관계없이 만들 수 있는 immutable logical address다. 주소는
`MeshName`, `InstanceSpotType`, `SpotRid`를 가지며 node RID, endpoint, Spot generation, transfer identity와 owner
token을 포함하지 않는다. Location uniqueness는 `(MeshName, SpotRid)`가 소유하므로 같은 key를 서로 다른 Spot
kind나 Instance type이 동시에 사용할 수 없다.

`InstanceSpotAddress`를 받는 send·request는 existing owner가 없을 때 해당 logical address의 activation을
명시적으로 요청하는 공개 operation이다. 이 의미는 일반 `SpotHandle` resolve·send나 User Spot의 local
`Create`·`GetOrCreate`에 적용되지 않는다.

Instance Spot은 Actor membership이 없는 Spot이다. Direct packet handler, timer와 outbound call은 사용할 수 있지만
Actor create·join·leave·transfer와 Logical Multicast subscription은 사용할 수 없다.

## 3. User Spot 생성과 Instance activation 경계

Spot manager의 `Create`·`GetOrCreate`는 Entry·User Spot을 caller가 선택한 local MeshNode에 만들 때 사용한다.
다른 MeshNode를 선택하거나 remote create request를 전달하지 않는다. 자세한 생성 순서와 동시 호출 계약은
[20 SPOT 메시징](20-spot-messaging.ko.md)이 정한다.

Instance Spot은 application이 target MeshNode를 선택하는 별도 `Create`·`GetOrCreate` operation을 제공하지
않는다. Instance type과 factory는 startup 전에 등록하고, missing logical address의 activation은
`InstanceSpotAddress` send·request가 시작한다. Target 선택, owner claim과 factory 실행은 §5.1의 cold activation
transaction 안에서 Framework가 처리한다.

## 4. Existing-owner resolve

Spot resolver는 MeshName과 Spot RID로 이미 `Ready`인 owner의 `SpotHandle`을 반환한다. `InstanceSpotAddress` resolve도
같은 logical key와 type의 유효한 `Ready` owner만 찾는다. Owner가 없거나 `Activating`·`Closing`·expired 상태이면
not-found 또는 해당 typed 상태 오류로 끝난다.

Resolve는 생성 operation이 아니다. 유효한 row가 없을 때 Framework는 serving MeshNode를 선택하거나 factory를
실행하지 않는다. Address resolve가 이전 close 뒤 새 generation을 발견하면 fresh
`SpotHandle`을 반환할 수 있지만 이전 generation의 handle을 변경하지 않는다. `InstanceSpotAddress` send·request의
cold activation은 resolve와 구분한 §5.1의 operation이다.

정상 send·request는 handle의 in-memory route snapshot을 사용하며 매 호출마다 Store를 읽지 않는다. Location
event, runtime refresh와 명확한 stale-target 응답만 같은 activation의 route snapshot 갱신을 시작할 수 있다.

## 5. Direct send와 request

Spot direct 호출은 `SpotHandle` 또는 `InstanceSpotAddress`와 typed payload를 받는다.

- `SpotHandle` 호출은 handle의 exact Spot generation과 current owner route로 제출한다.
- `InstanceSpotAddress` 호출은 existing-owner resolve 뒤 얻은 exact generation·route로 제출하고, owner가 없으면
  §5.1의 명시적 cold activation을 수행한다.
- Local과 remote owner는 같은 handler, metadata와 completion 의미를 가진다.
- Handle·address의 MeshName과 local source MeshNode의 MeshName이 다르면 제출 전에 실패한다.
- 서로 다른 MeshName 사이에 relay, fallback과 target 변경을 수행하지 않는다.
- 일반 `SpotHandle` send·request와 resolver는 owner가 없을 때 Instance Spot을 만들지 않는다.

Address one-way call은 location I/O와 cold activation이 필요할 수 있으므로 비동기 submit만 제공한다. 동기
`TrySubmit`이나 cache 상태에 따라 의미가 달라지는 호출은 제공하지 않는다. Existing owner 호출은 outbound
admission까지만 기다린다. Cold one-way 호출은 address resolve, target 선택과 source outbound admission까지만
기다리며 target factory·queue 수락과 handler 실행 완료를 기다리지 않는다. 이후 activation 실패는 message-flow
관측에 남긴다.

Address send와 request는 선택한 route에 한 번만 제출한다. Stale-target, timeout, cancellation, disconnect,
claim conflict와 handler failure 뒤 같은 operation을 current owner나 다른 owner에 다시 제출하지 않는다. Route
refresh는 다음 application call에만 적용한다.

Address 호출도 target queue admission 뒤 다른 owner에게 숨은 retry를 수행하지 않는다. Resolve 뒤 owner가
바뀌거나 close가 시작되면 typed stale·closing·not-found 결과로 끝내고 application이 새 address call 또는
업무 retry를 결정한다.

### 5.1 InstanceSpotAddress cold activation

`InstanceSpotAddress` send·request가 compatible `Ready` owner를 찾지 못하면 다음 절차로 logical address의
activation을 명시적으로 요청한다.

1. 같은 MeshName에서 serving 상태이고 drain 중이 아니며 해당 stable Instance type factory를 등록한 MeshNode를
   후보로 선택한다.
2. Source Framework가 outbound wire보다 먼저 Location Store의 `NewObject` CAS를 수행해 owner 하나를 확정한다.
3. Target Framework는 owner를 claim하지 않고 type·owner authority를 검증한 뒤 activation barrier와 factory를
   실행한다.
4. Location을 `Ready`로 commit하고 barrier를 연 뒤 첫 message를 일반 Spot application queue에 한 번 제출한다.

후보 선택은 Framework 내부 정책이며 caller가 target node나 factory class를 지정하지 않는다. 같은 address에
동시에 도착한 cold 호출은 owner와 factory execution 하나로 수렴한다. Owner claim CAS에서 패한 caller가 target
transport 제출을 시작하지 않았다면 winner의 compatible `Ready` route를 최초 target으로 선택할 수 있다. Target
transport 제출을 시작한 뒤에는 다른 owner로 재제출하지 않는다.

Cold activation은 `InstanceSpotAddress`가 공개적으로 표현하는 operation이며 일반 resolve·SpotHandle 호출에
숨겨서 적용하지 않는다. Application에 remote creation target을 고르는 별도 표면을 제공하지 않는다.
Compatible `Activating` authority가 있으면 같은 activation의 terminal 결과에 합류하며 새 target을 선택하지
않는다. `Closing` authority가 있으면 closing 결과로 끝나고 row release 전에 새 activation을 시작하지 않는다.
Expired authority를 교체할 때는 Store CAS와 owner fencing으로 이전 activation의 factory·message·timer admission을
먼저 차단한다.

## 6. Close와 generation 경계

Instance Spot close는 다음 순서를 지킨다.

1. Expected owner와 Spot generation을 검증해 location row를 `Closing`으로 CAS 전이한다.
2. Local admission을 seal하고 seal 전에 수락한 turn·timer를 정의된 boundary까지 처리한다.
3. Handler scope, timer와 local activation resource를 한 번 정리한다.
4. 같은 owner·generation authority를 검증해 location row를 release한다.

Close와 send·request가 경쟁할 때 seal 전에 accepted된 operation은 기존 generation에서 완료할 수 있다. Seal 뒤
operation은 closing 또는 stale 결과로 끝나며 새 generation으로 전달되지 않는다. Close의 terminal result와
resource cleanup은 반복 호출·callback·timeout 경쟁에서도 한 번만 완료된다.

Close된 generation의 `SpotHandle`은 계속 stale이며 자동 재생성·refresh 대상이 아니다. `InstanceSpotAddress`는
논리 주소이므로 이후 address cold activation을 명시적으로 요청하거나 §7의 maintenance materialization이
commit되면 fresh resolve로 새 generation의 handle을 얻을 수 있다. 이전 handle은 이 새 generation으로 바뀌지
않는다.

## 7. Host Retire의 maintenance materialization

이미 존재하는 Instance Spot의 owner를 다른 MeshNode로 옮겨 materialize하는 동작은 명시적으로 시작한 host
`Retire` transaction 안에서만 허용한다. Owner가 없는 logical address를 처음 활성화하는 §5.1 cold activation과
existing owner를 transfer하는 maintenance materialization은 서로 다른 authority·operation kind를 사용한다.
일반 resolve와 address cold activation은 maintenance 경로를 사용하지 않는다.

Retire authority는 source owner·generation, transfer identity, target, phase, deadline과 recovery lease를 durable
record로 관리한다. Typed Instance factory에 등록한 maintenance policy가 target materialization 방식을 정한다.

| Policy | Retire 동작 |
|---|---|
| `Disabled` | Instance가 남아 있으면 preflight를 `Blocked`로 끝내고 source admission과 owner를 변경하지 않는다. |
| `Recreate` | Target의 typed factory로 새 runtime instance를 만들며 application state checkpoint를 전달하지 않는다. |
| `Snapshot` | Typed state adapter로 application state를 capture·restore한다. |

Source admission seal, accepted journal, Location authority commit, target restore·journal replay, activation barrier와
route publication 순서를 지킨다. Commit 전 failure는 source owner를 유지하고, commit 뒤 failure는 target
activation recovery로 수렴한다. Stale source와 이전 transfer는 새 generation에 message, timer, phase update와
cleanup을 적용할 수 없다.

Maintenance materialization은 address request 실패나 cold activation failure를 처리하는 retry가 아니다.
Original send·request를 maintenance target에 자동 재제출하지 않는다.

Application `Close`와 host `Retire`가 경쟁하면 같은 authority CAS가 순서를 정한다. `Closing`이 먼저 commit되면
Retire는 close completion을 기다리고 그 Instance를 transfer하지 않는다. Transfer commit이 먼저 완료되면 늦은
Close는 moving 또는 stale 결과로 끝나며 Framework가 새 owner에 Close를 자동 재제출하지 않는다.

## 8. 실패와 관측

- Local process에 MeshName과 일치하는 source MeshNode가 없으면 configuration 오류다.
- Owner RID, Spot generation, kind 또는 Instance type이 맞지 않으면 typed stale·conflict 오류다.
- 알려진 owner route가 deadline 안에 ready가 되지 않으면 route-not-connected 또는 timeout으로 끝난다.
- `Closing` 또는 draining owner는 신규 admission을 거부한다.
- Request 실패를 다른 Spot RID, MeshName이나 owner로 우회하지 않는다.
- Expired owner는 신규 message·timer admission과 location update를 수행할 수 없다.

관측 정보는 MeshName, logical address, Spot generation, resolve 결과, route refresh, activation·close·maintenance
operation kind, submit 결과와 stale 분류를 구분한다. Spot RID와 Instance key는 metric label로 사용하지 않는다.

## 9. 검증 요구

- User Spot `Create`·`GetOrCreate`가 선택한 local MeshNode에만 Spot을 만들고 remote owner를 자동 생성·반환하지
  않는다.
- Instance Spot에는 application이 target MeshNode를 선택하는 별도 `Create`·`GetOrCreate` operation이 없다.
- Resolver와 `SpotHandle` send·request가 existing `Ready` owner만 사용하고 owner가 없으면 factory를 실행하지 않는다.
- `InstanceSpotAddress` cold activation만 owner가 없는 logical address의 serving target을 선택하고 factory를 실행한다.
- 동시 address cold activation이 owner 하나, factory 실행 하나와 barrier 뒤 첫 message 한 번으로 수렴한다.
- 정상 handle 호출이 Store를 매번 조회하지 않는다.
- One-way와 실행 여부가 불명확한 request를 자동 재제출하지 않는다.
- Stale route refresh는 다음 application call에만 적용되고 실패한 send·request를 다시 제출하지 않는다.
- Close seal 전 accepted operation과 seal 뒤 거부 operation의 경계가 유지된다.
- Close와 Retire 경쟁이 authority CAS 하나로 순서를 정하고 loser operation을 새 owner에 자동 재제출하지 않는다.
- Close된 generation의 handle은 stale이며 새 generation으로 자동 retarget되지 않는다.
- Address는 cold activation 또는 maintenance commit 뒤 fresh resolve로 새 generation을 찾을 수 있다.
- Existing Instance owner의 remote maintenance materialization은 host `Retire` authority에서만 시작된다.
- `Disabled`, `Recreate`, `Snapshot`이 typed factory 계약과 일치하고 stale transfer가 새 owner를 변경하지 못한다.
- C++, .NET, JVM과 Node.js가 address-only cold activation, existing-only resolve, close race와 maintenance recovery에서 같은
  terminal 결과를 제공한다.
