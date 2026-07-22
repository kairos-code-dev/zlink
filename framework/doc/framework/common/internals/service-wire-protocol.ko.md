# Service wire protocol

이 문서는 RouteMesh 11.0 목표 구조를 설명한다. 현재 구현과의 차이와 완료 상태는
`framework/doc/plan/v11.0/route-mesh-11.0.0-execution-ledger.ko.md`가 소유한다.

[내부 구조 목차](README.ko.md) · [Runtime architecture](service-runtime-architecture.ko.md) ·
[Location runtime](../../spec/server/40-location-runtime.ko.md) ·
[Redis Transfer Store](../../spec/server/42-transfer-store-redis.ko.md) ·
[Transport liveness](../../spec/server/55-transport-liveness.ko.md)

## 1. Schema와 생성 경계

`framework/runtime/protocol/service-wire-v1.schema.json`은 Framework service wire의 단일 생성 입력이다. 이
schema가 command ID, frame layout, enum 값, field bound, durable format과 semantic constraint를 고정한다.
C++·.NET·JVM·Node.js runtime은 schema에서 상수와 codec table을 생성하며 같은 값을 source에 다시 정의하지
않는다.

생성기와 fixture builder는 파일을 만들기 전에 validator를 실행한다.

```bash
node framework/runtime/protocol/validate-service-wire-schema.mjs \
  --self-test framework/runtime/protocol/service-wire-v1.schema.json
```

Wire major는 `1`이고 required capability는 `framework-service-v11`이다. Schema와 golden fixture가 다르거나
validator가 undefined type, 중복 ID, 잘못된 enum·bound·conditional field를 발견하면 build를 중단한다.

Location Store canonical authority key도 같은 schema와 golden fixture가 고정한다. Actor key는
`zla1:a:<byte-length>:<encoded-ActorId>`, Spot key는 `zla1:s:<byte-length>:<encoded-SpotRid>` 형식이다.
MeshName은 key에 포함하지 않으며 authority payload의 current placement attribute로만 저장한다. Percent encoding은
RFC 3986 unreserved byte만 그대로 두고 나머지는 uppercase hex로 표현한다.

## 2. Record framing과 decode

ROUTER routing identity는 raw binding이 소비하는 transport envelope다. Service codec은 이를 application frame에
복사하지 않는다. Service record는 다음 순서의 multipart로 구성한다.

```text
+------------------------------------------+
| Frame 0: Head Prefix and Command Body    |
+------------------------------------------+
| Frame 1: Metadata when flag 0x01         |
+------------------------------------------+
| Next: Typed Payload Envelope if allowed  |
+------------------------------------------+
```

Frame 0의 prefix는 `Z`, `M`, wire major, command ID, flags 순서다. Multi-byte integer는 network byte order다.
Metadata, bound session, source Spot RID와 extension flag는 schema가 허용하거나 요구한 command에서만 사용할 수
있다. 정의하지 않은 flag, frame 수, conditional tail 또는 trailing byte가 있으면 application dispatch 전에
protocol error로 거부한다.

Decoder는 allocation 전에 complete record 길이, item count, UTF-8 validity와 모든 bound를 검사한다. Metadata
frame은 1,024 byte를 넘을 수 없다. Application payload의 schema 절대 상한은
`applicationPayloadAbsoluteBytes`인 4,294,966,774 byte다. 실제로 허용하는 payload 크기는 이 절대 상한과
`normalizedEffectiveMaxMessageBytes`에서 실제 envelope overhead를 뺀 값 중 작은 값이다. Application payload에는
별도의 숨은 16 MiB 고정 상한을 적용하지 않는다.

Complete message 상한은 startup admission에서 정한다. Sender는 local과 remote의
`normalizedEffectiveMaxMessageBytes` 중 작은 값을 사용하고 receiver는 자신의 admitted 상한을 사용한다. 이 값은
admitted connection lifetime 동안 바꿀 수 없으며, allocation 전에 적용한다. 양쪽 상한이 32 MiB이면 complete
message가 32 MiB 이내인 17 MiB payload를 허용한다.

Typed payload는 packet name, contract 정보와 serializer payload를 하나의 envelope로 보존한다. Application code에
raw frame 조합, codec table 또는 maintenance field를 노출하지 않는다.

## 3. Command space

Wire v1은 다음 37개 command를 사용한다. `7..15`와 `47..255`는 reserved이며 다른 의미로 재사용하지 않는다.

| ID | Command | 역할 |
|---:|---|---|
| 1 | `hello` | local admission descriptor 제안 |
| 2 | `admit` | selected connection 승인 |
| 3 | `reject` | admission 거부 |
| 4 | `update` | admitted descriptor revision 갱신 |
| 5 | `livenessProbe` | 현재 connection의 round-trip 확인 |
| 6 | `livenessAck` | 같은 probe ID 응답 |
| 16 | `nodeSend` | node one-way |
| 17 | `nodeRequest` | node request |
| 18 | `channelSend` | channel one-way |
| 19 | `channelRequest` | channel request |
| 20 | `reply` | request terminal result |
| 21 | `spotSend` | Spot one-way |
| 22 | `spotRequest` | Spot request |
| 23 | `logicalMulticast` | logical multicast |
| 24 | `actorSend` | Actor one-way |
| 25 | `actorRequest` | Actor request |
| 26 | `actorLookup` | Actor route lookup |
| 27 | `actorDestroy` | Actor destroy coordination |
| 28 | `actorJoin` | Actor membership proposal |
| 29 | `actorLeft` | Actor leave commit |
| 30 | `transferReady` | capacity offer와 inventory accept |
| 31 | `transferData` | frozen record 전달 |
| 32 | `transferAck` | participant high-water ACK |
| 33 | `replyRelay` | terminal completion relay |
| 34 | `transferSeal` | participant terminal seal |
| 35 | `transferComplete` | target finalization 알림 |
| 36 | `boundSessionSend` | bound STREAM session egress |
| 37 | `actorJoined` | Actor join commit |
| 38 | `boundSessionBind` | session binding commit |
| 39 | `instanceSpot` | logical Instance Spot operation |
| 40 | `transferPrepare` | exact sealed inventory 제안 |
| 41 | `transferReserved` | target reservation ACK |
| 42 | `sessionTransferSeal` | session ingress seal 요청 |
| 43 | `sessionTransferSealed` | session high-water 응답 |
| 44 | `sessionTransferRoute` | session route 교체 요청 |
| 45 | `sessionTransferRouted` | session route 교체 ACK |
| 46 | `replyRelayAck` | relayed terminal result ACK |

Command별 body, metadata·payload 허용 여부와 direction은 schema의 closed definition을 따른다. 알 수 없는 command,
반대 direction의 infrastructure command와 topology에서 허용하지 않은 command는 application queue에 넣지 않는다.

## 4. Admission과 connection fence

RouteMesh와 ClientServer는 `hello → admit|reject`로 current physical connection을 service route로 승인한다. Manual
구성의 lifecycle token은 CSPRNG로 만든 non-zero opaque equality token이다. 숫자 크기로 새 값을 판단하지 않으며
current physical connection의 handover와 liveness로 이전 token을 차단한다. Store-backed peer는 exact host owner
lease도 함께 검사한다.

`DescriptorRevision`만 같은 lifecycle에서 strictly increasing ordering을 가진다. 같은 revision의 같은 bytes는
idempotent하고, 같은 revision의 다른 bytes나 낮은 revision은 protocol error다. `update`가 바꿀 수 있는 값은
기존 channel weight, runtime state, placement capacity와 maintenance wave뿐이다. RID, topology, security identity,
capability, application version과 normalized message 상한은 connection을 다시 admit해야 바뀐다.

ClientServer connection은 ChannelName 하나와 client-to-server 방향을 고정한다. Client는 send·request와 liveness
command만 보내고 server는 reply, liveness, update와 reject만 보낸다. RouteMesh record를 ClientServer connection에
재사용하거나 반대로 재사용하면 protocol error다.

## 5. Service liveness

Admission이 성공하면 peer timeout deadline을 시작한다. Runtime은 application traffic과 관계없이 5초마다
`livenessProbe` tick을 실행한다. Outstanding probe가 없으면 current connection에서 유일한 non-zero `u64` ID를
만들어 보내고, 이미 있으면 같은 ID를 다시 보낸다. Connection마다 outstanding ID는 하나뿐이다.

현재 physical connection에서 current outstanding ID와 일치하는 첫 `livenessAck`만 15초 deadline을 새로 시작하고
outstanding ID를 지운다. 이전 ID, 중복 ACK, 다른 connection의 ACK와 다른 inbound traffic은 diagnostic activity로만
기록하며 deadline을 연장하지 않는다. Orderly disconnect와 raw transport failure는 deadline을 기다리지 않고
즉시 not-ready로 전환한다. Probe, ACK와 timer는 infrastructure reserve에서 처리하며 application queue나 handler에
전달하지 않는다.

Classic fanout publisher는 ACK를 받을 수 없으므로 별도 beacon을 5초마다 보낸다. Beacon은 application publish
traffic과 관계없이 주기적으로 전송한다.

```text
Topic:   01 5A 4C 46 31
Payload: 5A 46 01 01
```

Subscriber는 publisher마다 전용 SUB socket을 사용한다. 첫 valid application record 또는 exact beacon에서 Ready가
되고, 마지막 valid receive 뒤 15초가 지나면 해당 publisher만 not-ready로 바꾼다. Reserved topic의 frame 수나
payload가 정확하지 않으면 즉시 protocol error다. Public topic derivation 결과가 exact reserved topic이면 transport
전 application argument 또는 configuration error로 거부한다.

## 6. `framework-json-v1`

Application payload와 Snapshot state는 같은 typed JSON profile을 사용한다. Runtime은 다음 규칙을 모든 언어에
같게 적용한 뒤 원본 UTF-8 bytes를 전달하거나 transfer envelope에 보존한다.

- UTF-8 BOM은 허용하지 않는다. Property name과 enum name은 대소문자를 구분한다.
- Property 순서와 의미 없는 whitespace는 의미가 없다. 중복 property와 누락된 required property는 거부한다.
- Reader는 알 수 없는 property를 무시한다. `null`은 contract가 nullable로 선언한 값에만 허용한다.
- Signed·unsigned 64-bit integer는 범위를 확인한 canonical decimal string이다. 32-bit 이하 integer는 fraction이
  없는 JSON number다.
- Floating-point 값은 finite JSON number만 허용한다. Byte sequence는 padding을 포함한 RFC 4648 base64다.
- Date, decimal, UUID와 언어별 custom type은 암묵적으로 변환하지 않고 contract가 정한 string 또는 DTO로 표현한다.

`StateContractId`는 호환되는 typed Snapshot adapter를 선택한다. Serializer를 application option으로 선택하는
값이 아니다.

## 7. Durable authority와 explicit creation

Store-backed authority는 provider가 발급한 `StoreVersion`, `ObjectGeneration`, `AuthorityOwnerGeneration`과 current
host의 `OwnerId`, `OwnerLeaseGeneration`을 분리해 보존한다. Object generation은 delete 뒤 같은 canonical key로
새 object를 만들 때만 바뀐다. Authority owner generation은 같은 object의 owner 변경을 fence한다. Host owner
lease token은 process 전체가 공유한다.

Actor와 User·Instance Spot manager create는 generic reservation 뒤 `NewObject` CAS로 final object·owner generation과
`Creating` row를 만든다. Creation record는 object kind, global key, stable type, target descriptor, capacity delta,
fence와 최대 1 MiB request content reference·hash를 보존한다. Factory, initialize와 initial membership이 끝나야
같은 fence로 reservation commit과 `Ready` CAS를 수행한다. Manager `Find`와 ID-only messaging은 `Ready`만 사용한다.
Entry Spot은 startup initialization 뒤 host가 `Serving`이 되기 전에 publish하며 caller가 생성하지 않는다.

Factory 실패는 local barrier를 failed 상태로 seal하고 waiting request를 한 번만 terminal 처리한다. One-way
operation은 drop event를 기록한다. Runtime은 exact Store version, object·owner generation과 owner lease로 row를
삭제하고 ambiguous 결과를 read로 reconcile한다. Local registry는 `Missing`을 확인할 때까지 failed 상태를
유지하며, 그 다음 caller만 새 `NewObject`를 시작할 수 있다.

Object `Client`와 `Server` role은 Location Store를 요구한다. Object `None` role은 authority와 hidden local
runtime을 만들지 않는다.

## 8. Instance Spot reactivation

Normal Instance send·request는 global SpotRid만 포함하며 create command가 아니다. Runtime이 사라진 durable
Instance authority는 stored creation intent로 reactivation한다. Target host는 startup initial scan과 bounded
background scan에서 자신이 소유한 `Creating` 또는 reactivation authority를 재개한다. Scan과 late control record는
object key, object·owner generation과 owner lease로 key를 정한 local barrier 하나로 수렴한다. Original application
payload를 hidden replay하지 않는다.

Reactivation 실패는 local barrier를 seal하고 request를 한 번만 terminal 처리한 뒤 one-way drop event를 기록한다.
그 다음 exact fenced delete와 read reconcile을 수행한다. Delete 전 process가 종료되면 target scan은 retry-safe
factory를 다시 실행할 수 있다. `Missing`이 확인되기 전에는 새 activation을 시작하지 않는다.

## 9. Maintenance capture와 transfer envelope

Retire seal은 accepted boundary를 고정한다. Source lifetime이 `connectionBound`인 accepted send·request와 모든
bound-session request는 `Captured` 전에 terminal state까지 drain한다. 이 work는 frozen journal에 기록하지 않는다.
Deadline 안에 끝나지 않으면 transfer를 pre-Captured에서 abort하고 `Blocked/TransferDisabled`로 끝낸 뒤 source
admission을 복원한다.

Durable frozen record는 `leaseBacked` source만 허용한다. 각 record는 exact source node lifecycle과
`OwnerId`·`OwnerLeaseGeneration`을 포함하며 replay 전 current authority와 비교한다. Connection lifetime에만 묶인
record를 transfer envelope에 넣는 것은 protocol error다.

Framework는 accepted journal과 optional application state를 deterministic `transfer-envelope-v1` stream으로
encode한다. 모든 immutable chunk를 쓰고 root manifest를 쓴 다음 authority의 `Captured` CAS로 root를 연결한다.
이 CAS가 durability boundary다. `Captured` 전에 source가 종료되면 transfer를 abort하며 continuity replay를
보장하지 않는다. CAS에 연결되지 않은 chunk와 manifest는 orphan이다.

Location Store authority는 phase, `TransferId`, source·target fence, root reference·checksum, bounded canonical
participant set·mutation·aggregate generation·inventory digest와 replay·completion count를 원자적으로 CAS한다.
Transfer Store manifest는 participant별 payload를 찾기 위한 같은 inventory digest의 projection이며 owner와
membership authority가 아니다. 두 Store는 distributed transaction이나 2PC를 사용하지 않는다.

Transfer root retention은 24시간이고 renew threshold는 12시간이다. `Captured`와 `Prepared` CAS 직전에 complete
tree가 threshold보다 오래 유지되는지 확인하거나 renew한다. Reader는 current authority가 가리키는 root만 읽고
chunk checksum과 전체 checksum을 streaming으로 검증한다.

## 10. Transfer, Actor membership과 Ready

`TransferId`는 runtime이 CSPRNG로 만든 non-zero 128-bit 값이다. Active transfer와 retained transfer root의 ID가
충돌하면 다시 만들며 application에 노출하지 않는다. 같은 transfer에서 target을 바꿀 때는 stable `TransferId`와
transfer root를 유지하고 `TargetAttemptGeneration`만 증가시킨다.

Authority phase는 다음 순서와 closed owner rule을 따른다.

```text
Preparing -> Captured -> Prepared -> Committed -> Activating
                                               -> Activated
                                               -> Cleaning
                                               -> Completed
Preparing..Prepared -> Aborted
```

`Preparing`과 `Captured`의 main owner는 source이고 target reservation은 없다. `Prepared`는 source owner와 exact
target attempt·reservation을 함께 보존한다. `Committed`부터 `Completed`까지 main owner는 current target이다.
각 transition은 expected `StoreVersion` CAS다. Target replacement는 target attempt, target owner lease와 reservation만
바꾸며 stable identity와 transfer root를 바꾸지 않는다.

User Spot과 member Actor transfer는 non-zero 128-bit aggregate ID와 exact participant inventory를 사용한다.
Participant는 최대 1024개이고 encoded aggregate는 최대 1 MiB다. Target offer는 Spot과 member Actor의 global
identity, ObjectGeneration, kind와 capacity reservation을 고정한다. `Committed` CAS는 aggregate owner와 membership
visibility를 원자적으로 바꾼다. Target은 factory·restore, joined callback과 journal replay 순서로 처리한다. Source는
leave callback과 old membership cleanup을 durable하게 끝낸다.

`Activated`는 Ready가 아니다. Target application admission은 durable source cleanup, `Completed` CAS, bound-session
route ACK와 steady authority normalization이 모두 끝날 때까지 닫혀 있다. Abort도 source route ACK와 steady source
normalization이 끝난 뒤 admission을 복원한다.

## 11. Request terminal identity

`OperationId`와 `ReplyRouteId`는 source owner lifecycle 안에서 각각 unique한 non-zero 값이다. Wrap과 reuse는 terminal
runtime error다. Operation ID는 deduplication identity이고 reply route를 대신하지 않는다. Durable terminal
identity는 stable `TransferId`와 `OperationId` 조합이다.

Target은 terminal completion과 delivery state를 새 immutable transfer root에 쓴 뒤 authority CAS로
`TerminalCompletionCount`와 `PendingRelayCount`를 함께 갱신한다. `replyRelay`는 original reply route와 exact request
source lease fence를 사용한다. Source는 terminal result를 수락하거나 이미 terminal임을 확인한 뒤 authenticated
`replyRelayAck`을 보낸다. Physical connection close는 terminal delivery의 증거가 아니다.

`Completed`는 accepted request count와 terminal completion count가 같고 pending relay가 0일 때만 허용한다. Source
lease가 유효한 동안 ACK를 확인하지 못하면 Retire는 transfer root와 reply bytes를 retention 동안 보존한 채
`ForceStopped`로 끝난다.

Root replacement는 새 immutable root의 reference·checksum·inventory digest를 검증한 뒤 authority CAS로 연결한다.
Conflict loser root는 orphan으로 정리한다. Cleanup은 Location authority에서 reference를 release한 뒤 Transfer
Store delete를 수행한다. Published reference의 permanent missing, checksum mismatch 또는 inventory digest mismatch는
non-retriable `TransferDataLost`이며 commit된 owner·membership을 source로 rollback하지 않는다.

## 12. 구현 검증

- 생성 결과와 checked-in codec table이 schema와 일치한다.
- 모든 decoder가 allocation 전에 complete length, count, enum, flag와 topology direction을 검사한다.
- Manual lifecycle token을 숫자 순서로 비교하지 않고 `DescriptorRevision`만 ordering에 사용한다.
- Application traffic이 probe round-trip deadline을 연장하지 않는다.
- Connection-bound accepted work가 transfer envelope에 들어가지 않는다.
- `Captured` CAS 전 crash를 durable replay로 처리하지 않는다.
- Transfer root write·verify가 authority CAS보다 먼저이고 authority reference release가 root delete보다 먼저다.
- Location participant digest와 Transfer manifest digest mismatch가 `TransferDataLost`로 끝난다.
- Actor transfer commit이 owner와 target Entry Spot membership을 atomic하게 바꾼다.
- `Activated`에서 Ready를 publish하지 않는다.
- `framework-json-v1` golden fixture가 네 runtime에서 같은 typed value와 failure를 만든다.
- `replyRelayAck` 없이 physical disconnect만으로 pending relay를 완료하지 않는다.
