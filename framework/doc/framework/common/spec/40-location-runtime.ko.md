# Location runtime

[공통 스펙 목차](README.ko.md) · [Spot 주소 메시징](24-spot-address-messaging.ko.md) ·
[Redis Location Store](41-location-store-redis.ko.md) · [Redis Relocation Store](42-relocation-store-redis.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위와 책임

Location runtime은 Framework service runtime에서 다음 작업을 조정한다.

- 실행 중인 service와 연결 endpoint를 찾는다.
- Global Actor·Spot을 현재 어느 node가 처리하는지 관리한다.
- Object를 만들거나 옮길 target의 capacity를 예약한다.
- 같은 logical object가 여러 node에서 중복 생성되지 않게 조정한다.
- 현재 owner로 가는 route를 cache하고 오래된 route를 구분한다.
- Host 교체 중 stateful object를 다른 node로 옮기고 복구한다.

Core raw transport는 Location key, [owner](01-glossary.ko.md#owner) lease, Actor·[Spot](01-glossary.ko.md#spot) identity, creation intent,
relocation envelope과 relocation phase를 해석하지 않는다.

Location Store와 Relocation Store는 서로 별도로 등록한다.

[Location Store](01-glossary.ko.md#location-store)는 “현재 누가 처리 권한을 가지는가”와 “이동이 어느 단계까지
확정되었는가”를 저장한다. Owner와 위치, generation, membership, placement
reservation뿐 아니라 relocation ID, source·target 검증 정보, 이동 payload의
reference와 checksum, participant 목록, commit과 replay 진행 수를 기록한다. 같은
version을 읽은 경우에만 변경하는 CAS로 여러 값을 함께 확정한다.

Relocation Store는 실제로 옮겨야 하는 application state와 아직 실행하지 않은 작업을
보관한다. Participant별 state·journal과 replay·recovery payload를 변경할 수 없는
root로 저장한다. 두 Store 모두 Actor용과 Spot용으로 나눈 별도 interface를 제공하지
않고 공통 operation을 사용한다.

[Object Server](01-glossary.ko.md#object-client와-object-server-role) factory에 `Recreate` 또는 `Snapshot` policy를 하나라도 등록했거나
Instance Spot [factory](01-glossary.ko.md#factory)를 하나라도 등록한 Framework root는 Relocation Store를 정확히
하나 등록해야 한다. 누락되거나 둘 이상이면 socket bind 전에 startup configuration
error다. [Instance Spot](01-glossary.ko.md#entry-spot-user-spot과-instance-spot) factory가 없고 모든 factory가 `Disabled`일 때만 Relocation
Store가 필요하지 않다. 이 경우 cross-node 이동은 capture 전에 거부한다. Same-node
Actor join은 Location Store [membership](01-glossary.ko.md#membership) transaction만 사용하며 Relocation payload를
만들지 않는다.

| 이동 조건 | Relocation Store 사용 |
|---|---|
| Same-node Actor join | 사용하지 않는다. Location Store에서 membership만 commit한다. |
| `Disabled` cross-node 이동 | 사용하지 않는다. Capture 전에 거부한다. |
| `Recreate` cross-node Actor·Spot 이동 | Application state 없이 accepted journal과 recovery payload를 저장한다. |
| `Snapshot` cross-node Actor·Spot 이동 | Application state, accepted journal과 recovery payload를 저장한다. |
| Host maintenance의 Actor·User Spot aggregate | Participant별 state·journal payload와 replay manifest를 저장한다. |
| Cross-node Actor `JoinSpot`·`JoinEntrySpot` | 이동하는 Actor의 policy에 맞는 payload를 저장한다. |
| Instance Spot cold activation | Complete activation envelope와 durable activation inbox의 첫 record를 저장한다. Relocation policy가 `Disabled`여도 사용한다. |

Location Store는 다음 두 종류의 정보를 분리한다.

- MeshNode descriptor, ClientServer Server descriptor와 fanout publisher descriptor를
  통칭하는 discovery descriptor는 host owner lease에 종속된 ephemeral discovery
  data다.
- Actor·Spot authority는 명시적 fenced delete까지 유지되는 durable data다.

Transport ready, [descriptor](01-glossary.ko.md#descriptor) 존재와 owner authority는 서로 다른 조건이다. Resolver와 placement는 필요한 조건을
모두 확인하며 어느 한 신호로 다른 조건을 대신하지 않는다.

Object role이 `Client` 또는 `Server`인 MeshNode는 Location Store가 필수다. Store가 없으면 startup을 실패하며
runtime-local manager, hidden authority나 local CAS provider를 만들지 않는다. Role이 `None`인 [MeshNode](01-glossary.ko.md#meshnode)는 object
create, find, message와 factory를 제공하지 않는다.

## 2. Identity와 generation

### 2.1 Host lifecycle token

한 Framework host process lifecycle은 `(OwnerId, LeaseGeneration)` token 하나를 사용한다. OwnerId는 Framework가
만드는 재사용 불가능한 값이고 LeaseGeneration은 Store provider가 global durable counter에서 발급하는 non-zero
값이다. 같은 host의 모든 MeshNode·ClientServer·fanout descriptor, automatic RID claim, Actor·Spot authority와
maintenance role이 이 token을 공유한다.

LeaseGeneration은 host lifecycle fence다. Object owner가 바뀌는 횟수를 나타내지 않는다. Provider는 필요한
OwnerId를 exact read하며 provider-wide
[owner lease](01-glossary.ko.md#owner-lease) list를 권한 판단에 사용하지 않는다.

Owner lease Claim과 expired-row Takeover가 새 LeaseGeneration을 발급해야 하는데 global counter가 `2^63-1`이면
provider는 closed `GenerationExhausted`를 반환한다. 이 결과는 non-retriable이며 lease row, index와 counter를
변경하거나 소비하지 않는다. Renew와 Release는 새 generation을 발급하지 않으므로 이 결과를 반환하지 않는다.

### 2.2 Object authority generation

Durable [authority](01-glossary.ko.md#authority) metadata는 다음 값을 분리한다.

| 값 | 의미 |
|---|---|
| ObjectGeneration | 같은 canonical object key의 서로 다른 logical incarnation을 구분한다. Relocation의 `Recreate`는 target에서 object를 다시 만들더라도 같은 incarnation이므로 이 값을 유지한다. |
| AuthorityOwnerGeneration | 같은 object incarnation의 authority owner가 바뀐 순서를 나타낸다. |
| StoreVersion | Exact CAS expectation에 사용하는 opaque row revision이다. |
| OwnerLeaseGeneration | Current host process lifecycle token의 generation이다. |

이 값은 모두 provider가 발급한다. ActorRef와 SpotRef의 generation은 [ObjectGeneration](01-glossary.ko.md#objectgeneration)이다.

ObjectGeneration, [AuthorityOwnerGeneration](01-glossary.ko.md#authorityownergeneration)과 새 StoreVersion을 위한 StoreRevision은 provider transaction domain의
global monotonic counter를 사용하고 maximum은 `2^63-1`이다. 필요한 increment 시 counter가 maximum이면 provider는
closed write result `GenerationExhausted`를 반환한다. 이 결과는 non-retriable이며 row, index와 모든 counter의 mutation·
consumption은 0이다. 같은 상태의 반복 호출도 같은 결과다. Transport exception과 혼합하지 않는다. Runtime은 해당
authority를 error로 seal하고 wire command를 보내지 않으며 host snapshot과 error event에 원인을 기록한다. Counter를
reset하거나 wrap해 복구하지 않는다.

### 2.3 Authority snapshot

Authority canonical key는 Actor의 global ActorId 또는 Spot의 global SpotId다. 두 ID는 UTF-8 1..255 bytes,
case-sensitive exact value이며 normalization과 case folding을 하지 않는다. Spot row는 Entry, User와 Instance
kind를 closed union으로 구분한다. MeshName은 identity key가 아니라 current placement attribute다. [Snapshot](01-glossary.ko.md#relocation-policy)은
opaque payload와 다음 provider metadata를 포함한다.

- StoreVersion, ObjectGeneration과 AuthorityOwnerGeneration
- current OwnerId와 [OwnerLeaseGeneration](01-glossary.ko.md#ownerleasegeneration)
- provider capacity state `Reserved` 또는 `Active`, object kind·stable type, current descriptor key·lifecycle
  generation과 typed capacity bundle로 구성한 current placement allocation
- StoreNow

Initial placement intent, creation intent와 relocation phase·fence는 Framework가 encode한 opaque payload에 둔다.
Current placement allocation의 kind·[stable type](01-glossary.ko.md#stable-type)·descriptor key·lifecycle generation·typed capacity bundle은 payload에
중복 encode하지 않는다. Provider도 application state와 relocation payload를 해석하지 않는다. Canonical authority
key가 immutable object identity를 제공하므로 payload에 key를 다시 넣지 않는다.
Typed capacity bundle은 Actor slot 수, Spot slot 수와 optional `(Spot kind, stable type, slot 수)` 하나를
포함한다. 각 slot 수는 `0..2^31-1`이고 bundle 전체에는 하나 이상의 양수 slot이 있어야 한다. Actor 하나는
Actor slot 하나를 사용한다. User·Instance Spot 하나는 Spot slot 하나와 해당 Spot kind·stable type slot
하나를 사용한다. User Spot aggregate relocation은 Spot slot 하나, 해당 Spot type slot 하나와 participant
Actor 수만큼의 Actor slot을 하나의 bundle로 사용한다. Creation reservation, relocation reservation,
current allocation과 capacity counter는 같은 bundle을 사용한다.

Authority row에는 TTL이 없다. Owner lease가 만료돼도 row는 남으며 recovery coordinator가 expected StoreVersion
CAS로 owner를 교체하거나 명시적으로 삭제한다. Missing read는 StoreNow만 반환하고 synthetic StoreVersion이나
generation을 만들지 않는다.

## 3. Descriptor와 discovery

MeshNode, ClientServer server와 [fanout publisher descriptor](01-glossary.ko.md#fanout-publisher-descriptor)는 current host owner lease token을 포함한다. Automatic
consumer는 descriptor page를 읽고 owner lease를 exact 확인한 뒤 desired connection을 계산한다.

DescriptorRevision은 Framework host가 descriptor마다 발급하는 non-zero monotonic 값이며 provider global counter가
아니다. 값이 `2^63-1`에 도달해 다음 revision이 필요하면 wrap하지 않고 host를 `Error`로 seal하며 새
descriptor를 게시하지 않는다.

Descriptor page는 request item 1..1000개, encoded 최대 4 MiB다. Framework는 scope change stamp를 열거 전후에
읽고 값이 같을 때만 complete page set을 적용한다. Continuation은 provider-issued opaque cursor다. Provider가
전체 list를 Lua에서 materialize하거나 Redis `SCAN` 결과를 그대로 공개하면 안 된다.

Host는 startup에서 complete descriptor를 먼저 만든다. Encoded descriptor 최대 크기는 1 MiB다. Type과
Snapshot adapter capability vector는 각각 최대 1024개다. 초과하면 startup/configuration을 atomic하게
실패하고 descriptor를 truncate, split하거나 일부 publish하지 않는다. Descriptor는 application state
format이나 version을 포함하지 않는다.

Descriptor가 connection intent를 만들 수는 있지만 [ready](01-glossary.ko.md#ready)를 증명하지 않는다. RouteMesh와 ClientServer는 current
physical connection의 service admission을 통과해야 한다. Fanout subscriber는 publisher별 전용 SUB socket에서 첫
정상 application record 또는 형식이 올바른
[liveness beacon](01-glossary.ko.md#liveness와-liveness-beacon)을 받은 뒤 ready다.

Object Server descriptor는 `Server` role, node-wide placement weight, node별 Actor·Spot count와 limit,
Spot stable type별 capability를 포함한다. [Weight](01-glossary.ko.md#weight)는 0..10000이고 기본값은 100이다.
Actor 전체·Spot 전체 limit과 User·Instance Spot stable type별 limit의 기본값 `0`은 제한 없음이다.
양수 limit은 `1..2^31-1`이며 음수는 startup configuration error다. Entry Spot은 node마다 하나로
고정하며 Spot count에서 제외하지만 Entry Spot의 Actor는 Actor 전체 capacity에 포함한다. Actor stable
type별 limit은 제공하지 않는다. Location Store의 active·reserved count가 authoritative value이고
descriptor count는 projection이다.
같은 descriptor에는 이 MeshNode lifecycle에 발급한 exact Entry Spot ID를 포함한다. Actor placement와 Entry
Spot join은 descriptor key·lifecycle generation·Entry Spot ID mapping을 함께 고정하며 Spot ID 문자열을 parsing해
관계를 계산하지 않는다.
Placement는 current lease와 `Serving` 상태를 확인하고 descriptor capacity projection으로 후보를 거른 뒤
Location Store에서 Active와 reserved slot을 원자적으로 검사한다. 그 다음 positive weight 비율로 target을
고른다. Weight 0은 새 reservation과 relocation target에서만 제외하며 Ready object와 이미 완료된
reservation은 취소하지 않는다.

## 4. Owner lease와 local admission deadline

Location Store를 사용하는 모든 host는 startup에서 다음 관계를 검증한다. Routing ID allocation 사용 여부와
무관하다.

```text
renew interval + renew timeout < owner lease TTL - owner lease fencing margin
```

공통 기본값은 renew interval 5초, TTL 15초, renew timeout 3초, owner lease fencing margin 5초다. 값은 모두
0보다 커야 하고 위 관계가 성립하지 않으면 startup을 실패한다. Automatic RID descriptor claim도 별도 deadline을
만들지 않고 같은 host token과 [deadline](01-glossary.ko.md#deadline)을 사용한다.

성공한 Claim·Read·Renew 결과의 StoreNow와 ExpiresAt으로 남은 시간을 계산하고 operation 시작 전후 local monotonic
시각을 읽어 보수적인 deadline을 만든다. 한 번 성공한 host-token renew가 host 전체의 shared local admission
deadline을 갱신한다. Object별 deadline이 이 값을 연장하지 않는다.

Deadline을 넘거나 exact token read가 stale이면 다음 admission을 즉시 seal한다.

- descriptor publication과 automatic RID owner mutation
- Actor·Spot·Instance message와 timer turn
- factory·restore completion commit
- relocation source·target·coordinator phase CAS와 relocation capacity fence

이미 local queue에 accept한 turn의 terminal 처리와 cleanup은 별도 deadline 계약에 따라 진행할 수 있지만 stale
owner 권한으로 새 mutation을 만들지 않는다.

## 5. Authority operation

### 5.1 Read와 CAS

Authority Read는 `Missing(StoreNow)` 또는 `Found(snapshot, StoreNow)` closed result다. Public authority
mutation은 Active `Found`의 exact expected StoreVersion만 받는다. Missing→Reserved 생성은 generic Reserve가
전담하므로 public compare-exchange에 Missing expectation을 제공하지 않는다.

- Preserve는 Active allocation에서 target owner token 없이 object, authority owner generation과 placement
  allocation을 유지하고 StoreRevision만 바꾼다.
- NewOwner는 Active allocation에서 exact target owner token과 relocation capacity fence를 받아 ObjectGeneration을
  유지한 채 새 AuthorityOwnerGeneration을 발급하고 target Active allocation으로 교체한다.
- Delete는 Active allocation에서만 row와 current index entry를 제거하고 current active capacity delta를 같은
  transaction에서 감소시킨다.

Provider는 expectation 검증, global counter 소비, row와 index 변경을 atomic하게 수행한다. Opaque payload 안에
provider metadata를 중복 encode하지 않는다. Put mutation의 target owner token은 Preserve에서 없어야 하고
NewOwner에서 반드시 있어야 한다. Provider는 이 token으로 owner ID와 owner lease generation을 metadata에
기록하며 payload를 해석해서 owner를 복원하지 않는다. Preserve와 Delete는 row에 저장된 current owner token의
lease를, NewOwner는 mutation의 target owner token lease를 같은 transaction에서 검증한다. Lease가
missing·stale이면 current authority read를 담은 Conflict로 끝내고 row·index·counter를 변경하지 않는다. Token
존재 규칙을 위반한 mutation은 provider I/O 전에 argument validation error로 거부한다. Relocation capacity
fence는 `NewOwner`에서만 반드시 있고 `Preserve`에서는 없어야 한다. 이 조합도 provider I/O 전에
검증한다.

Missing→Reserved allocation은 generic `Reserve`, Reserved→Active는 exact reservation `Commit`, Reserved→Missing은
exact `Abort`만 수행한다. Active→다른 Active는 capacity fence를 소비하는 NewOwner 또는 aggregate commit,
Active→Missing은 Delete만 수행한다. Reserved row에 generic Preserve·NewOwner·Delete를 적용하면 current authority
read를 담은 Conflict이며 mutation은 0이다. Public authority transition에는 별도 create transition이 없고
ObjectGeneration과 initial AuthorityOwnerGeneration·allocation은 Reserve만 발급한다.

### 5.2 Snapshot-consistent scan

Recovery scan은 1..1000 item과 encoded 4 MiB 상한을 지킨다. 첫 call은 cursor가 없고 provider가 snapshot
watermark와 scan lease를 내부에 만든다. `AuthorityScanCursor`는 non-empty, 최대 4096-byte opaque value다.
Framework는 이를 parse, 조합하거나 다른 scan에 섞지 않는다. Expired, replayed 또는 cross-scan cursor는
`ScanExpired`로 끝난다.

첫 watermark에 존재한 row incarnation을 canonical key byte 순서로 정확히 한 번 반환한다. Scan item은 recovery
후보일 뿐이다. Framework는 exact key Read와 expected StoreVersion CAS를 다시 수행한다. Provider는 active scan
lease가 참조하는 versioned index와 tombstone만 보관하고 older scan이 모두 끝나면 GC한다.

## 6. Logical create와 placement reservation

Actor와 User Spot은 Manager의 명시적인 `Create` 또는 `GetOrCreate`로 생성한다. ActorId와 SpotId는 global
canonical key이고 [MeshName](01-glossary.ko.md#meshname)은 initial placement attribute다. Actor operation은 required ActorId를 받는다.
User Spot의 `Create`는 Framework가 lowercase canonical UUID v4 string SpotId를 발급하고,
`GetOrCreate`는 caller의 SpotId와 stable type을 받는다.
Entry Spot identity는 Framework만 발급한다. Instance Spot은 Manager create family를 제공하지 않으며 §6.1의
Spot direct [cold activation](01-glossary.ko.md#cold-activation)으로만 Missing authority를 만든다.

Create option에 `InMesh`가 있으면 그 Mesh를 사용한다. 생략했고 Object Client 또는 Server role의 Mesh가 하나이면
그 Mesh를 사용한다. 후보가 0개면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`, 명시한 Mesh가
없으면 `MeshNotFound`다.
Create call은 object kind에 따라 필요한 identity와 stable type, optional MeshName·creation request·
timeout을 가진 single-use operation이다. Terminal submit 시점에 resolve,
reservation, factory와 Ready barrier 전체에 적용할 하나의 end-to-end deadline을 고정한다. 같은 option을 두 번
설정하면 `InvalidConfiguration`, terminal submit을 두 번 호출하면 `AlreadySubmitted`다.

Creation request는 encoded 최대 1 MiB다. Actor와 User Spot manager의 generic create는 request content를
Location Store의 creation reservation 영역에 보관하며 ZLIA root나
[durable activation inbox](01-glossary.ko.md#durable-activation-inbox)를 사용하지 않는다. 따라서 이
factory들의 policy가 `Disabled`이고 Instance Spot factory가 없으면 Relocation Store가 필요하지 않다.

Target이 시작하는 Instance Spot cold activation만 reservation 전에 complete [activation envelope](01-glossary.ko.md#activation-envelope)를 Relocation
Store에 변경할 수 없게 저장한다. Content reference, SHA-256과 encoded size는 Reserved creation projection에
기록한다. 이 Instance의 `Ready` authority는 최초 handler의 완료와 replay cursor 갱신이 끝날 때까지 recovery
root를 유지한다. Factory는 `(logical key, ObjectGeneration, creation attempt)`에 대해 at-least-once 실행될 수
있으므로 retry-safe해야 한다.

Actor와 User Spot manager가 시작하는 Missing object 생성은 다음 순서를 따른다. Instance Spot은 target
runtime이 activation envelope를 수락한 뒤 reservation을 만드는 §6.1의 순서를 사용한다.

1. Complete descriptor와 exact lease에서 role, `Serving` 상태, stable type capability와 active·pending
   capacity를 만족하는 target을 찾고 positive node-wide placement weight로 하나를 선택한다.
2. Framework는 선택한 descriptor key·[lifecycle generation](01-glossary.ko.md#lifecycle-generation)·owner token과 Creating authority payload를 encode한다.
   Generic `Reserve`는 payload를 해석하지 않고
   `Missing → Creating` authority, creation intent와 target reserved capacity bundle을 하나의 atomic transaction으로
   기록한다. ObjectGeneration과 AuthorityOwnerGeneration은 이때 발급한다.
   Reserve가 capacity 소진 또는 target descriptor·lifecycle·lease 변경으로 거부되고 authority가 계속
   Missing이면 해당 candidate lifecycle을 제외하고 최신 complete descriptor에서 다른 eligible target을
   선택한다. 이 재선택은 operation deadline까지만 반복한다. 다른 operation이 authority를 먼저 만들었다면
   target을 다시 선택하지 않고 current authority를 reconcile한다.
3. Target은 reservation에 고정한 exact authority, descriptor lifecycle과 owner
   lease를 확인하고 factory와 initialization을 실행한다. Remote target이면 source는
   reservation을 만든 다음 User Spot에는 command 47 `userSpotCreate`, Actor에는
   command 49 `actorCreate`를 보낸다. Creation request bytes는 wire payload로 다시
   보내지 않는다. Target은 command의 source lifecycle,
   key·type, provider가 발급한 reservation·`StoreVersion`과 target lifecycle을
   Reserved authority exact read 결과와 비교한다.
4. Actor는 Entry Spot의 creation callback, User Spot은 자신의 creation callback으로
   application의 생성 결정을 받는다. Callback exception은 application `Rejected`와
   구분한 typed creation failure다.
5. 생성이 승인되면 Framework는 Ready authority payload와 `Created` terminal result를 encode한다.
   Generic `CompleteCreation`의 Created branch는 payload를 해석하지 않고 같은 reservation의 target
   descriptor lifecycle과 owner lease를 다시 확인한 뒤 `Creating → Ready`,
   pending-to-active capacity 전환과 terminal record publish를 atomic하게 수행한다.
6. 생성이 거절되면 같은 `CompleteCreation`의 Rejected branch가 exact Creating authority를
   제거하고 reserved capacity를 반환하면서 `Rejected` terminal record를 atomic하게
   publish한다. Ready authority와 active capacity는 만들지 않는다.
7. Callback exception은 `CompleteCreation(Failed)`가 Creating authority와 pending
   capacity를 정리하면서 operation terminal을 기록한다. Node 종료나 recovery cleanup은
   `Abort`가 exact reservation을 정리하며 operation terminal을 기록하지 않는다.

위 Rejected branch는 generic creation 흐름에서 `Reject`라고 부르는 동작에 해당한다.
Location Store의 닫힌 creation surface에서는 `CompleteCreation`의 `Rejected` case로
표현한다.

`Create`가 Ready object를 찾으면 같은 stable type이어도 `AlreadyExists`다. `GetOrCreate`는 같은 type의 Ready
object를 반환하고 같은 type의 Creating attempt이면 authority 변경을 기다린다. 다른 Actor type 또는 Spot kind·type은
`TypeMismatch` 또는 `SpotTypeMismatch`다. 동시에 요청했지만 생성 권한을 얻지
못한 target은 다른 target을 선택하거나 factory를 시작하지 않는다. Creating
waiter가 deadline에 도달하면 `DeadlineExceeded`로 끝나며 다음 call이 Store의 현재
authority를 다시 확인한다.

상태 전이는 다음과 같다.

```text
Missing
  → Reserved(R1)
      ├─ Created(R1, ObjectRef, ReplyRef?)
      ├─ Rejected(R1, ReplyRef?)
      └─ Aborted(R1, Failure)
```

`Created`, `Rejected`와 `Failed`
[terminal record](01-glossary.ko.md#creation-terminal-result)는 exact source Node
RID·lifecycle generation·`OperationId`로 식별한다. 같은 operation의 재전송만 retained
terminal을 읽는다. 서로 다른 operation은 Ready 뒤 `Existing`을 받고, rejection·failure
cleanup 뒤 Missing이면 새 reservation을 경쟁한다. Terminal record에는 request
correlation과 reply route가 없는 `creation-operation-terminal-v1` semantic envelope와
SHA-256을 저장한다. 재전송 reply는 현재 correlation과 reply route로 새로 encode한다.
Envelope는 최대 1,048,576 bytes이며 original operation deadline 뒤 5분에 TTL로
제거한다. Terminal record의 `ExpiresAt`은 original operation deadline에 5분을 더한
provider Store time의 절대 시각이며 application은 retention을 설정하지 않는다.

Location Store의 generic creation surface는 다음 닫힌 operation을 제공한다.

- Runtime은 `Reserve` 전에 exact source Node RID·lifecycle generation·128-bit
  `OperationId`로 `ReadCreationTerminal`을 호출한다. Retained record가 있으면
  reservation을 만들지 않고 기존 terminal을 반환한다. Read가 `Missing`인 뒤
  경쟁이 발생해도 authority reservation과 terminal key의 CAS가 callback 결과의
  중복 publication을 막는다. Conflict 뒤에는 terminal을 다시 읽는다.
- `CompleteCreation`은 `Created`, `Rejected`, `Failed` union이다. `Created`는 Ready
  payload와 terminal publication을 받고, 나머지는 terminal publication만 받는다.
- `Created`는 Ready publication, reserved-to-active capacity와 terminal publication을
  하나의 transaction에서 수행한다. `Rejected`와 `Failed`는 Creating 삭제, reserved
  capacity release와 terminal publication을 하나의 transaction에서 수행한다.
- `ReadCreationTerminal`은 exact operation identity로 `Missing` 또는 `Found`를
  반환한다.
- Recovery용 `Abort`는 operation terminal을 만들지 않는다. Runtime은 application
  rejection이나 callback failure를 `Abort`로 처리하지 않고 `CompleteCreation`의
  대응 case로 처리한다.

Factory 또는 callback exception은 `CompleteCreation.Failed` case로 Creating authority와 reserved
capacity를 정리하면서 operation terminal을 함께 기록한다. Ready
publication을 시작하기 전 infrastructure failure는 generic `Abort`로 Creating authority와
reserved capacity를 함께 정리한다. Abort는 current descriptor lifecycle이나
owner lease의 유효성을 요구하지 않고 reservation에 고정한 이전
descriptor·capacity counter를 exact fence로 정리한다. Owner lease가 stale이면
recovery coordinator가 exact fence로 attempt를 takeover하거나 abort한다.
Caller cancellation, timeout이나 response loss는 commit 실패를 의미하지 않으므로 exact read로 결과를 확인한다.
Original creation payload와 일반 message를 다른 owner에 hidden retry하지 않는다.
Exact read는 recovery와 다음 call의 상태 조정에 사용하는 근거이며 현재 call의
terminal completion은 아니다. Remote create는 command 20 reply가
`Existing`·`Created`·`Rejected`, exact `SpotRef`와 optional application reply를 한
번 반환해야 완료된다. 같은 source lifecycle·`OperationId`의 재전송이면 semantic
terminal envelope를 현재 command 20 correlation으로 다시 framing한다.

### 6.1 Instance Spot direct cold activation

[Spot direct](01-glossary.ko.md#spot-direct) call은 기본적으로 existing-only다. Instance intent가 없는 call은 Missing authority에서
target-not-found로 끝난다. [Instance intent](01-glossary.ko.md#instance-intent)가 있는 call은 optional stable type과
`InMesh`를 Missing object의 최초 placement에 사용할 수 있다.

Source가 Location Store에서 Ready authority를 찾으면 저장된 kind·stable type과 current Mesh를 사용한다. 이 경우 cold
activation option은 current owner를 제한하거나 다른 Mesh로 이동시키지 않는다. Missing이면 먼저 object Mesh를
선택한다. `InMesh`를 생략했고 후보가 0개이면 `ObjectClientNotConfigured`, 둘 이상이면
`MeshSelectionRequired`다. Stable type을 생략했을 때 선택한 Mesh의 serving descriptor에 등록된 distinct
Instance type이 하나면 자동 선택한다. 0개이면 target-not-found, 둘 이상이면 required type을 생략한
`InvalidConfiguration`이다. 여러 node가 같은 stable type을 등록한 경우 distinct type 하나와 여러 placement
후보로 처리한다.

Source는 선택한 type·Mesh와 eligible target descriptor fence를 고정하고 global Spot
RID, source node RID·lifecycle generation·optional source Spot ID, operation
identity·reply correlation·deadline, command 39의 optional metadata 존재 여부와
metadata frame 및 first message를 하나의 activation envelope로 target transport에
제출한다. Source는 전송 전에 owner claim, Missing→Reserved reservation 또는 synthetic
generation을 만들지 않는다.

Target은 Location Store의 현재 owner 기록과 자신의 Instance Spot 목록을 함께
확인한다. `Ready`인 현재 owner가 자신이고 같은 generation의 Spot이 이미 있으면 기존 queue를
사용한다. Store에 owner가 없고 target에도 현재 사용할 Spot이 없으면 target은
자신에게 이 Spot을 만들어도 되는지 `Reserve`로 요청한다.

Target은 complete activation envelope를 Relocation Store에 변경할 수 없는 recovery
root로 저장하고 receipt를 확인한다. 그 뒤 자신을 owner로 하는 `Reserve`를 Location
Store에 요청한다. Store는 target descriptor의 lifecycle, owner lease, type과
capacity를 한 transaction에서 다시 확인한다. 조건을 만족하면 `Creating` authority,
provider가 발급한 reservation fence, recovery receipt와 reserved capacity를 함께
확정한다. Reserved authority snapshot은 정확한 reservation ID, request content
reference, SHA-256과 encoded size를 반환한다. Active authority에는 이 Reserved
creation 정보를 두지 않는다.

생성 권한을 얻은 target만 factory와 initialize를 실행한다. Target은 recovery
root의 first message를 durable activation inbox의 첫 record로 확정하되 application
handler는 barrier로 계속 차단한다. `Ready` commit은 recovery root와 [replay cursor](01-glossary.ko.md#replay-cursor)를
authority에 유지하면서 active capacity를 게시한다. Runtime은 first record를 local
queue 선두에 복원한 뒤 barrier를 열며 후속 message는 이 record를 추월할 수 없다.

최초 handler의 완료를 durable하게 기록하고 replay cursor를 inbox sequence까지
갱신한 뒤에만 expected-version `Preserve` CAS로 activation recovery pointer를
제거한다. Queue에 넣었다는 사실만으로 pointer를 제거하지 않는다. CAS가 성공한
다음 Relocation Store root를 idempotent하게 삭제한다. Source는 `Ready` 뒤 first
message를 별도 direct call로 다시 보내지 않는다. [Activation recovery pointer](01-glossary.ko.md#activation-recovery-pointer)는
`Ready` Instance cold activation에만 존재하며 Actor, Entry Spot, User Spot,
`Creating`·`Closing`·`Relocating` authority와 maintenance relocation payload에는
둘 수 없다.

Target process가 `Reserve` 뒤 종료되면 startup Serving gate의 complete authority
scan과 이후의 bounded background scan이 Reserved creation projection을 exact
read한다. Recovery는 root reference·SHA-256·size를 검증하고 같은 reservation,
`ObjectGeneration`과 `AuthorityOwnerGeneration`으로 factory, initialize, durable
inbox와 `Ready` commit을 이어가거나 정확한 fence로 `Abort`한다. `Ready` commit 뒤
queue 선두 복원 전에 종료되면 recovery root와 cursor로 inbox를 먼저 복원한다. 그
전에는 해당 owner의 application admission을 열지 않는다. Relocation Store에 root를
저장했지만 `Reserve` 전에 종료되었다면 authority가 참조하지 않는 orphan이므로
retention 또는 idempotent delete로 정리한다.

동시에 보낸 envelope가 다른 target에 도착해도 생성 권한을 얻은 target 하나만
factory를 실행한다. 나머지 target은 local instance를 만들지 않는다. 권한을 얻은
Spot이 Ready이면 원래 operation을 그 owner로 한 번 전달하고, 아직 `Creating`이면
같은 activation의 완료를 기다린다. Target의 목록에 Spot이 있더라도 Store의 current
authority가 다른 owner나 generation을 가리키면 오래된 local instance로 판단하여
message를 실행하지 않는다. 기존 User Spot 또는 다른 stable type authority는
`SpotTypeMismatch`다. Public check와 send를 별도 operation으로 나누지 않는다.

### 6.2 Find, ref와 exact mutation

Manager `Find(global ID)`는 existing Ready authority만 반환하며 create를 시작하지 않는다. ActorRef와 SpotRef는
`{global ID, ObjectGeneration, MeshName, NodeRid}` location snapshot이다. ObjectGeneration은 non-zero unsigned
63-bit conceptual value이고 JSON에서는 decimal string이다. Ref의 MeshName과 NodeRid는 current location을
진단하거나 exact generation을 고정하는 snapshot이며 일반 message target이나 placement input이 아니다.

Destroy와 Close는 exact ref를 받는다. 같은 incarnation이 없으면 idempotent `false`, 다른 generation이면
stale-generation error, moving이면 typed moving error다. Runtime은 current ref를 다시 찾아 새 incarnation을 종료하지
않는다.

Remote User Spot을 닫을 때 source는 command 48 `userSpotClose`를 current owner로
보낸다. Command는 source lifecycle과 operation identity, exact `SpotRef`, target
lifecycle, `AuthorityOwnerGeneration`, `StoreVersion`과 deadline을 고정한다. Target은
current authority, active Actor membership과 moving state를 확인한 뒤에만 Closing
CAS를 수행한다. Command 20 reply의 `closed` bool로 결과를 한 번 완료하며 Location
row 조회나 reserved application packet은 create·close의 terminal result를 대신할 수
없다.

### 6.3 Route cache와 stale-route forwarding

Positive Ready cache entry는 global key, ObjectGeneration, AuthorityOwnerGeneration, StoreVersion, owner lease,
node lifecycle과 route를 보존한다. `RouteCacheMaxAge` 기본값은 15초이고 current owner lease의 local admission
deadline을 넘을 수 없다. Missing, Creating과 Store failure는 cache하지 않는다. Store recovery, higher
StoreVersion, stale result나 lease invalidation은 entry를 즉시 제거한다.

`RelocationForwardingWindow` 기본값은 30초다. 두 duration은 0이면 각각 cache와 forwarding을 끈다. 둘 다 양수이면
cache max age가 forwarding window보다 최소 5초 작아야 한다. Runtime 변경은 새 cache entry와 새 relocation에만
적용한다. 범위나 두 값의 관계를 위반한 설정은 runtime에 적용하지 않고 configuration error로 끝낸다.

Relay는 committed source→target mapping만 사용하며 Store를 읽지 않는다. Mapping의 target
AuthorityOwnerGeneration은 source보다 커야 하고 chain은 최대 8 hops다. Mapping 하나의 대기열은 1024 message와
16 MiB를 넘지 않으며 negotiated message bound도 함께 지킨다. Original operation ID, ObjectGeneration, payload와
reply route를 보존한다. Loop, generation mismatch와 bound 초과는 typed stale-route error다.

## 7. Stateful maintenance authority

### 7.1 Stable identity와 compact authority

`RelocationId`는 runtime이 CSPRNG로 만드는 non-zero 128-bit 값이며 relocation root, journal, replay, late completion과
terminal identity 전체에서 안정적이다. Active relocation과 retention 중 relocation root에서 collision을 확인하면
사용하지 않고 새 값을 만든다. Application에 노출하지 않는다.
`TargetAttemptGeneration`은 같은 relocation에서 target capacity reservation을 교체하는 non-zero attempt fence일 뿐이다.
Target replacement만으로 immutable relocation root를 다시 만들거나 RelocationId를 바꾸지 않는다.

Authority hot row는 encoded 최대 1 MiB이며 다음 compact 정보만 가진다.

- immutable source OwnerId·LeaseGeneration과 source node RID·generation
- current target attempt, target owner lease·node fence와 reservation
- coordinator owner lease·node fence
- phase, application version, relocation root reference와 checksum
- participant progress, TerminalCompletionCount, PendingRelayCount와 SourceCleanupState

Full journal, participant별 state·journal, reply payload와 terminal completion vector는 Relocation Store의 immutable
stream에만 저장한다. Canonical participant set, participant별 mutation, aggregate generation과 inventory digest는
Location Store가 authority로 저장한다. Relocation manifest의 같은 digest는 payload 탐색 projection이며 authority가 아니다.

Cross-node relocation은 Missing object 생성용 reservation을 재사용하지 않는다. Framework가 만든 non-zero
128-bit reservation ID, current authority key·StoreVersion, object kind·stable type, source descriptor key·
lifecycle generation·owner token, target descriptor key·lifecycle generation·owner token과 capacity delta를
`ReserveRelocationCapacity`에 전달한다. Provider는 current
authority owner와 Active placement allocation이 request의 source descriptor·lifecycle·kind·stable type·capacity
delta와 정확히 같은지 확인한다. Source descriptor row나 source owner lease의 live 상태는 요구하지 않는다.
Target descriptor lifecycle·owner lease·capability·reserved capacity는 같은 transaction에서 live/exact로
검증하고 target reserved capacity를 예약한다. 같은 reservation ID와 exact request의 재호출은
같은 fence를 반환하고, 다른 내용은 conflict다. 이 operation은 authority owner나 source admission을 바꾸지 않는다.

Standalone Actor의 `NewOwner` Put은 exact relocation capacity fence를 반드시 포함한다. Provider는 authority CAS와
같은 transaction에서 source active capacity를 줄이고 target reserved을 active로 바꾼 뒤 fence를 committed로
닫는다. User Spot aggregate는 standalone relocation capacity fence를 사용하지 않는다. 모든 `NewOwner`
participant의 allocation delta를 합한 단일 typed capacity bundle을 aggregate prepare가 같은 transaction에서
예약하고, aggregate commit이 모든 capacity·owner·membership mutation을 함께 적용한다. Commit 전 abort는
aggregate fence가 소유한 target reserved만 해제한다. 이미 committed
fence의 abort는 closed `AlreadyCommitted`이고 다른 fence는 `Stale`다. Crash 뒤 recovery coordinator는 authority
StoreVersion·current owner token·Active allocation을 exact read해 unpublished reservation을 resume 또는 abort하며 elapsed time이나
TTL만으로 capacity를 해제하지 않는다.

Standalone `NewOwner`에 전달한 fence가 reserved 상태가 아니거나 authority key·expected StoreVersion·source·
target owner와 일치하지 않으면 current authority read를 담은 Conflict로 끝낸다. 이미 committed·aborted된 fence도
같다. CAS transaction은 source request와 durable current Active allocation의 exact match를 다시 확인하고 target
descriptor lifecycle과 target owner lease를 live/exact로 재검증한다. Source descriptor row와 source lease는
stale·missing이어도 allocation match를 대체하지 않는다. Target이 stale이면 Conflict이며 authority row, capacity와
fence state의 mutation은 0이다.

Aggregate prepare는 participant의 `OwnerTransition`에 따라 두 mode 가운데 하나로 닫힌다.

- `NewOwner` participant가 하나라도 있으면 relocation mode다. `Preserve`와 `NewOwner`를 섞을 수 있지만
  non-zero typed capacity bundle은 모든 `NewOwner` participant의 durable allocation delta를 정확히 합산해야
  한다. `Preserve` participant의 allocation은 bundle에 더하지 않고 그대로 유지한다. Standalone relocation
  capacity fence는 request에 포함하지 않는다.
- 모든 participant가 `Preserve`이면 completion·steady-normalization mode다. Capacity는 exact zero이고 모든
  `MembershipMutation`도 empty여야 한다. Capacity reservation이나 mutation 없이 exact participant set의 authority
  payload만 atomic CAS하며 owner, `ObjectGeneration`, `AuthorityOwnerGeneration`과 durable Active allocation을
  그대로 유지한다.

Provider는 relocation mode에서 모든 `NewOwner` participant authority key·expected StoreVersion, current
authority의 source owner와 aggregate target owner가 일치하는지 확인하고 source·target descriptor lifecycle과
placement allocation을 다시 검증한다. Source descriptor row·lease의 live 상태는
요구하지 않고 target descriptor lifecycle과 owner lease만 live/exact로 확인한다. Zero capacity에 `NewOwner`가
있거나 non-zero capacity인데 모든 participant가 `Preserve`이면 conflict다. All-Preserve의 non-empty membership
mutation 또는 다른 값 불일치도 conflict이며 participant, capacity와 aggregate record의
mutation은 0이다.

Relocation mode의 prepare 성공은 exact typed capacity bundle을 예약하고 `(AggregateId,
AggregateGeneration)`의 aggregate record를 `Prepared`로 전이한다. Completion·steady-normalization mode는 capacity
reservation 없이 exact request를 `Prepared`로 기록한다. Prepared aggregate의 capacity는 `AbortAggregate`만
해제할 수 있으며 standalone `AbortRelocationCapacity`의 대상이 아니다. 다른 aggregate prepare는 conflict와
mutation 0으로 끝나고 같은 aggregate generation의 exact duplicate prepare만 `AlreadyPrepared`다.
`CommitAggregate`는 relocation mode에서 source Active allocation exact match와 target descriptor lifecycle·
owner lease를 다시 확인하고 유효한 aggregate fence만 committed로 전이해 capacity를
pending-to-active로 바꾼다. Completion·steady-normalization mode에서는 exact participant expectation을 다시
확인한 뒤 payload만 atomic하게 바꾼다. `AbortAggregate`는 relocation mode에서만 aggregate의 target reserved을
해제하며 두 mode 모두 aggregate를 aborted로 닫는다.

### 7.2 Phase별 closed owner rule

| Phase | Main owner와 target rule |
|---|---|
| `Preparing`, `Captured` | Main owner는 relocation을 시작한 source로 고정된다. 이 phase에는 target attempt, target token과 target reservation이 없다. |
| `Prepared` | Main owner는 source다. 0이 아닌 exact target attempt, target owner lease, target node, target reservation과 relocation root가 모두 존재해야 한다. |
| `Committed`부터 `Completed`까지 | Main owner는 exact current target이다. 같은 target attempt, reservation과 relocation root를 계속 사용한다. |
| `Aborted` | Main owner는 source다. Abort ACK, cleanup과 steady source normalization이 끝날 때까지 application admission을 닫아 둔다. |

Spot membership을 바꾸지 않는 standalone maintenance Actor relocation의
Prepared→Committed는 aggregate가 아닌 NewOwner CAS 한 번으로 수행한다. Source
token은 terminal까지 바뀌지 않는다. Target
replacement는 target attempt, target owner lease·node와 reservation만 교체한다. Post-commit replacement는
Committed로 재진입하고 stale attempt는 completion commit과 application admission을 열 수 없다.

Application이 요청한 cross-node `JoinSpot`·`JoinEntrySpot`은 owner만 바꾸는
standalone maintenance와 다르다. Actor authority, source·target membership,
capacity와 aggregate generation을 함께 전환해야 하므로
[bounded aggregate commit](01-glossary.ko.md#bounded-aggregate-commit)을
사용한다. 이 commit 전에는 target Context의 operation과 application handler를
허용하지 않는다. Commit이 성공하면 같은 `ObjectGeneration`을 유지하고
`AuthorityOwnerGeneration`만 증가시키며, source Context의 operation을 fence한다.

Source Entry Spot에 속한 standalone Actor `Retire`는 target Entry Spot identity를 exact reservation에 고정한다.
Committed CAS는 Actor owner, AuthorityOwnerGeneration과 current target Entry Spot membership을 한 번에 바꾸며
부분 상태를 공개하지 않는다.

User Spot과 그 member Actor는 하나의 maintenance aggregate다. Aggregate ID는 non-zero 128-bit이고 participant는
최대 1024개, encoded aggregate record는 최대 1 MiB다. Spot과 각 Actor의 policy·Snapshot adapter capability를
함께 preflight하고, 하나라도 `Disabled`이거나 target capability를 충족하지 못하면 capture 전에 전체 aggregate를 차단한다. Generic
Store transaction은 Spot owner, 모든 Actor owner와 membership visibility를 같은 commit generation으로 전환한다.
Commit 전에는 partial target owner를 resolve하지 않고 commit 뒤에는 target aggregate 전체만 recovery한다.

User Spot의 initial relocation aggregate에는 `NewOwner` participant가 있으므로 relocation mode를 사용한다.
`PrepareAggregate`는 participant authority expectation 전체, target descriptor lifecycle·owner lease와 하나의
non-zero typed capacity bundle을 받는다. Bundle은 `NewOwner` participant의 durable allocation delta만 합산하며,
Spot slot 하나, 해당 User Spot stable type slot 하나와 owner가 바뀌는 participant Actor 수만큼의 Actor slot을
포함해야 한다. Provider는 participant의 current Active allocation과 bundle inventory를 검증하고 target의
Actor·Spot·Spot type reserved count를 하나의 transaction에서 증가시킨 뒤 aggregate record를 `Prepared`로 만든다.

Target factory와 Snapshot adapter의 `Restore`는 Prepared CAS 전에 staging 상태로 완료한다. Accepted journal은
이 단계에서 checksum, 순서와 fence를 검증해 target queue에 실행되지 않은 상태로 준비한다. Standalone Actor의
owner·Entry membership commit 뒤에는 target Entry Spot의 `OnActorRelocated`와 source Entry Spot의 `OnLeaveActor`를
실행한 뒤 accepted journal을 replay하고, old Entry membership과 나머지 source resource를 durable하게 cleanup한다.
Source process가 종료되면 exact source fence의 durable terminal이 source callback 완료를 대신한 뒤 replay와
cleanup을 계속한다. User Spot aggregate는 logical membership을 유지하므로 Actor
`OnJoinedActor`·`OnActorRelocated`·`OnLeaveActor` callback을 호출하지 않으며 aggregate commit 뒤 journal을 replay한다. Target은
필요한 lifecycle callback과 journal replay, source cleanup, Completed, route ACK와 steady normalization을 모두
끝내기 전에는 Ready route와 application admission을 공개하지 않는다.

### 7.3 Turn boundary, permit과 Relocation root

Preflight는 capability와 bounded headroom만 확인하며 final relocation capacity reservation을 만들지 않는다. Host `Retire`는
User Spot aggregate, standalone Actor와 Instance Spot queue에 infrastructure intent notification을 예약한다.
Application callback이나 public readiness API는 사용하지 않는다. Notification을 처리한 turn 경계에서 process의
outbound·target inbound active unit, 필요한 `Capture`·`Restore` callback과 encoded payload byte permit을
nonblocking으로 모두 얻은 unit만 source message·timer admission을 reversible하게 seal한다. Byte reservation은
Snapshot participant마다 최대 64 MiB와 현재 queue turn에서 이미 Framework가 소유한 queue·journal bytes,
timer·manifest·metadata의 deterministic encoded upper bound를 합한다. Permit을 얻지 못하면 provisional permit을
모두 반환하고 notification을 다시 예약하며 application turn을 계속 처리한다.

Process 기본 상한은 outbound 64, inbound 64, concurrent `Capture` 8, concurrent `Restore` 8과 encoded payload
in-flight 268,435,456 bytes(256 MiB)다. 단일 User Spot aggregate가 byte 상한을 넘으면 payload window가 비어
있을 때만 oversized aggregate 하나로 진행한다. Permit은 queue seal 전에 all-or-nothing으로 얻고 `Capture` 뒤
actual encoded size로만 축소한다. Oversized aggregate는 byte permit을 반환할 때까지 exclusive 상태를 유지한다.
실행 중 option 변경은 새 admission에만 적용한다.

Seal 시점에는 실행하지 않은 application message queue, accepted journal, timer logical registration과 pending tick,
application state, relocation manifest와 Framework metadata를 deterministic relocation stream에 포함한다. Native timer
handle과 callback continuation은 포함하지 않는다. Source ingress는 seal 뒤 별도 bounded hold에 넣고 payload boundary를
늘리지 않는다. Connection-bound source에서 수락한 모든 work와 모든 bound-session request는 `Captured` 전에 terminal
drain하고 journal에 넣지 않는다. Deadline 안에 끝나지 않으면 pre-`Captured` abort,
`Blocked/DeadlineExceeded`와 admission 복원으로 끝낸다. Durable journal의 모든 frozen record는 exact lease-backed
source OwnerId·LeaseGeneration을 가진다.

Preparing CAS 뒤 deterministic relocation stream을 immutable chunks와 root manifest로 쓰고 `Captured` CAS로 root를
연결한다. Exact inventory로 target offer·accept·reservation ACK를 끝내고 target factory와 Snapshot adapter
`Restore`를 완료한다. Accepted message·journal과 timer state는 application handler를 실행하지 않고 checksum,
순서와 fence만 검증해 staging queue에 준비한 뒤 `Prepared` CAS를 수행한다. Owner commit 뒤 target Framework가
logical timer를 자동 복원하고 source ingress hold를 target으로 relay한다. Precommit abort는 hold를 source queue에
arrival order로 되돌린다.

Reservation handshake의 inventory 경계는 다음과 같이 고정한다. Source의 `relocationPrepare`는 sealed participant
전체와 필요한 message·byte 수를 전달한다. Target의 첫 `relocationReady`는 수용 가능한 message·byte 용량만
offer하며 participant vector는 비워 둔다. Source의 다음 `relocationReady`가 prepare와 같은 participant 전체를
accept하고 각 allowance가 offer 안에 있는지 검증한다. Target은 이 accept를 예약한 뒤 같은 participant vector와
reservation generation을 `relocationReserved`로 반환한다. Target offer에 participant를 미리 넣거나 prepare에서
받은 inventory를 target이 임의로 줄여 accept하지 않는다.

Target은 User Spot aggregate accept에서 participant 전체의 typed capacity bundle으로 `PrepareAggregate`를 실행한다.
이 operation이 반환한 aggregate fence를 factory·Restore staging이 끝날 때까지 보관하고, 성공하면 같은 fence를
`CommitAggregate`에 전달한다. Standalone Actor와 Instance Spot accept만 `ReserveRelocationCapacity`로 별도 capacity
fence를 만든다. Aggregate에 standalone fence를 함께 만들거나 factory·Restore 뒤 capacity를 다시 예약하지 않는다.

`relocationPrepare`는 기존 object의 stable type과 authority payload를 중복 전송하지 않는다. Target은 command의
object identity와 expected authority owner generation으로 Location Store의 current authority와 Active placement
allocation을 exact하게 읽고 object kind·stable type·membership과 capacity 정보를 얻는다. 이 read가 없거나 fence가
다르면 factory 또는 staging을 시작하지 않는다. Accepted journal의 frozen record에 필요한 source·target owner lease는
ingress admission 시 Location Store와 admitted peer descriptor로 확인해 canonical record에 보존해야 하며, Node RID나
lifecycle generation에서 OwnerId·LeaseGeneration을 추정하지 않는다.

Accepted journal의 crash replay 보장은 complete root가 Captured CAS로 authority에 연결된 이후에만 성립한다.
Preparing 또는 Relocation Store Put 중 source process가 종료되면 recovery는 relocation을 fenced abort하고 연결되지 않은
Put을 orphan cleanup 대상으로 둘다. 이 구간의 original request는 일반 connection failure, timeout 또는 cancellation
terminal 계약을 따르며 accepted work replay와 hidden remote activation을 보장하지 않는다.

Relocation logical stream은 aggregate 전체와 accepted journal을 포함해 최대 256 GiB다. Adapter 하나가 반환하는
application state는 최대 64 MiB이며 Framework가 logical stream을 최대 64 MiB chunk로 나눈다. Root manifest는
최대 4096개 chunk를 가진다.
Retention은 24시간, renew threshold는 12시간이다. Staged component도 expiry를 추적하며 Captured와 Prepared CAS
직전에 complete tree의 remaining lifetime이 12시간보다 큰지 verify·renew한다. Missing 또는 partial renew는
precommit abort이고 root를 authority에 연결하지 않는다.

Application state는 adapter가 반환한 opaque bytes다. Framework는 JSON parsing, property·enum validation과
application-specific version 비교를 수행하지 않고 byte count와 relocation checksum만 검증한다.
Participant state record는 이 application bytes와 Framework recovery payload를 서로 다른 length field로
저장한다. Recovery payload에는 target이 authority mutation을 구성하는 데 필요한 sealed Session binding route와
Framework metadata가 들어가며 adapter에는 전달하지 않는다. Target은 root Spot authority와 Actor authority scan으로
participant key·stable type·current StoreVersion을 얻고, root의 object·owner generation 및 recovery payload가 current
authority와 정확히 맞을 때만 aggregate prepare를 실행한다. Command 40이나 private stage request에 같은 authority
payload를 다시 싣지 않는다.

### 7.4 Activation, route barrier와 Ready

Target은 Relocation root restore와 journal staging 중 application admission을 sealed 상태로 유지한다. Target factory와
restore callback은 attempt 사이에 at-least-once로 실행될 수 있고 stale attempt와 겹칠 수 있다. Restore가 실패한
instance는 폐기하며 새 attempt는 factory가 만든 새 instance에 같은 immutable payload를 적용한다. Callback은
retry-safe해야 하며 Framework는 external side effect의 exactly-once를 보장하지 않는다. Public callback에
RelocationId를 노출하지 않는다. Only current exact owner와 TargetAttemptGeneration만 completion을 commit할 수 있다.

Owner·membership commit 뒤 필요한 lifecycle callback과 accepted journal replay·logical timer 복원을 끝내고 standalone Actor의
old Entry membership을 포함한 source resource를 durable하게 cleanup한다. Relocation 자체는 physical·logical disconnect가
아니므로 Actor disconnect callback을 실행하지 않는다.
Framework는 `Restore`가 끝난 뒤 payload의 timer logical registration으로 target timer를 만들고 pending tick을
frozen queue ordering boundary에 맞춰 replay한다. Application이 `Capture`나 `Restore`에서 Framework timer를 중복
저장·등록하지 않는다.
`Activated` 뒤에도 target을 Ready로 publish하지 않는다. Owner·membership commit, lifecycle callback,
accepted journal replay·logical timer 복원, durable source cleanup, Completed authority CAS를 차례로 마친다. 이동한 Actor가
Session에 bind되어 있으면 Framework는 같은 ObjectGeneration을 검증하고 그 뒤에만
Session owner가 보관한 해당 Actor의 binding route, 즉 현재 Actor owner에 전달할 경로를 target owner로 갱신해 달라고 요청하고 확인을 받는다(`command 44·45`). 새
incarnation은 explicit bind가 필요하다. 같은 Session의 다른 Actor route와 physical STREAM connection은
유지한다. Maintenance authority의 steady normalization까지 마친 뒤 application packet·push admission을
열고 Ready route를 publish한다. Resolver는 relocation payload가 남은 authority를 어느 phase에서도 Ready로
투영하지 않는다.

### 7.5 Late request completion과 acknowledgement

Accepted request의 frozen record는 OperationId, exact request-source OwnerId·LeaseGeneration·node RID·generation과
original non-zero ReplyRouteId를 함께 보관한다. OperationId는 dedupe identity이며 reply route를 대신하지 않는다.
Send/event record에는 ReplyRouteId가 없다.

OperationId와 ReplyRouteId는 source owner lifecycle 안에서 non-zero unique 값이다. Wrap하거나 같은 lifecycle에서
재사용하지 않으며 소진은 terminal runtime error다. Durable terminal identity는 RelocationId, exact request-source
OwnerId·LeaseGeneration·node RID·generation과 OperationId의 조합이고 ReplyRouteId는 original correlation에만
사용한다. Completion vector는 participant sequence로 정렬하며 exact request-source fence와 OperationId 조합의
중복을 허용하지 않는다.

Late completion은 immutable completion chunk와 새 root manifest를 먼저 만든 뒤 expected StoreVersion CAS 한 번으로
authority root, checksum, TerminalCompletionCount와 PendingRelayCount를 함께 교체한다. Count는 referenced relocation root에서
계산한다. Accepted request count와 terminal completion count가 같고 PendingRelayCount가 0일 때만 Completed가 가능하다.
불일치는 recovery error다.

Target은 maintenance relocation `replyRelay`를 original route로 재전송한다. Request source는 terminal result를 처음
받으면 `terminalReceived`, 이미 terminal이면 `alreadyTerminal` 상태의 `replyRelayAck`을 보낸다. ACK sender는 relocation
source가 아니라 frozen record의 exact request source다. Target은 authenticated connection 자체를 증거로 삼지 않고
frozen source fence와 OperationId를 검증한다.

Delivery state는 `Pending → TerminalReceived | AlreadyTerminal | SourceLeaseExpired`로만 전이한다. Physical connection
close와 reconnect는 terminal proof가 아니며 current route로 relay를 계속 시도한다. Source lease expiry는 accepted
record에 저장한 exact token을 Store가 Missing 또는 stale로 확인했을 때만 인정한다. Source lease가 유효한 채 Retire
deadline을 넘으면 ForceStopped로 끝내고 relocation root와 reply bytes를 24시간 recovery horizon 동안 유지한다.

### 7.6 Source cleanup과 abort

`relocationComplete`는 durable SourceCleanupState가 `Completed`이거나 current coordinator가 immutable source token의
expiry를 exact 확인한 뒤 `SourceLeaseExpired`로 CAS한 경우에만 보낸다. Source sender면 authenticated source fence,
coordinator sender면 current coordinator fence를 검증한다. 같은 RelocationId와 cleanup state의 duplicate는 idempotent다.

Precommit abort는 source admission을 즉시 열지 않는다. Source sealed 유지 → durable Aborted CAS → Aborted phase의
session abort route와 routed ACK → reservation·relocation orphan cleanup → steady source normalization CAS → source
admission reopen 순서다. Aborted 결정 전에 abort route를 보내거나 session을 unseal하지 않는다.

## 8. Store outage와 cancellation

`StoreFailureGrace`는 descriptor reconcile과 새 outbound connect만 마지막 stable desired set으로 freeze한다. Existing
transport는 service liveness를 계속 적용한다. Grace 이후 stable descriptor snapshot을 얻기 전에는 새 connect를
만들지 않는다.

Grace는 host owner lease와 coordinator deadline을 연장하지 않는다. Shared local admission deadline에서 stateful
message, timer, factory completion, relocation CAS와 reservation을 seal한다. Recovery는 exact owner token과 stable page
set을 재검증한 뒤 diff와 connection intent를 적용한다.

Provider operation 시작 전 cancellation은 invocation과 commit을 막을 수 있다. 시작 뒤 waiter cancellation,
timeout과 provider error는 commit 여부가 불명확하다. Authority CAS는 exact key와 expected fence를 다시 읽어
reconcile한 뒤 retry한다. Content-addressed Relocation Store Put은 verify하거나 idempotent retry하고 unlinked result는
orphan으로 정리한다. Input bytes는 async completion까지 immutable·live해야 하고 provider가 더 오래 보관하면
복사한다. Success result bytes는 stable immutable snapshot이다.

Relocation Store write와 root 검증이 성공한 뒤에만 Location Store CAS로 reference를 publish한다. Root replacement도
새 root를 먼저 저장하고 검증한 뒤 reference·checksum·count를 한 CAS로 바꾼다. Cleanup은 Location authority에서
reference를 release한 다음 Relocation Store delete를 수행한다. 두 Store는 distributed transaction이나 2PC를 요구하지
않으며 다른 Redis에 배치할 수 있다.

Published authority가 가리키는 root가 일시적으로 보이지 않으면 bounded retry와 exact authority re-read를 수행한다.
Permanent missing, checksum mismatch 또는 Location participant inventory digest와 Relocation manifest digest의 불일치는
non-retriable `RelocationDataLost`다. Runtime은 authority를 error로 seal하고 commit된 owner·membership을 source로
rollback하거나 다른 root를 추측하지 않는다.

## 9. Cleanup

Host `Retire`는 `Retiring` descriptor를 먼저 게시해 새 placement·membership과 inbound relocation target을 닫는다.
기존 owner의 stateful message와 timer는 unit별 permit을 얻은 turn 경계까지 유지한다. 모든 unit이 source dispatch에서
분리된 뒤 `Draining`으로 전환하고 late reply relay와 STREAM route barrier를 deadline까지 진행한 다음 exact host
token의 descriptor를 제거한다. `Shutdown`은 `Retiring` 없이 바로 admission을 seal하고 `Draining`으로 전환한다.
`RemoveAllByOwner`는 durable authority를 제거하지 않는다. Authority는 explicit expected StoreVersion과 owner fence를
가진 delete만 허용한다.

Deadline을 넘으면 terminal ForceStopped result를 한 번 완료한다. Timer, Store callback, reconnect work와 observer가
runtime-owned resource보다 늦게 남지 않는다.

## 10. 구현 및 contract test 검증 요구

- ObjectGeneration, AuthorityOwnerGeneration과 OwnerLeaseGeneration이 서로 다른 fence로 동작한다.
- Provider counter가 `2^63-1`이면 `GenerationExhausted`가 atomic no-write·no-consume로 반환되고 반복해도 같다.
- 모든 Location host가 automatic RID 사용 여부와 무관하게 owner lease timing relation을 startup에서 검증한다.
- Shared local owner lease deadline이 descriptor, object, timer와 relocation authority를 함께 seal한다.
- Descriptor overflow가 partial publish 없이 startup을 실패시킨다.
- Authority Read Missing이 synthetic StoreVersion을 만들지 않고 authority row에 TTL이 없다.
- Opaque 4096-byte scan cursor, 1000-item·4-MiB page와 snapshot consistency가 유지된다.
- Actor·Spot global key가 MeshName과 독립적으로 같은 authority row에 수렴한다.
- `Create`와 `GetOrCreate`가 동시에 실행되어도 같은 Creating attempt로 수렴하고,
  생성 권한을 얻지 못한 target은 별도 factory를 시작하지 않는다.
- 서로 다른 operation은 Ready 뒤 Existing을 받고 cleanup 뒤 새 reservation을 경쟁하며,
  같은 source lifecycle·OperationId의 재전송만 correlation-free terminal envelope를 replay한다.
- Created terminal publish가 Ready authority와 active capacity를 함께 확정하고,
  Rejected는 Ready를 공개하지 않은 채 reserved capacity를 반환한다.
- Terminal result는 original deadline 뒤 5분 동안 같은 operation에서 회수할 수 있고,
  TTL 뒤 Ready authority가 없으면 새 ReservationId로 생성할 수 있다.
- Remote User Spot create·close가 command 47·48의 source·target lifecycle, operation
  identity, reservation·`StoreVersion`과 exact generation fence를 검사하고 command
  20으로 terminal result를 한 번만 반환한다.
- Generic reservation이 Creating authority와 reserved capacity를 atomic하게 reserve·commit·abort한다.
- Authority snapshot의 provider-owned allocation이 Reserved·Active state, kind·stable type, descriptor
  lifecycle과 capacity delta를 빠짐없이 반환하고 opaque payload와 중복되지 않는다.
- Relocation reservation과 commit이 source request를 durable Active allocation에 exact-match하며 source
  descriptor·lease가 stale이어도 recovery를 허용하고 target descriptor·lease가 stale이면 no-write로 막는다.
- Aggregate prepare의 relocation mode가 `NewOwner` participant의 exact fence와 non-zero capacity bundle만
  aggregate generation에 atomic하게 bind하고, all-Preserve completion·steady-normalization mode는 zero capacity와 empty
  membership mutation에서 owner·generation·Active allocation을 유지한 채 payload만 atomic하게 바꾼다.
- Zero capacity와 `NewOwner`, non-zero capacity와 all-Preserve 조합을 conflict와 mutation 0으로 거부한다.
- Delete가 live current owner lease와 Active allocation을 검증하고 exact active capacity delta를 같은
  transaction에서 감소시킨다.
- Creation request reference와 hash가 Ready 또는 fenced abort까지 유지되고 factory가 retry-safe하게 재개된다.
- 일반 message와 find가 Missing Instance Spot을 hidden create하지 않는다.
- Instance-intent source가 owner claim을 선점하지 않고 first-message activation envelope를 target에 제출한다.
- Instance Spot 생성 권한을 얻은 target만 reservation과 factory를 실행하고 durable
  inbox 첫 record를 `Ready` 전에 확정한다. `Ready` recovery pointer를 유지한 채
  첫 record를 local queue 선두에 복원한 뒤 barrier를 연다.
- Missing, Creating과 Store failure를 cache하지 않고 Ready cache가 owner admission deadline을 넘지 않는다.
- Forwarding chain이 8 hops, mapping별 1024 message·16 MiB bound와 generation 증가를 검증한다.
- Preflight가 final reservation을 만들지 않고 queue turn 경계에서 모든 permit을 얻은 unit만 seal해 Prepared를 만든다.
- 기본 outbound·inbound 64, `Capture`·`Restore` 8, payload in-flight 256 MiB gate가 독립적으로 적용되고 oversized
  User Spot aggregate는 단독으로 진행된다.
- Frozen payload가 미실행 message·journal·timer registration·pending tick을 포함하고 target Framework가 timer를
  자동 복원한다.
- Seal 뒤 ingress hold가 precommit abort에서는 source queue로 복원되고 commit 뒤에는 target으로 relay된다.
- Preparing/Captured/Prepared/Committed..Completed의 main owner와 target field closed rule을 위반한 CAS가 실패한다.
- Target replacement가 stable RelocationId와 relocation root를 유지하며 stale attempt의 commit과 admission을 막는다.
- Target은 Activated·Cleaning·Completed에서도 sealed이며 route ACK와 steady normalization 뒤에만 Ready다.
- Frozen request가 OperationId, exact source fence와 ReplyRouteId를 보존하고 send에는 reply route가 없다.
- Standalone Actor `Retire`가 source Entry Spot membership을 target Entry Spot membership과 atomic하게 전환한다.
- User Spot aggregate가 bounded participant record를 사용하고 Spot·Actor owner와 membership을 한 commit
  generation에서 전환한다.
- Completion root와 authority count가 atomic하게 일치하고 ACK 또는 exact source lease expiry 전에는 relocation root를
  release하지 않는다.
- Relocation root write·verify가 Location CAS보다 먼저이고 reference release가 root delete보다 먼저다.
- Published root의 permanent missing·checksum mismatch·inventory digest mismatch가 `RelocationDataLost`로 seal되며
  source rollback을 시작하지 않는다.
- Physical connection close가 request terminal proof로 사용되지 않는다.
- Aborted CAS 전에 session abort route와 source reopen이 발생하지 않는다.
- StoreFailureGrace가 discovery만 freeze하고 authority deadline을 연장하지 않는다.
- Commit 성공·response loss·cancellation race를 exact read 또는 idempotent Put으로 reconcile한다.
