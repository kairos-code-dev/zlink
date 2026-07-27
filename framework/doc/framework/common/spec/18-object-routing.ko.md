# Spot·Actor routing

[스펙 목차](README.ko.md) · [이전: Stage wrapper on Spot](17-stage-wrapper-on-spot.ko.md) · [다음: STREAM 서버 session](19-stream-session.ko.md)


## 1. 어떤 message가 어느 route를 사용하는가

Spot·Actor에 message를 보내는 모든 경로가 Location Store를 조회하는 것은 아니다.
Framework는 message가 시작된 방법에 따라 route를 다음과 같이 정한다.

```text
+----------------------------------------------------------------------+
| Route source by message path                                         |
|                                                                      |
| Spot / Actor direct : Ready cache -> Store on cache miss             |
| Session -> Actor    : Stored binding route                           |
| Request reply       : Preserved reply route + correlation            |
|                                                                      |
| Only direct resolution reads the Location Store.                     |
+----------------------------------------------------------------------+
```

위 그림의 세 경로는 다음처럼 동작한다.

- Application이 global Spot ID나 Actor ID를 지정하면 Framework가 current owner를
  찾는다. 최근에 확인한 Ready route를 사용할 수 없을 때만 Location Store를
  조회한다.
- Session에 bind된 Actor로 relay할 때는 bind가 성공하면서 Session owner에 저장한
  route를 사용한다. Message마다 Actor 위치를 다시 조회하지 않는다.
- Request의 reply는 request에 포함된 반환 경로와 correlation을 사용한다. Reply를
  보내려고 requester의 Spot·Actor 위치를 조회하지 않는다.

이 문서는 위 세 경로가 route를 얻고 검증하며 위치 변경에 대응하는 방법을 한곳에서
정의한다. Node direct, Channel select-one과 Logical Multicast의 target 선택은
다루지 않는다. Object create·get-or-create, exact `ActorRef`·`SpotRef`를 사용하는
close·destroy와 membership transaction도 각 lifecycle 문서가 정의한다.

## 2. Global ID로 Spot·Actor에 보내는 방법

### 2.1 Current owner를 찾는 순서

Spot direct call은 global Spot ID를 받고 Actor direct call은 global Actor ID를
받는다. Source runtime은 ID를 실제 owner route로 바꾼 뒤 message를 제출한다.

```text
+----------------------------------------------------------------------+
| Direct resolution                                                    |
|                                                                      |
| Global SpotId or ActorId                                             |
|            |                                                         |
|            v                                                         |
| [Positive Ready cache] -- miss --> [Location Store]                  |
|            | hit                         | Ready authority           |
|            +-----------------------------+                           |
|                          |                                           |
|                          v                                           |
|             [Owner route + generation fences]                        |
+----------------------------------------------------------------------+
```

Source runtime은 다음 순서로 처리한다.

1. Global Spot ID 또는 Actor ID로 최근에 확인한 Ready owner route를 찾는다.
2. 사용할 수 있는 최근 route가 없으면 Location Store에서 current object 상태를
   조회한다.
3. Object가 Ready이면 owner의 `MeshName`과 `NodeRid`, object generation과 owner
   fence를 이번 operation에 고정한다.
4. 선택한 owner route로 message를 제출한다.
5. Target은 generation, owner fence와 local admission 상태를 확인한 뒤
   application queue에 넣는다.

Location Store가 global object마다 기록한 current owner, incarnation, owner
generation과 lease 정보를 authority라 한다. Framework는 current Ready
[authority](01-glossary.ko.md#authority)만 application message의 route로 사용한다.

Spot ID나 Actor ID 문자열에는 owner 주소가 들어 있지 않다. Framework는 ID를
parse하여 node를 추론하거나 Core routing ID로 변환하지 않는다. Caller도 다음
값을 message target으로 지정하지 않는다.

- `MeshName`
- Owner `NodeRid`
- `ActorRef` 또는 `SpotRef`
- Actor의 current Spot ID

### 2.2 최근 Ready route를 사용하는 조건

Source runtime은 Location Store에서 확인한 Ready owner route를 잠시 보관할 수
있다. 이를 positive route cache라 한다. 이 정보는 Store의 current authority를
대체하는 별도 authority가 아니라 최근 조회 결과의 snapshot이다.

| 확인할 항목 | 계약 |
|---|---|
| Cache에 보관하는 정보 | [Positive route cache](01-glossary.ko.md#positive-route-cache)는 global object ID, `ObjectGeneration`, `AuthorityOwnerGeneration`, `StoreVersion`, owner lease, node lifecycle과 owner route를 보관한다. |
| 사용할 수 있는 기간 | Current owner lease의 local admission deadline과 `RouteCacheMaxAge` 가운데 먼저 끝나는 시점까지만 사용한다. |
| 기본 설정 | `RouteCacheMaxAge` 기본값은 15초다. 0이면 route cache를 사용하지 않는다. |
| 저장하지 않는 결과 | `Missing`, `Creating`과 Store failure는 cache하지 않는다. 이전 실패만으로 다음 call을 끝내지 않는다. |
| 즉시 무효화하는 조건 | 더 큰 `StoreVersion`, stale route 결과, Store recovery event 또는 owner lease invalidation을 확인하면 entry를 제거한다. |
| 실행 중 설정 변경 | 변경한 `RouteCacheMaxAge`는 새 cache entry부터 적용한다. 기존 entry의 수명을 새 값으로 연장하지 않는다. |

Resolve한 뒤 object가 close·destroy되고 같은 ID로 새 incarnation이 만들어져도
진행 중인 operation을 새 `ObjectGeneration`으로 보내지 않는다. Local owner와
remote owner에는 같은 handler, metadata와 completion 계약을 적용한다.

### 2.3 Object가 없을 때

Instance intent가 없는 Spot direct call과 Actor direct call은 이미 Ready인 object만
대상으로 한다.

- Missing Actor message는 Actor를 새로 만들지 않는다.
- Missing Spot message도 기본적으로 Spot을 새로 만들지 않는다.
- Spot 전용 fluent call에 Instance intent를 명시한 경우에만 Missing Instance
  Spot의 [cold activation](01-glossary.ko.md#cold-activation)을 시작할 수 있다.

`Missing`, `Creating`과 Store failure를 cache하지 않으므로 다음 call은 당시의
current 상태를 다시 확인한다.

### 2.4 이전 owner route에 도착한 message

Object relocation을 commit한 뒤에도 cache에 남은 이전 route로 message가 도착할 수
있다. 이전 owner는 commit된 source→target Message Follow route가 있을 때만 같은 operation을
current owner로 relay한다. Relay 중에는 Location Store를 읽거나 application
handler를 실행하지 않는다.

Message Follow route는 global object ID, `ObjectGeneration`, source·target
`AuthorityOwnerGeneration`과 owner fence를 검증한다. Owner generation은 hop마다
증가해야 하며 chain은 최대 8 hops다. Route 하나의 queue는 1,024 messages와
16 MiB를 넘을 수 없고 negotiated message bound도 함께 지킨다.

`MessageFollowDuration` 기본값은 30초이며 0이면 Message Follow를 사용하지 않는다.
`RouteCacheMaxAge`와 Message Follow duration이 모두 양수이면 cache max age가 Message Follow
duration보다 최소 5초 짧아야 한다. 실행 중 변경한 Message Follow duration은 새
relocation부터 적용한다.

Relay는 original operation ID, `ObjectGeneration`, payload와 reply route를
보존한다. Message Follow route가 없거나 만료됐거나 generation mismatch, loop 또는 bound
초과가 발생하면 typed stale-route 오류로 끝난다.

`PerActor` User Spot relocation 중 `ToActor`는 Spot authority가 아니라 Actor별
current owner route를 사용한다. Spot authority가 target으로 바뀌어도 아직 source에
남은 Actor는 source route를 유지한다. Actor owner CAS가 성공하면 이전 owner는
같은 Actor Message Follow route로 target에 relay한다.

Actor queue를 seal하기 전에 수락한 작업은 이전 queue와 accepted journal에
포함한다. Seal 뒤 source에 도착한 작업은 ingress hold에 보관한다. Target은 다음
순서를 지킨 뒤 Actor admission을 연다.

1. 이전 queue와 accepted journal을 복원한다.
2. Source ingress hold를 original operation identity와 reply route로 받는다.
3. Source의 relay 완료를 확인한다.
4. Owner CAS 뒤 target에 직접 도착해 대기 중인 작업을 받는다.

따라서 Actor가 전송 도중 이전되어도 caller가 새 route를 선택하거나 operation을
다시 만들 필요가 없다. Request deadline과 correlation, one-way operation identity,
ActorId와 ObjectGeneration을 relay 전후에 유지한다.

Framework는 실패한 현재 operation을 Location Store에서 찾은 새 owner에게 자동으로
다시 제출하지 않는다. 다음 call만 cache 또는 Location Store에서 current owner를
다시 찾는다. 이 규칙은 이미 실행됐는지 알 수 없는 operation이 두 owner에서
중복으로 실행되는 것을 막는다.

## 3. Session에 bind된 Actor로 relay하는 방법

### 3.1 Bind할 때 route를 저장한다

Session relay는 message마다 Actor ID를 resolve하지 않는다. Bind할 때 Actor route를
한 번 검증하고 Session owner에 저장한 뒤 이후 relay에서 그 정보를 사용한다.

```text
+----------------------------------------------------------------------+
| Session binding route                                                |
|                                                                      |
| Bind       : ActorRef -> validate -> store route                     |
| Relay      : Session -> stored route -> Actor owner                  |
| Relocation : Target -> command 44 -> Session owner                   |
|                                   -> command 45 ACK                  |
|                                                                      |
| No per-message Location Store lookup                                 |
+----------------------------------------------------------------------+
```

Session owner가 특정 Actor binding에 보관하는 current Actor owner 전달 경로를
binding route라 한다.

Bind는 caller가 제출한 exact `ActorRef`의 위치를 최초 route로 사용한다. Source가
bind 전에 Location Store에서 current route를 미리 조회하거나 local Actor instance를
받는 overload는 제공하지 않는다.

Actor owner는 다음 값이 current 상태와 일치하는지 확인하고 binding generation을
등록한 뒤 terminal reply를 반환한다.

- `ActorId`와 `ObjectGeneration`
- Target `NodeGeneration`
- `AuthorityOwnerGeneration`
- Current owner lease
- Session owner와 Session lifecycle identity

Bind가 성공하면 Session owner는 Actor마다 다음 정보를 하나의
[binding route](01-glossary.ko.md#binding-route)에 저장한다.

| 저장 정보 | 사용하는 이유 |
|---|---|
| `ActorId`, `ObjectGeneration` | 같은 ID로 다시 만든 다른 Actor에게 relay하지 않는다. |
| `MeshName`, owner `NodeRid` | Actor relay와 disconnect 통지를 보낼 route로 사용한다. |
| `NodeGeneration`, `AuthorityOwnerGeneration`, `OwnerLeaseGeneration` | 재시작 전 node와 이전 owner를 거부한다. |
| Session owner RID·lifecycle generation, binding generation·token | 이전 connection이나 교체된 binding의 늦은 message를 거부한다. |
| Session sequence | 같은 Session에서 수락한 message의 순서를 유지한다. |

다른 owner나 다른 Actor generation으로 rebind할 때 target Actor owner는 새 identity를
등록한 뒤 이전 exact owner에 tombstone을 제출한다. 이전 owner의 ACK까지 받은 뒤에만
bind terminal reply를 반환한다. Session owner는 terminal reply 전에는 기존 route를
유지하고, reply 뒤에는 새 route로 atomic하게 교체한다. 따라서 Session owner가
교체 뒤 별도의 durable retry journal을 보관하거나 Location Store·Relocation Store에
binding route를 기록하지 않는다. 같은 owner의 atomic replacement에서는 이전
identity tombstone이 새 identity를 제거하지 않아야 한다.

### 3.2 저장한 route로 message를 보낸다

Bind가 끝난 뒤에는 다음 작업이 저장한 route를 사용한다.

- Session에서 Actor로 보내는 `RelayAsync(...)`
- Physical disconnect와 application의 logical disconnect 통지
- Actor에서 bound Session으로 보내는 push

이 작업을 시작할 때마다 Location Store에서 Actor 위치를 조회하지 않는다. 저장
route는 current owner lease와 local admission deadline 안에서만 유효하다. Store를
일시적으로 사용할 수 없어도 lease나 deadline을 연장하지 않는다.

저장 route가 더 이상 유효하지 않으면 active Message Follow route로 original
operation을 정확히 한 번 전달하거나 typed stale 오류로 끝낸다. Location Store에서
새 `ActorRef`를 찾아 같은 operation을 다른 owner에게 자동으로 보내지 않는다.

Location Store와 Relocation Store는 binding route를 저장하거나 갱신하지 않는다.
이 route는 Session owner runtime이 소유한다. Direct route cache를 갱신해도 Session
binding route는 자동으로 바뀌지 않는다.

### 3.3 Actor relocation 뒤 저장 route를 바꾼다

Actor가 다른 MeshNode로 이동해도 physical STREAM connection과 Session object는
Session owner process에 유지한다. Socket, transport handle과 Session callback
state를 target Actor process로 옮기거나 복제하지 않는다.

Relocation 중에도 Session owner는 Location Store를 조회하여 새 Actor route를
추측하지 않는다. 같은 `ObjectGeneration`의 target Actor가 다음 순서를 완료한 뒤
Session owner에 새 route를 전달한다.

1. Source Actor와 Session ingress를 seal하고 마지막 accepted session sequence를
   high-water로 고정한다.
2. Bound-session request를 terminal 상태까지 정리하고 허용된 one-way 작업과 Actor
   state를 target staging에 준비한다.
3. Owner와 membership을 commit하고 lifecycle callback과 accepted journal replay를
   완료한다.
4. Durable source cleanup과 `Completed` authority CAS를 완료한다.
5. Target은 command 44로 Session owner에 route 갱신을 요청한다.
6. Session owner는 Actor generation, 이전·target owner generation, binding
   generation, owner lease와 high-water를 검증한다.
7. Session owner는 해당 Actor route만 atomic하게 바꾸고 command 45 ACK를 반환한다.
8. Maintenance authority를 steady target으로 정리한 뒤 target Actor의 session
   packet·push admission을 연다.

Route 갱신은 binding이 가리키는 `ObjectGeneration`과 같은 Actor relocation에만
허용한다. 같은 Actor ID로 새 incarnation이 만들어지면 기존 binding을 새 Actor로
바꾸지 않는다. Application이 새 `ActorRef`로 bind를 다시 시작해야 한다.

같은 Session에 bind된 다른 Actor의 route, token과 generation은 유지한다. Physical
STREAM connection도 그대로 유지한다. Command 45 ACK와 steady normalization 전에는
target Actor가 session packet이나 push를 수락하지 않는다.

Commit 전 relocation failure는 durable abort, source route 복원 ACK, cleanup과
steady source normalization 뒤 기존 source admission을 다시 연다. Commit 뒤에는
source route로 rollback하지 않으며 current target 또는 recovery coordinator가
route switch ACK와 steady normalization을 이어간다.

## 4. Request의 reply가 돌아가는 방법

### 4.1 Reply는 새 주소 조회를 시작하지 않는다

Source runtime은 request를 제출할 때 reply가 돌아올 내부 경로와 도착한 reply를
원래 request에 연결할 식별값을 함께 만든다.

```text
+----------------------------------------------------------------------+
| Request and reply                                                    |
|                                                                      |
| Request : Source -> Target  [reply route + correlation]              |
| Reply   : Target -> Source  [preserved reply route]                  |
|                                                                      |
| No SpotId / ActorId lookup for reply                                 |
+----------------------------------------------------------------------+
```

Target handler는 request가 가진 reply capability를 사용한다. Reply를 보내기 위해
request를 시작한 Spot·Actor의 global ID를 cache나 Location Store에서 resolve하지
않는다.

Request와 terminal reply를 연결하는 식별값을 reply correlation이라 한다.
[Reply correlation](01-glossary.ko.md#reply-correlation)은 어떤 request를 완료할지
정하고, reply route는 원래 source runtime으로 돌아갈 경로를 정한다.

Reply route와 correlation은 application metadata가 아니다. Application metadata는
업무 payload와 함께 전달하는 key-value 정보다. Request metadata를 reply에
자동으로 복사하지 않으며 일반 reply에는 metadata setter를 제공하지 않는다.

### 4.2 Spot에서 시작한 request를 재개한다

Spot에서 request를 시작했다면 source runtime은 request correlation과 함께 다음
정보를 보존한다.

- Request를 시작한 Spot 실행
- Request를 시작한 Spot의 `ObjectGeneration`

Reply가 도착하면 원래 request completion을 재개한다. 같은 Spot ID로 새 incarnation이
만들어져도 이전 reply를 새 Spot에 application message로 전달하지 않는다.

Spot·Actor operation이 Message Follow나 relocation payload를 거쳐도
original reply route와 correlation을 보존한다. Operation ID는 중복 작업을 구분하는
값이며 reply route를 대신하지 않는다.

### 4.3 Reply route를 사용할 수 없을 때

Framework는 reply route를 복원할 수 있는 request의 handler·decode failure를
구조화된 error reply로 완료한다. Reply route를 복원할 수 없다고 해서 requester의
Spot·Actor ID나 새 owner를 Location Store에서 찾아 우회하지 않는다. 해당 failure는
[상호작용 모델](03-interaction-model.ko.md#10-handler-실패)이 정한 drop과 log,
metric, observer event 계약을 따른다.

Route 오류, timeout, cancellation이나 실행 여부가 불명확한 failure 뒤에도 같은
request를 다른 owner에게 자동으로 재제출하지 않는다. Request는 reply, error,
timeout, cancellation 또는 shutdown 가운데 먼저 확정된 terminal 결과 하나로
완료한다.

## 5. 구현 및 contract test 검증 요구

- Spot·Actor direct 시작 method가 global ID만 받고 owner RID, generation과
  `ActorRef`·`SpotRef`를 message target으로 요구하지 않는다.
- Cache hit에서는 Location Store를 읽지 않고 cache miss나 invalidation 뒤 current
  Ready authority를 조회한다.
- `Missing`, `Creating`과 Store failure를 negative cache하지 않는다.
- Positive cache가 owner admission deadline과 `RouteCacheMaxAge`를 넘지 않고 higher
  `StoreVersion`, stale result, Store recovery와 lease invalidation에서 즉시
  제거된다.
- Target admission이 resolve한 exact object·owner generation과 lease fence를
  검증하며 새 incarnation으로 retarget하지 않는다.
- Message Follow relay가 committed route만 사용하고 Store를 읽지 않으며 operation
  ID, generation, payload와 reply route를 보존한다.
- `PerActor` User Spot relocation에서 `ToSpot`은 Spot authority, `ToActor`는 Actor별
  current owner를 사용하며 source hold, relay 완료와 target direct queue 순서를
  보존한다.
- Failed operation을 fresh owner에게 자동 재제출하지 않고 다음 call만 current
  authority를 다시 resolve한다.
- Bind가 caller의 exact `ActorRef` 위치를 최초 route로 사용하고 검증된 route만
  Session owner binding에 저장한다.
- Session relay, disconnect와 Actor push가 message마다 Location Store를 조회하지
  않고 stored binding route를 사용한다.
- Actor relocation이 같은 `ObjectGeneration`에서만 해당 Actor의 binding route를
  command 44·45로 바꾸고 다른 Actor route와 physical STREAM connection을 유지한다.
- Command 45 ACK와 steady normalization 전에는 target Actor의 session packet·push
  admission을 열지 않는다.
- Reply가 request의 reply route와 correlation을 사용하고 requester의 logical ID를
  Location Store에서 조회하지 않는다.
- Application metadata가 owner route와 reply route를 대신하지 않고 request metadata를
  reply에 자동 복사하지 않는다.
