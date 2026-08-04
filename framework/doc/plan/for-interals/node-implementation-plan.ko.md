# Node framework 구현 계획

[계획 문서 묶음](README.ko.md) · [구현 갭 목록](framework-internals-implementation-gaps.ko.md)

판정과 근거는 [구현 갭 목록](framework-internals-implementation-gaps.ko.md)이 소유한다.
이 문서는 **무엇을 어떤 순서로 고칠지**만 다룬다.

> **여기 적힌 `file:line`은 길잡이다.** 판정의 근거가 아니며, 갭 목록과 어긋나면 **갭
> 목록이 맞다.** 새 사실을 발견하면 갭 목록을 먼저 고친다.

## 이 언어의 특징

**Spot·Actor queue에 한도가 아예 없다.** promise chain tail에 무한히 붙는 구조라, 다른
언어처럼 "한도를 두 축으로 넓히는" 것이 아니라 **한도라는 개념을 처음 도입**해야 한다.
W1이 가장 크고, 다른 묶음의 선행 조건이기도 하다.

**event loop가 하나다.** 두 실행 영역을 물리적으로 분리할 수 없다. 계약이 이 제약에 맞춰
정의되어 있다 — 보장하는 것은 "application handler가 `await`로 양보한 뒤에는 infrastructure가
진행한다"까지이고, 양보 없이 CPU를 붙잡는 동안은 보장하지 않는다
([3. application과 infrastructure 실행 분리](../../framework/common/internals/03-progress-isolation.ko.md)).

**owner 점유 상한 구현에 macrotask 경계가 필요하다.** 이미 완료된 promise가 계속 이어지면
microtask만 돌아 timer와 I/O가 진행하지 못한다. 상한에 도달하면 `setImmediate` 같은
macrotask 경계를 통과해야 한다.

두 채널 종류 모두 framework에서 고르므로 Core 수정에 걸리지 않는다.

## 순서

```
0. 선행 판정 대기        상한 값 · exact interface · codec 선택 키
   │
1. B5 취소 판정          작고 독립적
   │
2. W1 scheduler          ★ 한도 도입부터. 가장 크고 다른 묶음의 선행 조건
   │
3. W5 관찰자             exact interface 확정 후
   │
4. W2 selector           선행 과제 없음. RouteMesh만 갭
   │
5. W6 수신 공정성        채널 loop은 충족. STREAM·RouteMesh 감사 필요
   │
6. W4 유휴 정리          신규 기능
   │
7. W3 relay 통지         wire command 확정 후
   │
8. B4b 계약 정합 · C부
```

## B5 — 취소 판정 (먼저)

`runtime/execution/index.ts:365-370`이 오류 message를 `/aborted|cancelled|canceled/i`로
매칭한다. 오류 분류를 문자열에 의존하면 런타임 버전이나 라이브러리에 따라 판정이 달라진다.

typed abort/cancellation 오류와 `AbortSignal` 상태만 쓰고 일반 오류 문자열은 분류 입력에서
제거한다. 작고 독립적이라 먼저 넣는다.

## W1 — scheduler (한도 도입, 가장 큼)

한 단위로 묶는 항목: **A4 · A6 · A8 · A10 · D4**

**출발점이 다른 언어와 다르다.** 한도도 lane도 claim도 없는 promise chain에서 시작한다.

| 단계 | 지금 | 목표 |
|---|---|---|
| 한도 | **없음.** promise tail에 무한히 붙는다 (`runtime/spots/spot-serial-executor.ts:109-113`, `runtime/actors/actor-mailbox.ts:1-12`) | 건수·byte 두 축을 하나의 예약으로 |
| lane | 단일 tail. barrier도 같은 tail 뒤에 붙어 **우선 실행이 아예 없다** (`runtime/spots/spot-serial-executor.ts:75-77`) | application·lifecycle 두 FIFO lane |
| 선택 | 없음 | lane 선택 + 연속 실행 상한 + 양보 부채 |
| 점유 | tail을 계속 잇는다 (`:109-113`) | claim 시작 시각 + 경과 시간 확인 + **macrotask 경계로 양보** |
| 포화 | 발생하지 않는다 | 계열×위치 표에 따른 kind |
| 반납 | — | handler 종료 후 두 축 동시 반납 |

**참조 구조가 이미 있다.** `runtime/foundation/service-mailbox.ts:89-93`이 message·byte 두
축을 모두 강제한다. 실행 queue를 같은 형태로 맞춘다.

**event loop 하나라는 제약을 회계에 반영한다.** Node에는 process 전역 atomic이 필요 없다.
다른 언어의 lock·atomic 구조를 그대로 옮기면 불필요한 비용만 생긴다.

**D4 — 복사.** `contracts/Common/ZLinkEncodedPayload.ts:4,12`가 생성과 `data()`에서 각각
복사한다. 그리고 `runtime/messaging/payload-codec.ts:173`이 `isJsonPayload()`에서
`JSON.parse`를 수행한 뒤 성공하면 같은 message를 **다시 parse한다**(`:79-80`). buffer 복사가
아니라 CPU와 객체 생성 비용이며, internals가 금지한 "형식 판별을 위한 이중 해석"에 정확히
해당한다. wire의 content-type으로 codec을 골라 본문 sniffing을 없앤다.

## W5 — 관찰자

항목: **A11 · A12**

두 자리가 가득 차면 오래된 것을 버리고, terminal 뒤에는 stream을 닫는다.

| 위치 | 결함 |
|---|---|
| `runtime/host/route-mesh-runtime.ts:670-679` | 오래된 것 폐기 |
| `runtime/host/route-mesh-runtime.ts:701` | terminal 전달 뒤 `close()` |
| `runtime/diagnostics/topology-runtime-projections.ts:327` drop, `:342` close | 같은 패턴 |

**결함이 둘이다.** 버리는 것과 닫는 것은 다른 위반이다. terminal은 source slot만 닫고
구독 자체는 유지한다.

source별 최신 slot + terminal 보관 + 관찰자별 유실 누계 구조로 바꾼다. 전달 형태가
`{status, 유실 누계}` 쌍이 되므로 **exact interface 확정이 선행**이다
(`spec/server/languages/node/interfaces/03-location-observability.ko.md`에 유실 field가 없다).

## W2 — selector

항목: **A1 · D1**

**Node는 갭이 하나뿐이다.** ClientServer는 이미 충족한다.

| 경로 | 판정 |
|---|---|
| ClientServer | **충족.** 누적값 3단계 + `serverRoutingId` tiebreak (`runtime/foundation/service-discovery-registry.ts:80-115`) |
| RouteMesh 채널 | **갭.** gcd로 줄인 **연속 block** (`runtime/foundation/service-topology-registry.ts:333-354`) |

RouteMesh만 누적값 3단계로 바꾸면 된다. 후보 정렬이 이미 NodeRid라 tiebreak는 맞다.
**ClientServer 구현을 그대로 참고할 수 있다** — 같은 저장소 안에 정답이 있다.

**D1은 Node에도 해당한다.** `runtime/foundation/service-discovery-registry.ts:80`이 호출마다
filter·sort·`Set` 생성 후 전체 누적값을 갱신한다. 후보 변경 시점으로 옮긴다.

## W6 — 수신 공정성

항목: **A9**

채널 loop은 connection별로 한 record를 처리한 뒤 event loop에 양보해 **충족**이다
(`runtime/channels/channel-receive-loops.ts:283-306`).

다만 STREAM은 별도 frame batch와 양보를 쓰므로
(`runtime/streams/stream-session-runtime.ts:835-841`) 채널 loop 판정으로 증명되지 않는다.
**부분 확인 상태이며 STREAM·RouteMesh 경로 감사가 필요하다.**

## W4 — 유휴 정리 (신규 기능)

항목: **A3**

`contracts/Spots/ZLinkSpot.ts:19-23`에 `IdleEvicted`를 추가하는 것은 시작일 뿐이다.
Location Store CAS·admission seal·timer 구조가 함께 필요하다.

## W3 — relay 통지

항목: **A2 · D5**

통지가 optional method 호출로 되어 있고 **구현체가 없다**(`runtime/host/index.ts:404-410`,
`runtime/actors/actor-transport-delivery-gate.ts:3-16`). 자리는 있으나 아무것도 하지 않는
상태다. wire command 확정 전에는 시작할 수 없다.

## B4b — 공개 계약에 없는 선택자

공개 codec registry 계약은 `addSerializer(contentType, serializer)` 두 인자인데
(`contracts/Codecs/IZLinkCodecRegistryBuilder.ts:11`), runtime이 계약에 없는 optional
`canSerialize`를 타입 단언으로 읽고 검사한다(`runtime/messaging/payload-codec.ts:150,169`).

**이 항목은 단독으로 닫을 수 없다.** `06-framework-api`가 codec 선택 입력을 "선언 type"으로
고정했는데 Node 공개 API는 `message: unknown`만 받아 그 정보가 runtime에 남지 않는다.
`canSerialize`를 공개 계약으로 올려도 여전히 **값 기반**이라 조항을 만족하지 못한다.

갭 목록 A1c에 함께 묶여 있다. 네 언어 공통 선택 키가 정해진 뒤에 진행한다.

## C부 구조 부채

- 직렬 실행 원시 타입 4종 통합 — W1에서 실행 구조를 새로 짜므로 **함께 정리하는 것이 낫다**

## 검증

W1은 한도가 없던 상태에서 시작하므로 **빈 payload flood와 큰 payload flood 두 경우**를
모두 확인한다. owner 점유 상한은 **application queue가 계속 ready인 상태에서 timer와 I/O가
진행하는지**를 보는 test가 필요하다 — microtask만 도는 구현은 이 test에서 걸린다.
