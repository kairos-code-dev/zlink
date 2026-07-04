<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Location Runtime](location-runtime.ko.md) | [다음: Spot 주소 기반 메시징](spot-address-messaging.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

# Location Store — 공식 Redis Extension

이 문서는 framework가 공식 제공하는 **Redis location store extension**의 언어 중립 공통
스펙이다. store/lease/generation의 계약 의미는 [location runtime](location-runtime.ko.md)이
소유하고, 이 문서는 그 계약을 Redis 위에서 어떻게 만족시키는지(key schema, 원자성, watch/stamp,
오류 변환, connection lifecycle)를 정의한다.

> Redis extension은 **공식 제공이지만 framework 본체 dependency가 아니다.** 별도 package
> (`Zlink.Framework.Locations.Redis` 상당)로 배포되고, 사용자는 인스턴스를 만들어
> `AddLocationStore(instance)`로 등록한다. 전용 등록 함수는 없다.

## 1. 등록과 설정

```csharp
options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
    .SetConnectionString("redis-host:6379")
    .SetKeyPrefix("zlink:app")));
```

| 설정 | 의미 |
|------|------|
| connection string / configuration | Redis 연결 정보. 언어별 Redis client의 관용 표현을 그대로 받는다 |
| key prefix | 이 배포의 모든 key 앞에 붙는 격리 접두사. 배포(또는 테스트 실행)별로 달라야 한다 |

Redis extension 인스턴스는 store 5종 통합 계약과 optional **change stamp** 계약을 구현한다.
watch(변경 이벤트 stream)는 구현하지 않는다 — polling + change stamp가 이 extension의 변경
감지 경로다(계약상 polling이 correctness 경로이므로 충분하다).

## 2. Key schema

prefix `P`, kind ∈ {`peer`, `spot`, `actor`, `route`} 기준. row key는 key 필드들을
`길이:값` 형태로 이어 붙인 canonical length-prefixed 문자열이며, `RoutingId`는 hex로
인코딩한다(임의 바이트 rid와 구분자 충돌을 피하기 위해 — row key에 raw rid 문자열을 쓰지
않는다).

| key | 타입 | 내용 |
|-----|------|------|
| `P:row:{kind}:{rowKey}` | HASH | `owner`, `gen`, `json`(row 직렬화), `updatedAtMs`[, `mesh`] |
| `P:gen:{kind}:{rowKey}` | STRING | generation counter. **row가 지워져도 삭제하지 않는다**(재claim 시 단조 증가 유지) |
| `P:keys:{kind}` | SET | 해당 kind의 모든 row key (목록 조회 index) |
| `P:own:{kind}:{ownerId}` | SET | 한 owner가 소유한 row key (owner 단위 bulk remove index) |
| `P:lease:{ownerId}` | STRING | `nodeRidHex\|updatedAtMs`, Redis `PX` TTL로 만료 |
| `P:leases` | SET | lease를 가진 적 있는 owner id 목록 |
| `P:stamp:{kind}[:{mesh}]` | STRING | scope별 change stamp counter |

row key 예 (peer): `AutoConnectType canonical 문자열 + MeshName + Role canonical 문자열 +
identity(NodeRid hex, 없으면 endpoint)`를 length-prefix로 연결한 값.

**POSD 재설계 반영(2026-07-04, cross-language row/key 형식 변경)**:

- actor row key는 **actor id 단독**이다 — actor id 전역 unique 계약에 따라 `ActorType`은 key
  구성에서 제거됐다(type은 row의 nullable 진단 필드로만 남는다).
- row `json`의 actor ref는 문자열 포맷이 아니라 **typed 객체 `{ nodeRid, actorId, generation }`**
  로 직렬화한다. actor ref 문자열 조립/파싱은 어떤 언어 extension에도 존재해서는 안 된다.
- actor row의 중복 `SpotKind` 필드는 제거됐다 — actor가 사는 spot 종류는 `LocationKind` 단독이다.
- 이 세 가지는 4언어 extension이 동일하게 따라야 하는 저장 형식 변경이며, 언어별 적용 현황은
  `framework/doc/plan/framework-public-contract-posd-redesign-{node,java,cpp}.ko.md`가 추적한다.

## 3. 원자성 — write는 전부 Lua script

모든 write 결정(NewClaim/Renew/Takeover 판정, generation 발급, owner-guard remove, lease
renew/remove)은 **Lua script 한 번**으로 원자 실행한다. script는:

- 판정과 갱신을 한 atomic step에서 수행한다 — NewClaim의 "현재 row 없음 또는 row owner의
  lease 만료" 확인과 새 generation 발급이 분리되지 않는다.
- **Redis `TIME`을 script 안에서 읽어** `updatedAtMs`와 lease 만료를 기록한다. 호출자의 wall
  clock은 계약에 들어오지 않는다(스크립트가 기록한 timestamp를 반환값으로 돌려준다).
- 결과를 `stored | stale | conflict`로 반환하고 extension이
  `ZLinkLocationWriteResult`로 변환한다.
- old-owner index 같은 파생 key는 row의 현재 owner를 알아야 계산되므로 ARGV로 prefix를 받아
  script 내부에서 조립한다.

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
polling tick은 stamp만 먼저 읽고(GET 1회) 값이 바뀌었을 때만 목록을 읽는다. stamp는 최적화일
뿐이며 유실/불일치가 있어도 다음 polling의 전체 목록 조회로 correctness가 보장된다.

## 6. 오류 변환과 connection lifecycle

- read API와 write API에서 Redis 연결/명령 실패는 infrastructure error로 던진다(계약 §3.1).
- Redis client connection은 extension 인스턴스가 소유한다. 인스턴스는 `IAsyncDisposable`이며
  framework host가 dispose lifecycle을 관리한다. 재연결 정책은 언어별 Redis client의 표준
  동작을 따르고, 장애 구간의 의미는 framework의 fail-static 규칙이 담당한다.
- Redis 응답 지연/실패가 framework runtime을 블록하면 안 된다 — 조회 실패는 상태
  (`StoreUnavailable`)와 이벤트로 강등된다.

## 7. 격리와 테스트

- 배포별 key prefix 격리가 필수다. E2E와 테스트는 실행마다 전용 prefix(또는 disposable Redis
  instance)를 사용하고, 실행 후 prefix 하위 key를 정리하거나 인스턴스를 버린다.
- store 계약 회귀는 언어 공통 store contract 테스트(같은 시나리오를 in-memory 구현과 Redis
  구현에 함께 실행)로 검증한다. Redis 자체의 HA/복제(sentinel, cluster)는 이 extension의 검증
  범위가 아니다.
