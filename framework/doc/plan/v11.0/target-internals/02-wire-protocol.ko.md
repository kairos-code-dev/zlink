# Service wire protocol

[Runtime architecture](01-runtime-architecture.ko.md) ·
[Schema](../../../../runtime/protocol/service-wire-v1.schema.json)

## 1. Schema authority

`framework/runtime/protocol/service-wire-v1.schema.json`은 service wire와 durable format의 단일 생성 입력이다. Wire
major, command ID, flag, field order, integer width, bound, conditional tail과 semantic constraint를 이 파일에서
고정한다. C++·.NET·JVM·Node.js runtime은 생성한 상수와 codec을 사용하며 같은 숫자와 layout을 별도 source에
정의하지 않는다.

Generator와 fixture builder는 schema를 읽은 직후 validator를 실행한다. 저장소 gate는 다음 command다.

```bash
node framework/runtime/protocol/validate-service-wire-schema.mjs \
  --self-test framework/runtime/protocol/service-wire-v1.schema.json
```

Validator는 undefined type, duplicate ID, unreferenced bound, unsafe JSON integer, invalid flag condition, durable
format과 golden fixture 불일치를 거부한다. Schema와 golden binary가 충돌하면 구현이 아니라 schema review에서
해결한다.

## 2. Record framing

Protocol magic은 ASCII `ZM`, wire major는 `1`, byte order는 big-endian이다. Frame 0은 5-byte head prefix와 command
body다. Metadata flag가 있으면 다음 frame에 metadata를 두고 payload command는 typed application payload envelope를
한 frame에 둔다. ROUTER identity는 raw binding transport envelope이며 service frame에 복사하지 않는다.

```text
+------------------------------------------+
| Frame 0: Prefix and Command Body         |
+------------------------------------------+
| Frame 1: Metadata when Flagged           |
+------------------------------------------+
| Next: Typed Application Payload          |
+------------------------------------------+
```

Head prefix는 magic 2 byte, wire major, command ID와 flags 순서다. Flag는 metadata, bound session, source Spot RID와
extension 네 bit만 허용한다. Command별 required·allowed flag와 frame cardinality는 schema가 정한다. Decoder는
allocation 전에 frame 수, declared length, count와 upper bound를 확인하고 invalid UTF-8, unknown enum, trailing byte와
허용하지 않은 flag를 전체 record protocol error로 거부한다.

Application payload는 packet name, content type과 bytes로 구성한 `application-payload-envelope-v1`만 사용한다.
Payload format은 exact `framework-json-v1` typed contract이며 각 runtime이 편의상 다른 JSON 표현을 wire에 내보내지
않는다.

## 3. Command registry

Command ID는 다음 closed registry다.

| ID | command | ID | command | ID | command |
|---:|---|---:|---|---:|---|
| 1 | `hello` | 23 | `logicalMulticast` | 36 | `boundSessionSend` |
| 2 | `admit` | 24 | `actorSend` | 37 | `actorJoined` |
| 3 | `reject` | 25 | `actorRequest` | 38 | `boundSessionBind` |
| 4 | `update` | 26 | `actorLookup` | 39 | `instanceSpot` |
| 5 | `livenessProbe` | 27 | `actorDestroy` | 40 | `transferPrepare` |
| 6 | `livenessAck` | 28 | `actorJoin` | 41 | `transferReserved` |
| 16 | `nodeSend` | 29 | `actorLeft` | 42 | `sessionTransferSeal` |
| 17 | `nodeRequest` | 30 | `transferReady` | 43 | `sessionTransferSealed` |
| 18 | `channelSend` | 31 | `transferData` | 44 | `sessionTransferRoute` |
| 19 | `channelRequest` | 32 | `transferAck` | 45 | `sessionTransferRouted` |
| 20 | `reply` | 33 | `replyRelay` | 46 | `replyRelayAck` |
| 21 | `spotSend` | 34 | `transferSeal` |  |  |
| 22 | `spotRequest` | 35 | `transferComplete` |  |  |

Reserved range와 향후 command 추가 규칙은 schema의 `reservedCommandRanges`가 소유한다. Unknown command와 기존 ID의
재해석은 protocol error다.

## 4. Admission과 liveness

`hello`·`admit`·`reject`·`update`가 physical connection의 service admission을 정한다. Descriptor revision은 host가
발급한 non-zero monotonic value다. Maximum 뒤에는 wrap하지 않고 host를 Error로 seal한다. Manual endpoint lifecycle은
non-zero opaque token equality와 current physical connection만 확인하며 numeric ordering을 적용하지 않는다.

Admitted connection은 application traffic과 무관하게 5초마다 `livenessProbe`를 보낸다. Outstanding probe는 하나다.
없으면 새 non-zero connection-local ID를 만들고, 있으면 같은 ID를 재전송한다. 같은 current connection에서 current
outstanding ID와 일치하는 첫 `livenessAck`만 outstanding을 지우고 15초 peer deadline을 갱신한다. Duplicate·stale·
다른 connection ACK와 application traffic은 diagnostic만 갱신하며 deadline을 연장하지 않는다. Orderly disconnect는
deadline을 기다리지 않고 즉시 not-ready로 전환한다.

Fanout publisher는 application publish와 무관한 periodic beacon을 보낸다. Subscriber는 beacon과 current connection
identity로 publisher liveness를 판정한다. Application frame 수신을 beacon으로 간주하지 않는다.

## 5. Authority fence

Store-backed object command는 ObjectGeneration, AuthorityOwnerGeneration, exact OwnerId·OwnerLeaseGeneration과
필요한 StoreVersion-derived route snapshot을 보존한다. Actor current Spot은 authority의 일부이며 별도 membership
counter가 없다. User Spot과 Actor는 `Creating → Ready`, Instance Spot은 `ColdActivating → Ready` publication barrier를
사용한다. Resolver와 remote messaging은 Ready만 사용한다.

Store-less command는 runtime-local opaque lifecycle token과 current physical connection equality만 사용한다. 이
token에 greater-than ordering을 적용하거나 remote transfer authority로 사용하지 않는다.

## 6. Maintenance identity와 frozen record

`TransferId`는 runtime이 CSPRNG로 만든 non-zero 128-bit stable identity다. Active transfer와 retained checkpoint
root에서 collision을 검사하고 충돌하면 다시 생성한다. `TargetAttemptGeneration`은 같은 transfer의 current target
attempt만 fence한다. Target replacement는 TransferId와 checkpoint root를 바꾸지 않는다.

Durable frozen journal은 exact lease-backed source에서 accept한 record만 포함한다. Record는 source
OwnerId·OwnerLeaseGeneration, source node fence, participant, sequence와 operation identity를 보존한다. Request에는
source lifecycle에서 unique한 non-zero OperationId와 ReplyRouteId가 있고 send에는 reply route가 없다. 두 ID는 같은
source lifecycle에서 wrap하거나 재사용하지 않는다. Connection-bound accepted send·request와 bound-session request는
Captured 전에 terminal drain하므로 frozen record kind가 될 수 없다.

`TransferData` deduplication은 TransferId, TargetAttemptGeneration, participant와 sequence를 사용한다. Durable
terminal identity는 TransferId와 OperationId다. `replyRelayAck`은 `terminalReceived`와 `alreadyTerminal`을 구분한다.
Physical disconnect는 terminal proof가 아니며 exact source lease expiry만 `sourceLeaseExpired`를 확정한다.

## 7. Durable authority와 checkpoint

Authority envelope, checkpoint chunk, root manifest와 logical stream의 exact byte layout은 schema의
`durableFormats`와 `checkpointLogicalStreamFormat`이 소유한다. Golden fixture가 magic, version, flags, length,
field order와 CRC32C를 검증한다. Provider는 envelope bytes를 opaque하게 저장한다.

Preparing 중 checkpoint Put은 아직 durability boundary가 아니다. Complete root를 authority에 연결한 Captured CAS가
accepted journal crash recovery의 시작점이다. Captured 전 crash는 fenced abort이고 unlinked data는 orphan cleanup
대상이다. Captured 뒤에는 checkpoint와 participant replay cursor로 recovery한다.

Actor Committed authority는 owner, AuthorityOwnerGeneration과 target Entry Spot membership을 atomic하게 바꾼다.
Target은 factory·restore, target `OnJoined`와 replay를 수행하고 source는 `OnLeave`와 old Entry cleanup을 durable하게
확정한다. `Activated` 뒤에도 target은 sealed 상태다. Completed, route ACK와 steady normalization까지 끝난 뒤에만
Ready와 application admission을 공개한다.

## 8. Codec 처리 규칙

Encoder는 schema field order와 exact conditional tail만 출력한다. Decoder는 다음 순서를 지킨다.

1. Prefix, command와 flags를 검증한다.
2. Fixed-width field와 declared length가 complete frame 안에 있는지 확인한다.
3. Count와 bound를 확인한 뒤 allocation한다.
4. Closed enum, sorted vector, duplicate key와 cross-field semantic constraint를 확인한다.
5. Current connection, owner lease, object authority와 transfer attempt를 검증한다.
6. 모든 검증이 끝난 뒤 infrastructure 또는 application queue에 admission한다.

Malformed record는 partial state, reply route, transfer staging과 application callback을 만들지 않는다. Same
deduplication key의 identical record는 schema가 허용한 command에서만 idempotent하며 다른 bytes는 protocol error다.

## 9. 검증 기준

- 네 runtime의 generated command ID, flag와 bound가 schema와 byte-for-byte 일치한다.
- Golden fixture decode와 re-encode가 동일한 bytes를 만든다.
- Probe schedule, same-ID retry와 matching ACK deadline refresh가 application traffic과 독립적이다.
- Manual lifecycle token을 numeric generation처럼 비교하지 않는다.
- Frozen journal에 connection-bound record가 포함되지 않는다.
- Captured CAS 전 crash를 durable replay로 오인하지 않는다.
- Actor owner와 target Entry membership이 같은 authority commit에 포함된다.
- `Activated`만으로 Ready route와 application admission을 열지 않는다.
