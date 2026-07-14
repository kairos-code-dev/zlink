# `.NET` — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> **기준선이다.** 다른 언어가 여기에 맞춘다. 그래서 여기 남은 갭은 **다른 언어로 전파된다.**

**이 문서는 계약이 아니라 작업 목록이다.** 계약은 spec이 소유한다. 여기서는 **스펙과 코드가 어긋난 자리**와 그것을 닫았는지만 추적한다.

**두 종류를 구분한다** — **미구현**(없다 → 만든다) / **결함**(있는데 계약과 다르게 돈다 → 동작을 바꾼다). 결함이 더 위험하다: 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **부하가 걸릴 때만 드물게 깨진다.**

## 1. 진행 체크리스트

**전체 15건. 완료 0건.**

### 구현 감사에서 발굴 (2026-07-14, 스펙↔코드 직접 대조)

- [ ] **IMP-DN-01** (결함) — 20 §5
- [ ] **IMP-DN-02** (결함) — 22 §5·20 §8
- [ ] **IMP-DN-03** (결함) — 05 §3.3·31 §15
- [ ] **IMP-DN-04** (결함) — 51
- [ ] **IMP-DN-05** (결함) — 05 §2.4.3
- [ ] **IMP-DN-06** (결함) — 40 §3·§8.2
- [ ] **IMP-DN-07** (결함) — 20 §8

### 교차 언어 결함 (여러 구현에 같은 문제)

- [ ] **IMP-X3** — startup validation이 스펙의 설정 오류를 통과시킨다
- [ ] **IMP-X4** — location store read에 5초 취소 상한이 없다

### 언어별 표면 차이 (기준선 대조)

- [ ] **§12.7** — metric drop reason 라벨 도달 불가 (`.NET`)

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [ ] **§12.20** (결함) — 응답에 packet name을 싣는다
- [ ] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
- [ ] **§12.22** (결함+미구현) — HTTP client가 framework 계약 밖에 있다
- [ ] **§12.23** (미구현) — worker 축 분리와 `yield` 부재
- [ ] **§12.24** (결함) — actor join의 orchestration이 뒤집혀 있다

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 구현 감사 상세

| ID | 종류 | 계약 | 구현이 하는 일 |
|----|------|------|----------------|
| **IMP-DN-01** | 결함 | [20 §5](../server/20-spot-messaging.ko.md): SPOT route one-way의 decode 실패는 **`Drop`** + 경고 + metric | `Runtime/Spots/ZLinkSpotRouteDispatcher.cs:105-108` — `Request` 분기는 `TryDecode`로 감싸는데 **one-way 분기만 `DecodeBody`를 무방비 호출**한다. 예외가 dispatcher를 뚫고 나가 **drain 루프까지 중단**시키고, 뒤에 큐잉된 route 메시지가 처리되지 않는다 |
| **IMP-DN-02** | 결함 | [22 §5](../server/22-actor-model.ko.md)·[20 §8](../server/20-spot-messaging.ko.md): handler 중복 등록은 **startup 오류** | 중복 검사가 spot **활성화 시점**의 `Bind()`에만 있다(`ZLinkSpotPacketRegistry.cs:25-38`). host는 정상 기동하고 **첫 방 생성에서** 터지며, 그것도 `SpotCreateFailed`로 감싸여 설정 오류로 보이지 않는다. Entry Spot 중복은 startup에서 잡히므로 **두 표면이 비대칭** |
| **IMP-DN-03** | 결함 | [05 §3.3](../05-framework-api.ko.md)·[31 §15](../server/31-session-actor-dispatch.ko.md): send는 nonblocking 시도 → **pending queue + ready 알림** | `Runtime/Streams/ZLinkBoundSessionService.cs:82-90` — bound-session push만 `SendFlags.DontWait` 실패 시 **즉시 `RouteNotConnected` 예외**. 클라이언트 소켓 하나가 잠깐 차면 브로드캐스트 타이머 턴이 죽는다 |
| **IMP-DN-04** | 결함 | [51](../server/51-runtime-metrics.ko.md): `zlink.spot.count`는 현재 유지 중인 SPOT 수 | `ZLinkSpotNodeCatalog.cs` — `RecordSpotCreated`가 `GetOrCreateAsync`(:354)에만 있고 **`CreateAsync`에는 없다.** 종료는 양쪽 다 기록(:548, :581). `CreateAsync`만 쓰는 앱은 5회 만들고 닫으면 게이지가 **-5** |
| **IMP-DN-05** | 결함 | [05 §2.4.3](../05-framework-api.ko.md): reason 닫힌 집합에 `PayloadDecodeFailed` | `ZLinkSpotActorPacketDispatcher.cs:35-51` — decode가 handler 호출 **안에서** 일어나 모두 `HandlerException`으로 보고된다. actor 표면에서 `PayloadDecodeFailed`가 **한 번도 발생하지 않는다** |
| **IMP-DN-06** | 결함 | [40 §3·§8.2](../server/40-location-runtime.ko.md): 목록 조회는 `list page size` option을 따른다 | `ZLinkStoreLocationResolvers.cs:90-98` — `new ZLinkPageRequest(1000, …)` **하드코딩**. drain 대상 탐색 경로라 option이 무시된다 |
| **IMP-DN-07** | 결함 | [20 §8](../server/20-spot-messaging.ko.md): **같은 Entry Spot 타입 중복**은 설정 오류 | `ZLinkSpotRegistrationValidator.cs:55-63` — spot factory는 노드 간 중복을 검사하는데 **Entry Spot 타입은 안 한다.** 두 SpotNode가 같은 Entry Spot 타입을 등록해도 기동된다 |

## 3. 언어별 표면 차이 상세

### §12.7 metric drop reason 라벨 도달 불가 (`.NET`)

**미충족(`.NET`).** [51 §4.4](../server/51-runtime-metrics.ko.md)의 `zlink.channel.messages.dropped`는
`no_handler`, `decode_error`, `backpressure`, `stale_route` 네 라벨을 규정한다. 현재 `.NET`
런타임에서 실제로 방출되는 값은 `no_handler` 하나뿐이다 — decode 실패 경로가 drop metric을
기록하지 않고, `backpressure`와 `stale_route` 사유를 넘기는 호출부가 없다.
