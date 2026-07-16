<!-- framework-adapter-nav:start -->
[스펙 목차](../README.ko.md) | [이전: Location Runtime](40-location-runtime.ko.md) | [다음: 런타임 모니터링](50-runtime-monitoring.ko.md)
<!-- framework-adapter-nav:end -->


# Location Store — 공식 Redis Extension

이 문서는 ZLink Framework 10.0.0이 공식 제공하는 **Redis location store extension**의 언어 중립
공개 계약이다. 이 문서는 “Redis extension이 MeshNode discovery, Spot·Actor location과 Actor transfer
authority를 어떤 원자성으로 제공하는가?”라는 질문에 답한다. Store, lease와 generation의 의미는
[location runtime](40-location-runtime.ko.md)이 소유하고, 이 문서는 Redis key, 원자 operation, 변경 감지,
오류 변환과 연결 수명을 정의한다.

Redis extension은 production 분산 구성이 사용하는 공식 기본 구현이지만 framework 본체 dependency는
아니다. 별도 package로 배포하며 application이 인스턴스와 연결 설정을 명시적으로 등록한다. 자동
discovery, remote Spot·Actor location 또는 분산 Actor transfer를 구성하고 Redis extension을 등록하지
않으면 host startup이 실패한다. Process-local in-memory 구현은 한 process의 contract test에서만 사용한다.

## 1. 등록과 설정

아래 코드는 .NET의 등록 지점을 보여 준다. 정확한 .NET interface는
[.NET Location Store·Redis](languages/dotnet/06-location-store.ko.md)가 소유한다.

```csharp
options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
    .SetConnectionString("redis-host:6379")
    .SetKeyPrefix("zlink:app")));
```

| 설정 | 의미 |
|------|------|
| connection string / configuration | Redis 연결 정보. 언어별 Redis client의 관용 표현을 그대로 받는다 |
| key prefix | 이 배포의 모든 key 앞에 붙는 격리 접두사. 배포(또는 테스트 실행)별로 달라야 한다 |

Redis extension 인스턴스는 MeshNode descriptor, Spot location, Actor location, owner lease, Actor transfer
authority와 routing ID slot allocation을 함께 제공한다. Change stamp도 같은 인스턴스가 제공한다.
Watch 형태의 변경 event stream은 공개 계약이 아니다. 이 extension은 polling과 change stamp로 변경을 감지한다.
polling은 주기적으로 store를 다시 읽는 방식이고, change stamp는 row가 바뀔 때 증가하는 번호다. 계약상
polling만으로도 올바른 연결 상태에 도달해야 하므로 Redis extension이 watch를 제공하지 않아도 충분하다.

## 2. Key schema

prefix `P`, kind ∈ {`mesh`, `spot`, `actor`, `route`} 기준. row key는 key 필드들을
`길이:값` 형태로 이어 붙인 문자열이다. 이렇게 길이를 함께 저장하면 값 안에 구분자가 들어 있어도
어디까지가 한 필드인지 알 수 있다. `RoutingId`는 hex로 인코딩한다. 임의 바이트 rid와 구분자 충돌을
피해야 하므로 row key에 raw rid 문자열을 쓰지 않는다.

| key | 타입 | 내용 |
|-----|------|------|
| `P:row:{kind}:{rowKey}` | HASH | `owner`, `gen`, `json`(row 직렬화), `updatedAtMs`[, `mesh`] |
| `P:gen:{kind}:{rowKey}` | STRING | generation counter. **row가 지워져도 삭제하지 않는다**(재claim 시 단조 증가 유지) |
| `P:keys:{kind}` | SET | 해당 kind의 모든 row key (목록 조회 index) |
| `P:own:{kind}:{ownerId}` | SET | 한 owner가 소유한 row key (owner 단위 bulk remove index) |
| `P:lease:{ownerId}` | STRING | `nodeRidHex\|updatedAtMs`, Redis `PX` TTL로 만료 |
| `P:leases` | SET | lease를 가진 적 있는 owner id 목록 |
| `P:stamp:{kind}[:{mesh}]` | STRING | scope별 change stamp counter |
| `P:transfer:{meshName}:{actorId}:{transferId}` | HASH | participant set, source·target identity, expected Actor generation·membership epoch, state와 recovery lease |
| `P:transfer-by-actor:{meshName}:{actorId}` | STRING | Actor마다 동시에 하나만 허용하는 active transfer ID |
| `P:ridalloc:{groupName}` | HASH | 정렬된 member·prefix JSON, slot count, identity mode, slot owner·generation과 owner별 slot index |

MeshNode descriptor row key는 `MeshName + RID`를 length-prefix로 연결한 값이다. Endpoint는 descriptor 값에
포함하며 identity key로 사용하지 않는다. 재기동한 같은 RID는 generation으로 구분한다.

Redis row 형식의 byte-for-byte fixture 정본은
`framework/testdata/location/redis/actor-location-v2.json`이다. 모든 공식 Redis extension은 이
fixture와 같은 key 문자열, hash field, row JSON을 만들어야 한다. fixture의 hash field는
`owner`, `gen`, `json`, `updatedAtMs`이고, row JSON은 PascalCase public field 이름을 유지한다.
`RoutingId` 값은 소문자 hex 문자열로 저장하고, route `Value`는 base64 문자열로 저장한다.

actor row와 key 형식은 다음 규칙으로 고정한다.

- actor row key는 **actor id 단독**이다. actor id 전역 unique 계약에 따라 `ActorType`은 key
  구성에 포함하지 않으며, row의 nullable 진단 필드로만 둔다.
- row `json`의 actor ref는 문자열 포맷이 아니라 **typed 객체 `{ nodeRid, actorId, generation }`**
  로 직렬화한다. 이 객체의 field 이름은 camelCase이고, `nodeRid`는 routing id hex 문자열이다.
  actor ref 문자열 조립/파싱은 어떤 언어 extension에도 존재해서는 안 된다.
- actor row에는 중복 `SpotKind` 필드를 두지 않는다. actor가 존재하는 spot 종류는
  `LocationKind` 단독으로 표현한다.
- 모든 공식 extension은 이 row와 key 형식을 동일하게 사용한다.

## 3. 원자성 — write는 전부 Lua script

모든 write 결정(NewClaim/Renew/Takeover 판정, generation 발급, owner-guard remove, lease
renew/remove와 Actor transfer 상태 전이)은 **Lua script 한 번**으로 원자 실행한다. script는:

- 판정과 갱신을 한 atomic step에서 수행한다 — NewClaim의 "현재 row 없음 또는 row owner의
  lease 만료" 확인과 새 generation 발급이 분리되지 않는다.
- **Redis `TIME`을 script 안에서 읽어** `updatedAtMs`와 lease 만료를 기록한다. 호출자의 wall
  clock은 계약에 들어오지 않는다(스크립트가 기록한 timestamp를 반환값으로 돌려준다).
- 결과를 `stored | stale | conflict`로 반환하고 extension이
  `ZLinkLocationWriteResult`로 변환한다.
- old-owner index 같은 파생 key는 row의 현재 owner를 알아야 계산되므로 ARGV로 prefix를 받아
  script 내부에서 조립한다.

### 3.1 Actor transfer authority

Actor transfer는 Actor 하나마다 active transfer 하나만 허용한다. 각 operation은 다음 원자 전이를
사용한다.

| 전이 | Redis 원자 조건과 결과 |
|---|---|
| prepare | Actor row의 source owner, Actor generation과 membership epoch가 expected 값과 일치하고 active transfer가 없을 때 `Prepared` record와 actor index를 함께 만든다 |
| commit | 같은 transfer가 `Prepared`이고 recovery lease owner가 일치할 때 Actor row를 target owner와 `expected epoch + 1`로 바꾸고 transfer를 `Committed`로 변경한다 |
| activate | `Committed` transfer만 `Activated`로 바꾸고 actor index를 정리할 수 있는 terminal 상태로 만든다 |
| abort | `Prepared` transfer만 `Aborted`로 바꾸며 Actor row의 source owner와 membership epoch를 유지한다 |
| takeover | recovery lease가 만료된 transfer의 participant set과 현재 Actor row를 확인한 뒤 successor lease owner를 한 번에 바꾼다 |

Redis record는 분산 권한 결정과 복구 상태를 소유한다. Core prepare가 발급하는 opaque sealed token은
같은 process의 Core handle에만 유효하므로 Redis에 저장하지 않는다. Successor는 Redis authority를
takeover한 뒤 자신의 Core runtime에서 prepare를 다시 수행해 새 sealed token을 얻는다. Application이
만든 임의의 token이나 외부 token 검증 callback은 Redis extension과 Core의 공개 계약에 포함하지 않는다.

**지원 topology는 standalone Redis다.** cluster에 배포하려면 모든 key가 한 slot에 모이도록
key prefix를 hash-tag(`{...}`)로 구성해야 한다(공식 지원 범위 밖의 운영 선택).

## 4. Lease와 stale 판정

- lease는 `PX` TTL이 걸린 STRING이다. 만료는 Redis가 수행하므로 lease read가 없어도 만료가
  성립한다.
- `ListOwnerLeases`는 lease 목록과 Redis `TIME` 기준 `StoreNow`를 한 script로 함께 반환한다.
  런타임의 만료 판정(`LeaseExpiresAt - StoreNow` + local monotonic 경과)이 이 snapshot을
  사용한다.
- NewClaim의 "row owner lease 만료" 판정은 script 안에서 `P:lease:{owner}` 존재 여부로
  원자적으로 확인한다.
- row 물리 삭제는 계약 대상이 아니다. lease가 만료된 owner의 row는 조회 경로(runtime의 lease
  join)에서 제외되며, `P:row`/`P:keys`/`P:own`의 잔존 항목 정리는 background cleanup 재량이다.

## 5. Change stamp

`P:stamp:{kind}[:{mesh}]`는 해당 scope의 write마다 `INCR`되는 단조 counter다. runtime의
polling tick은 stamp만 먼저 읽고(GET 1회) 값이 바뀌었을 때만 목록을 읽는다. stamp는 변경이 없을 때
전체 목록 조회를 건너뛰기 위한 최적화일 뿐이다. stamp가 유실되거나 실제 row 상태와 잠시 어긋나도,
다음 polling의 전체 목록 조회로 최종 연결 상태가 맞춰져야 한다.

## 6. 오류 변환과 connection lifecycle

- read API와 write API에서 Redis 연결/명령 실패는 infrastructure error로 던진다(계약 §3.1).
- Redis client connection은 extension 인스턴스가 소유한다. 인스턴스는 `IAsyncDisposable`이며
  framework host가 dispose lifecycle을 관리한다. 재연결 정책은 언어별 Redis client의 표준
  동작을 따르고, 장애 구간의 의미는 framework의 fail-static 규칙이 담당한다. fail-static은 마지막으로
  성공한 연결 판단을 유지하고 새 connect/disconnect 계산을 멈추는 정책이다.
- Redis 응답 지연/실패가 framework runtime을 블록하면 안 된다 — 조회 실패는 상태
  (`StoreFailure`)와 이벤트로 강등된다.

## 7. 격리와 테스트

- 배포별 key prefix 격리가 필수다. E2E와 테스트는 실행마다 전용 prefix(또는 disposable Redis
  instance)를 사용하고, 실행 후 prefix 하위 key를 정리하거나 인스턴스를 버린다.
- store 계약 회귀는 Redis extension contract test로 검증한다. Process-local 동작만 필요한 공통 row
  test는 test-only in-memory 구현에도 실행할 수 있지만, 분산 lease·transfer authority 검증을 in-memory
  결과로 대신하지 않는다. Redis 자체의 HA/복제(sentinel, cluster)는 이 extension의 검증
  범위가 아니다.

## 8. routing id slot 원자성

slot acquire는 Redis `TIME` 조회, group 구성 확인 또는 최초 고정, 같은 owner의 멱등 claim 확인, 가장
작은 빈 slot 선택, generation 증가, slot·owner index 기록과 owner lease 갱신을 Lua script 한 번으로
수행한다. peer row만으로는 fixed owner와 다른 allocation group의 owner를 구분할 수 없으므로 acquire가
기존 peer row를 fixed RID 충돌로 추측하지 않는다. 같은 runtime에서 fixed 설정과 자동 할당을 함께
사용하는 구성은 framework 설정 검증에서 거부한다. 여러 runtime이 같은 member에 두 방식을 섞는
배포는 지원하지 않는다. slot의 유효성은 hash field가 존재하는지가 아니라
`P:lease:{ownerId}`의 논리 유효성으로 판단한다. list 결과의 만료 시각도 같은 owner lease의 남은
TTL과 script 안에서 읽은 store 시각으로 계산한다.

release는 group, slot, owner id와 generation이 모두 일치할 때만 slot과 owner index를 지운다.
오래된 token은 `IgnoredStale`로 반환해 현재 claim을 유지한다. group metadata는 첫 acquire 뒤
자동으로 변경하거나 삭제하지 않는다.

공식 지원 topology는 기존 location row와 마찬가지로 standalone Redis다. 비동기 replica failover
뒤 성공 응답을 받은 write가 유실될 수 있는 구성은 strict single-active 보장을 제공하지 않는다.
Sentinel이나 Cluster를 지원 범위로 확대하려면 성공한 acquire의 failover durability와 모든 관련
key의 동일 hash slot 배치를 별도로 검증해야 한다.
