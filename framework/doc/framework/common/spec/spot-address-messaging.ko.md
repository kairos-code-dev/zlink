<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Location Store — Redis](location-store-redis.ko.md) | [다음: Use Case Validation](usecase-validation.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

# Spot 주소 기반 메시징

이 문서는 spot/actor 대상 메시징의 언어 중립 공통 스펙이다. **주소 모델
(`ZLinkSpotAddress`), 조회-보관-재조회 사용 모델, 메시징 표면, stale 주소 실패 계약**의
의미를 소유한다. 위치 저장·resolver·자동 연결의 하부 계약은
[location runtime](location-runtime.ko.md)이 소유하고 이 문서는 반복하지 않는다.

> 설계의 제1원칙: **전송 경로는 store를 읽지 않는다.** 위치 조회는 명시적 resolve 1회로
> 끝나고, 호출자가 주소를 보관하며, 실패가 재resolve를 유도한다. resolver·전송 어디에도
> 캐시가 없다 — "이름 없는 전역 캐시"가 각 보유자의 명시적 상태로 바뀐 모델이다.

## 1. Spot 주소 모델

```csharp
/// <summary>mesh 안의 spot 하나를 가리키는 논리 주소. resolve 1회로 얻어
/// 호출자가 보관하고, 실패 시 재resolve한다.</summary>
public readonly record struct ZLinkSpotAddress(
    RoutingId NodeRid,
    RoutingId SpotRid);
```

- **owner node rid + spot rid를 담는다.** endpoint metadata는 넣지 않는다 — node로의 물리
  연결은 자동 연결(peer location)의 책임이고, 메시징 주소는 mesh 연결이 있다는 전제 위의
  논리 주소다.
- **mesh는 주소가 아니라 전송 문맥이 결정한다.** 모든 전송 표면은 이미 mesh가 확정된
  문맥에서 호출된다 — spot 안의 outbound(`IZLinkSpotOutbound`)는 자기 mesh로 보내고,
  외부 client(`IZLinkRouteClient`)는 router channel id를 명시한다. 그래서 주소에 mesh를
  중복 보관하지 않으며, 잘못된 mesh로의 오용은 문맥 표면에서 걸러진다.
- **generation을 넣지 않는다.** generation이 주소에 있으면 node가 바뀌지 않은 generation
  bump에도 보유 주소가 전부 낡아져 재조회만 늘어난다. stale 판정은 owner lease와 수신측
  dispatch 실패가 담당한다(§4).
- **주소는 spot의 현재 활성 생애 동안만 유효하다.** spot은 이동(takeover)하고, 소멸했다
  재활성되며, actor 1:1 topology처럼 생성·소멸이 잦은 구성도 있다. 주소 보유 기간은 대상
  spot의 lifecycle에 맞추고, 어긋난 보유는 §4의 실패 계약으로 드러난다.
- entry spot의 주소는 `NodeRid == SpotRid`다. 별도 특례를 두지 않는다.

같은 `RoutingId` 두 개를 낱개 파라미터(`targetPeerRid`, `targetSpotRid`)로 나란히 받는
표면은 순서 실수를 컴파일러가 잡지 못하므로 두지 않는다 — 전송 표면은 항상 주소 값 하나를
받는다. location row(`ZLinkSpotLocation`, `ZLinkActorLocation`)는 운영 조회·lifecycle용
모델로 유지되며, 주소는 row에서 파생되는 메시징용 값이다.

## 2. 조회 표면

메시징 조회는 [location runtime §5](location-runtime.ko.md)의 두 resolver다. 모든 조회가
store에 도달하고 owner lease join으로 유효성을 판정한다.

| resolver | 입력 | 반환 |
|----------|------|------|
| `IZLinkSpotLocationResolver.ResolveSpotAddressAsync(spotRid)` | spot rid (이 runtime이 참여한 spot mesh들에서 검색) | `ZLinkSpotAddress?` |
| `IZLinkActorLocationResolver.ResolveActorSpotAddressAsync(actorType, actorId)` | 전역 actor key | actor가 위치한 spot의 `ZLinkSpotAddress?` (ENTRY_SPOT이면 entry spot 주소) |

- actor 1:1 spot topology에서는 호출자가 아는 것이 transient한 spot rid가 아니라 actor
  id이므로 `ResolveActorSpotAddressAsync`가 1차 조회 표면이다. spot rid 조회는 spot rid를
  도메인 key에서 파생하는 topology(player owner spot 등)에서 쓴다.
- 재연결·"없으면 생성"·takeover 같은 lifecycle 흐름은 주소가 아니라 generation을 포함한
  location row가 필요하므로 resolver가 아니라 store/runtime 경로를 쓴다.
- 없는 대상, owner lease가 만료된 대상은 null이다.

## 3. 메시징 표면과 사용 모델

```csharp
// spot 문맥 안 (mesh = 자기 mesh)
IZLinkSendCall SendToSpot<TMessage>(ZLinkSpotAddress address, TMessage message);
IZLinkRequestCall RequestToSpot<TRequest>(ZLinkSpotAddress address, TRequest request);

// 외부 connector client (mesh = 명시한 router channel)
IZLinkSendCall SendToSpot<TMessage>(string routerChannelId, ZLinkSpotAddress address, TMessage message);
IZLinkRouteRequestCall RequestToSpot<TRequest>(string routerChannelId, ZLinkSpotAddress address, TRequest request);
```

- spot rid만 받고 내부에서 resolve해 주는 overload는 **없다**. 전송 경로의 숨은 store
  I/O를 금지하는 제1원칙 때문이다.
- egress는 resolve하지 않는다: `address.NodeRid`가 local node면 직접 dispatch, 아니면 그
  mesh의 route bridge로 전달한다.
- **원격 spot 전달의 wire form은 route socket 위의 spot route bridge relay framing
  하나로 고정한다.** 소켓 수준 framing과 혼용하면 수신 pump가 한쪽을 일반 envelope로
  오인해 drop한다. 포팅 언어는 이 framing 하나만 구현한다. 이 고정은 framework node 간
  route channel 평면에 한정하며, 외부 connector client가 spot node router 평면으로 직접
  보내는 inbound는 기존 계약 그대로다.

사용 패턴 — **조회 1회, 로직에서 재사용**:

```csharp
// bind/route 수립 시점에 1회 조회해서 상태에 보관한다.
var address = await spots.ResolveSpotAddressAsync(playerQuestSpotRid, ct)
              ?? /* 미활성이면 도메인 규칙에 따라 placement 경로로 */;

// 이후 이벤트마다 보관한 주소로 전송한다. store 조회 없음.
await outbound.SendToSpot(address, new ApplyGameplayEvent(...)).SendAsync(ct);
```

framework 내부 호출자(bound session notify, actor session relay 등)도 같은 규칙을
따른다. 주소 보유 범위는 대상 spot의 lifecycle에 맞춘다 — 도메인 key에 묶여 오래 사는
spot은 session 상태에 보관해 재사용하고, actor lifecycle을 따라 생성·소멸하는 1:1 spot은
상호작용 범위에서만 조회·보관한다.

## 4. stale 주소 실패 계약

owner 이동, node 장애, 정상 lifecycle의 spot destroy 후 보유 주소는 낡는다. 그때의
실패는 **구분 가능**해야 한다 — 이것이 이 설계의 핵심 계약이다.

| 경로 | stale 주소일 때 | 호출자 책임 |
|------|-----------------|-------------|
| `RequestToSpot` | 로컬 판정 가능 실패는 대기 없이 즉시 typed error. node 도달 후 spot 부재는 수신측이 `SpotRouteNotFound` 오류 reply. timeout은 "전송했고 응답 없음"에만 남는다 | 아래 분류표(횟수·정책은 호출자 소유) |
| `SendToSpot` | best-effort. 대상 node에 spot이 없으면 drop, 통지 없음 | 도메인 보정(reconcile) 또는 location event 구독으로 회복 |

### 4.1 request 실패 분류 — fail-fast

오류 종류는 전부 기존 `ZLinkFrameworkErrorKind`를 쓴다. 새 종류를 추가하지 않는다.

| 상태 | 판정 위치 | 오류 | 호출자의 다음 행동 |
|------|-----------|------|--------------------|
| local runtime이 대상 mesh 미참여 | local, 즉시 | 구성 오류 | 재시도 금지 — 코드/구성 수정 |
| mesh가 모르는 node rid | local, 즉시 | `RequestTargetNotFound` | 재resolve (주소가 근본적으로 낡음) |
| node는 알지만 미연결 | local, 즉시 | `RouteNotConnected` (`IsRetriable=true`) | 같은 주소로 짧은 backoff 재시도 — mesh 수렴 창일 수 있다. 반복되면 재resolve |
| node 도달, spot 부재 | 수신측, 오류 reply 1왕복 | `SpotRouteNotFound` | 재resolve 후 재시도 |
| 전송 후 무응답 | timeout | timeout | 도메인 정책 (중복 실행 위험 감수) |

- "모르는 node"와 "미연결"의 로컬 판정은 자동 연결 reconciler의 **mesh 구성원 snapshot**과
  소켓 연결 상태에서만 나온다. 기준은 desired dial set이 아니라 **구성원 전체**다 —
  pairwise initiator 때문에 상대가 나를 dial하는 peer는 desired set에 없지만 rid 지정
  가능한 도달 대상이다. 이 판정을 위해 전송 경로에서 store를 읽지 않는다.
- 자동 연결 없이 수동 connect만 쓰는 구성은 snapshot이 없으므로 이 구분 없이 기존
  동작(연결 수렴 대기)을 유지한다.
- 미연결 즉시 오류는 startup·mesh 수렴 창에서 호출자에게 그대로 드러난다 — 의도된
  동작이다. 그 창을 넘기는 대기·재시도 편의는 전송 계층이 몰래 하지 않고, 재시도 횟수와
  backoff가 명시적 파라미터인 helper 경계에서 제공한다.

### 4.2 재시도의 의미

재시도는 framework가 전송 계층에서 몰래 반복하는 것이 아니라, **호출자가 오류를 보고
같은 API를 다시 부르는 명시적 재호출**이다.

| 재호출 형태 | 트리거 | 안전 근거 |
|-------------|--------|-----------|
| 같은 주소 재전송 | `RouteNotConnected` | 전송 전 실패 — handler 미실행 확정, 중복 없음 |
| 재resolve 후 재전송 | `SpotRouteNotFound`, `RequestTargetNotFound` | 주소가 낡았다. `SpotRouteNotFound`는 node에 도달했어도 spot이 없어 handler 미실행 확정 |
| 재시도 금지 | 구성 오류 | 몇 번을 불러도 같은 결과 — 코드/구성 수정 대상 |
| 도메인 판단 | timeout | handler 실행 여부 불확정 — 도메인 idempotency가 있을 때만 재시도 |

`RouteNotConnected`의 `IsRetriable=true`는 "전송 전 실패라 요청 중복 없이 안전하게
재시도할 수 있다"는 의미다. timeout과의 이 대비가 분류표의 실질 가치다.

전송 계층의 숨은 재시도는 금지한다. rid 지정 router 전송 경로는 NotConnected를 재시도로
흡수하지 않고 즉시 반환한다. dealer/pub의 connect-창 버퍼링(client/server 채널이 dial
직후 send하는 패턴)은 유지한다. fire-and-forget send의 local submit 실패는 최소
debug/monitoring event로 관측 가능해야 한다.

### 4.3 경계와 보정

- destroy된 spot의 주소는 재resolve가 null을 반환한다. 재활성(placement)할지 포기할지는
  도메인 결정이다.
- **spot 이동·재활성은 메시지 순서·전달 경계다.** 이동 창에서 이전 주소로의 in-flight
  전송은 drop될 수 있고 새 주소 전송은 도달하므로, 이동을 가로지르는 전달 순서와 전달
  자체를 보장하지 않는다. 순서에 민감한 도메인은 dedupe·reconcile 보정으로 흡수한다.
- send-only로 오래 보유하는 주소는 stale을 스스로 알 수 없다. 이런 보유자는 spot location
  event source를 구독해 보유 주소를 무효화하거나, 주기 request를 섞어 stale을 노출시킨다.
- 재resolve 직후에도 store 반영 지연(lease polling 한 tick) 동안 null이나 이전 주소가
  나올 수 있다. 호출자 재시도는 bounded backoff를 권장한다.

## 5. 회귀 기준

- resolver stale 제외·주소 파생은 언어 공통 unit/contract 테스트로,
- fail-fast 분류(미연결/모르는 node/수신측 spot 부재)와 재resolve 회복은 E2E Config 2
  (spot service)·Config 1(location messaging)로,
- 이동·재활성 경계는 spot takeover/재활성 시나리오로 검증한다.

전송 경로에 store 조회를 다시 넣는 변경, spot rid만 받아 내부 resolve하는 편의 overload,
전송 계층의 숨은 재시도는 이 스펙 위반이다.
