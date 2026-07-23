# Spot 주소 메시징 — 공통 스펙

[스펙 목차](../README.ko.md) · [SPOT 메시징](20-spot-messaging.ko.md) ·
[MeshNode](21-mesh-node.ko.md) · [Spot과 Actor membership](23-spot-actor.ko.md) ·
[Location runtime](40-location-runtime.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 global SpotRid identity를 생성·조회하고 직접 호출하는 공개 계약을
정의한다. User Spot과 Instance Spot은 같은 logical address와 placement lifecycle을 사용하지만 Actor
membership 지원 여부는 다르다.

Core는 raw socket과 transport만 제공하며 Spot kind·type, logical address, owner claim, generation,
activation과 maintenance authority를 해석하지 않는다. Target 선택, Store transaction, route cache와
activation barrier는 Framework가 소유한다.

## 2. Spot identity와 reference

User·Instance Spot의 logical key는 Location Store namespace 전체에서 전역인 Spot RID다. RID는 RoutingId의
1..255-byte exact value다. MeshName은 최초 placement attribute이며 identity key가 아니다. 같은 RID를
서로 다른 MeshName, Spot kind 또는 stable type에 동시에 사용할 수 없다.

User·Instance Spot type은 UTF-8 1..255 bytes의 case-sensitive stable name이다. Framework는 normalization이나
case folding을 적용하지 않으며 언어 class FQN을 Store 또는 wire identity로 사용하지 않는다. 같은 Object
Server에 같은 stable type을 중복 등록하면 startup 오류다. Entry Spot RID는 Framework가 발급하며 caller가
create 대상으로 지정하지 않는다.

`SpotRef`는 다음 값을 담는 immutable location snapshot이다.

- global `SpotRid`
- non-zero unsigned 63-bit `ObjectGeneration`
- 조회 시점의 `MeshName`과 `NodeRid`

ObjectGeneration은 JSON에서 decimal string으로 표현한다. `SpotRef`는 messaging target이나 owner
capability가 아니며 owner가 이동하면 위치 field가 stale할 수 있다. Application이 현재 위치를 관측하려면
Spot RID로 다시 조회한다. `SpotHandle`, 별도 resolver handle과 `InstanceSpotAddress`는 제공하지 않는다.

Instance Spot은 Actor membership이 없는 Spot이다. Direct packet handler, timer와 outbound call은 사용할 수
있지만 Actor create·join·leave·relocation과 Logical Multicast subscription은 사용할 수 없다.

## 3. User Spot Create와 GetOrCreate

Spot manager의 `Create`와 `GetOrCreate`는 User Spot만 명시적으로 생성한다. `Create`는 Framework가 global
RID를 생성하고 required stable type을 받는다. `GetOrCreate`는 caller가 지정한 global RID와 stable type을
받는다. Instance Spot kind를 받는 manager overload와 Instance Spot 전용 create operation은 제공하지 않는다.
두 operation은 target node나 endpoint를 받지 않는 single-use fluent call이다.

`InMesh`, encoded creation request, `PlacementProfile`, `AffinityKey`와 timeout은 선택 항목이다.
`PlacementProfile`과 `AffinityKey`는 UTF-8 1..255 bytes의 stable value이며 caller callback, target RID와
predicate를 받지 않는다. 같은 option을 두 번 설정하면 `InvalidConfiguration`, terminal submit을 두 번
실행하면 `AlreadySubmitted`다. Terminal submit을 시작할 때 resolve, reservation, factory와 Ready barrier
전체에 적용할 end-to-end deadline 하나를 고정한다.

`InMesh`를 지정하면 해당 Mesh를 사용한다. 생략했을 때 object Client 또는 Server role의 Mesh가 하나면
자동 선택한다. 후보가 0개이면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`, 명시한
Mesh가 없으면 `MeshNotFound`로 끝난다. Framework는 role, stable type capability, placement profile,
active·pending capacity를 먼저 검사하고 남은 후보를 node-wide placement weight로 선택한다.

Encoded creation request는 최대 1 MiB다. Reservation 전에 immutable content reference와 hash를 creation
intent에 기록하며 Ready 또는 fenced failure cleanup까지 유지한다. Authority CAS winner만 request를 factory에
전달한다. Factory는 `(SpotRid, ObjectGeneration, creation attempt)` 기준 at-least-once로 실행될 수 있으므로
retry-safe해야 한다.

`Create`의 automatic RID가 active authority와 충돌하면 public 결과를 만들기 전에 새 RID로 다시 시도한다.
같은 caller RID의 kind 또는 stable type이 다르면 `SpotTypeMismatch`다. `GetOrCreate`는 같은 User Spot
type의 Ready 또는 Creating attempt에
합류하고 같은 incarnation의 `SpotRef`를 반환한다. CAS loser는 다른 target에서 factory를 시작하지 않는다.
Deadline까지 같은 attempt가 terminal state가 되지 않으면 `DeadlineExceeded`로 끝나며 다음 call이 exact
authority를 reconcile한다.

Terminal result는 해당 attempt의 `SpotRef`, `Existing`·`Created`·`Rejected` state와 optional creation reply를
함께 반환한다. `Existing`은 같은 stable type의 Ready incarnation을 사용했으며 factory callback을 실행하지
않았다는 뜻이다. `Created`는 새 incarnation이 Ready로 commit됐다는 뜻이고, `Rejected`는 application create
callback이 거부해 reservation과 authority를 정리했다는 뜻이다. `Rejected`의 `SpotRef`는 거부된 attempt를
식별하며 current Ready location을 보장하지 않는다. Reply는 create callback이 반환한 opaque framework message이고
`Existing`에서는 비어 있다.

Manager `Find(SpotRid)`는 current Ready authority의 `SpotRef`를 반환하며 creation을 시작하지 않는다. Manager가
제공하는 current Spot query와 page size 1..1000, encoded 4 MiB 이하의 operational query 외에 unbounded list와
별도 resolver는 제공하지 않는다.

## 4. Instance Spot fluent activation intent

Spot direct call은 기본적으로 existing-only operation이다. Call builder에서 Instance Spot을 명시하지 않은
send·request는 Missing RID의 factory를 실행하거나 creation intent를 만들지 않는다.

Instance Spot cold activation을 허용하려면 같은 Spot direct call builder에서 Instance intent를 명시한다.
Builder는 stable type을 생략하는 형식과 명시하는 형식을 제공한다. `InMesh`, `PlacementProfile`과 `AffinityKey`는
Instance intent를 명시한 call에서만 사용할 수 있으며 Missing object의 최초 placement에만 적용한다. Existing
Ready owner를 다른 Mesh로 옮기거나 현재 placement를 제한하는 option으로 해석하지 않는다.

Terminal call은 별도 check와 send로 나누지 않고 다음 순서로 resolve와 activation을 수행한다.

1. global Spot RID의 current authority를 조회한다.
2. Ready authority가 있으면 저장된 kind와 stable type을 사용해 current owner로 전송한다.
3. authority가 Missing이고 Instance intent가 없으면 target-not-found로 끝낸다.
4. authority가 Missing이고 Instance intent가 있으면 eligible Object Mesh를 선택한다. `InMesh`를 생략했고 후보가
   0개이면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`다.
5. stable type을 명시하면 해당 capability를 가진 serving node만 후보로 사용한다.
6. stable type을 생략하면 선택한 Mesh의 serving descriptor에 등록된 distinct Instance type을 계산한다. 하나면
   자동 선택하고, 0개이면 target-not-found, 둘 이상이면 required type을 생략한 `InvalidConfiguration`이다.
7. Location Store의 generic placement reservation이 authority의 `Creating` 상태와 target pending capacity를 한
   transaction으로 확보한다. Factory와 initialize가 성공하면 같은 reservation을 `Ready`와 active capacity로
   commit하고, 실패하면 abort한다. Ready commit 뒤 최초 message를 application queue에 정확히 한 번 제출한다.

여러 MeshNode가 같은 stable Instance type을 등록한 경우 distinct type 하나이며 placement 후보가 여러 개인
것으로 처리한다. Concurrent cold call은 같은 reservation과 factory execution 하나에 합류하며 CAS loser가 별도
factory를 실행하지 않는다. 기존 authority가
User Spot이거나 builder에 명시한 stable type과 다르면 `SpotTypeMismatch`다. 기존 Instance Spot에 type을 명시하지
않은 일반 direct call은 authority에 저장된 type을 사용하므로 등록 type 수와 관계없이 전송할 수 있다.

## 5. Existing-owner resolve와 direct call

Spot direct send/request의 시작 method는 global Spot RID와 typed payload만 받는다. Framework는 positive route cache 또는
Location Store에서 current Ready incarnation과 owner route를 resolve하고 selected ObjectGeneration을 wire
admission에 고정한다. Local과 remote owner는 같은 handler, metadata와 completion 의미를 가진다.

- Missing, Creating과 Store failure는 negative cache에 저장하지 않는다.
- Positive Ready cache는 current owner lease의 local admission deadline과 `RouteCacheMaxAge` 안에서만
  사용한다.
- Higher StoreVersion, stale result 또는 Store recovery event를 확인하면 즉시 invalidate한다.
- Resolve 뒤 close와 recreate가 발생해도 이전 generation operation을 새 generation으로 retarget하지 않는다.
- Timeout, cancellation, disconnect와 실행 여부가 불명확한 failure 뒤 다른 owner에게 자동 재제출하지 않는다.

One-way call은 local outbound admission까지만 기다린다. Cold activation이 필요해도 application handler 실행은
기다리지 않는다. Request는 resolve, cold activation, 최초 message dispatch와 reply를 하나의 deadline 안에서
terminal-once로 완료한다. Target queue admission 뒤의
failure를 current owner를 다시 찾아 hidden retry하지 않는다.

## 6. Route cache와 stale-route forwarding

`RouteCacheMaxAge`의 기본값은 15초이고 `RelocationForwardingWindow`의 기본값은 30초다. 둘 다 0이면 각각
cache와 forwarding을 끈다. 두 값이 양수이면 cache max age가 forwarding window보다 최소 5초 작아야 한다.
Runtime 변경은 새 cache entry와 새 relocation에만 적용한다.

Relocation commit 뒤 source는 committed source→target mapping만 사용해 stale physical route를 relay한다. Relay
중 Store를 읽거나 application handler를 실행하지 않는다. Mapping은 Spot RID, ObjectGeneration, source와
target AuthorityOwnerGeneration과 owner fence를 exact 검증한다. Target owner generation은 hop마다 증가하며
최대 8 hops다.

Mapping 하나의 대기열은 1024 messages와 16 MiB 이하이며 negotiated message bound도 지킨다. Relay는 original
operation ID, generation, payload와 reply route를 보존한다. Mapping 없음·만료, generation mismatch, loop 또는
bound 초과는 stale-route 오류로 끝난다. Failed application operation을 Store에서 찾은 owner에게 다시
제출하지 않으며 다음 call만 fresh resolve를 수행한다.

User Spot aggregate relocation은 Spot과 member Actor의 mapping을 같은 aggregate commit에서 설치한다. 개별
participant mapping을 commit 전에 current route로 공개하지 않는다.

Relocation unit을 seal한 뒤 source route로 도착한 ingress는 bounded hold에 넣고 application handler를 실행하지
않는다. Owner commit 전 abort에서는 source queue에 arrival order로 복원하고, commit 뒤에는 stale-route mapping과
같은 operation ID·generation·reply route를 유지해 target으로 relay한다. Permit을 기다리는 `Retiring` unit은 seal된
상태가 아니므로 기존 owner route에서 application message와 timer를 계속 수락한다.

## 7. Close와 generation 경계

Spot manager의 public `Close`는 User Spot의 exact `SpotRef`를 받는다. Instance Spot은 application handler나
timer가 자신의 lifecycle context에서 local `Close`를 요청한다. Host shutdown과 `Retire`는 별도 운영
lifecycle로 Instance Spot을 정리하거나 이동할 수 있다.

1. Expected owner와 ObjectGeneration을 검증해 authority를 `Closing`으로 전이한다.
2. Local admission을 seal하고 seal 전에 수락한 turn·timer를 정해진 boundary까지 처리한다.
3. Handler scope, timer와 local activation resource를 한 번 정리한다.
4. 같은 owner·generation fence로 authority를 release한다.

같은 incarnation이 이미 없으면 idempotent `false`, 같은 RID의 다른 generation이 있으면
`SpotGenerationStale`, 이동 seal 중이면 `SpotMoving`으로 끝난다. Framework는 current ref를 다시 찾아 새
incarnation을 닫지 않는다. Seal 전에 accepted된 operation은 기존 generation에서 완료할 수 있지만 seal 뒤
operation은 closing 또는 stale 결과로 끝난다.

User Spot에 current Actor membership이 하나라도 있으면 Close는 `false`로 끝나며 admission과 authority를
유지한다. Framework는 member Actor를 숨겨서 이동하거나 destroy하지 않는다.

## 8. Maintenance materialization

이미 존재하는 Spot owner의 이동은 명시적인 host `Retire` transaction만 시작한다. Object Server factory는
`Disabled`, `Recreate`, `Snapshot` 중 하나의 policy를 반드시 등록한다. 생략 overload와 compatibility
default는 제공하지 않는다. Snapshot은 Spot type에 맞는 `SpotRelocationAdapter`를 요구한다. Adapter는
application이 형식과 version을 관리하는 opaque byte sequence를 capture·restore한다.

Source seal, durable capture, target reservation·factory·restore, authority commit과 admission 순서는
[23 Spot과 Actor membership](23-spot-actor.ko.md)이 정한다. Commit 전 failure는 source를 유지하고 commit 뒤에는
target recovery만 계속한다. Seal 시점의 실행하지 않은 message, accepted journal과 timer logical
registration·pending tick은 relocation payload에 포함하며 target Framework가 timer를 자동 복원한다. Application은
`Restore`에서 Framework timer를 다시 등록하지 않는다. Original send·request를 maintenance target에 자동 재제출하지
않지만 seal 뒤 source ingress hold는 commit된 mapping으로 relay한다.

## 9. 실패와 관측

- Object role Mesh 후보가 없거나 여러 개인 create와 cold activation은 §3·§4의 typed error로 끝난다.
- Ready authority가 없으면 not-found, exact generation이나 owner fence가 다르면 typed stale error다.
- `Closing`, relocation seal 이후 또는 `Draining` owner는 신규 admission을 거부한다. `Retiring`이지만 아직 seal하지
  않은 unit은 기존 owner admission을 유지한다.
- Request failure를 다른 Spot RID, MeshName이나 owner로 우회하지 않는다.
- Expired owner는 신규 message·timer admission과 location update를 수행할 수 없다.

관측 정보는 global logical RID, current MeshName, ObjectGeneration, resolve·cache 결과, creation attempt,
cold activation·close·maintenance operation kind, forwarding hop·drop과 stale 분류를 구분한다. Spot RID는 metric
label로 사용하지 않는다.

## 10. 검증 요구

- Spot RID가 Store namespace 전체의 global key이고 MeshName별 중복을 허용하지 않는다.
- User Spot Create·GetOrCreate가 target RID와 endpoint를 application에 요구하지 않는다.
- Spot manager가 Instance Spot create·get-or-create를 제공하지 않는다.
- Concurrent create가 authority attempt와 factory execution 하나로 수렴한다.
- `SpotRef`가 public exact generation을 보존하되 messaging target으로 사용되지 않는다.
- Spot direct 시작 method가 Spot RID만 받고 owner route를 요구하지 않는다.
- Instance intent가 없는 Missing Spot message가 creation intent를 만들지 않는다.
- Instance intent가 Missing Spot에서만 optional initial Mesh와 stable type을 사용해 cold activation을 시작한다.
- 선택한 Mesh의 distinct Instance type이 하나면 type을 자동 선택하고 여러 개면 type 명시를 요구한다.
- Cold activation이 owner·factory 하나와 Ready barrier 뒤 최초 message 한 번으로 수렴한다.
- Missing, Creating과 Store failure를 negative cache하지 않는다.
- Forwarding이 committed mapping과 bounded queue만 사용하고 operation identity를 보존한다.
- Close가 exact generation을 검사하고 새 incarnation으로 retarget하지 않는다.
- User Spot Close가 active membership을 숨겨서 정리하지 않는다.
- C++, .NET, JVM과 Node.js가 create 경쟁, logical messaging, cold activation, close와 stale forwarding에서 같은
  terminal 결과를 제공한다.
