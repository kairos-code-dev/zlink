<!-- framework-adapter-nav:start -->
[스펙 목차](../README.ko.md) | [이전: Spot Actor Join / Transfer 공통 스펙](23-spot-actor.ko.md) | [다음: Stage Wrapper On SPOT](25-stage-wrapper-on-spot.ko.md)
<!-- framework-adapter-nav:end -->


# SpotHandle 기반 메시징

이 문서는 spot/actor 대상 메시징의 언어 중립 공통 스펙이다. **논리 전송 대상
(`SpotHandle`), 주소 갱신, 메시징 표면과 실패 계약**의 의미를 소유한다.
위치 저장·resolver·자동 연결의 하부 계약은
[location runtime](40-location-runtime.ko.md)이 소유하고 이 문서는 반복하지 않는다.

> 설계의 제1원칙: **호출자는 transport 주소의 수명과 재조회 순서를 관리하지 않는다.**
> resolver가 반환한 handle이 논리 대상과 주소 snapshot을 함께 소유하고, framework가
> 안전하게 재전송할 수 있는 실패에서만 주소를 한 번 갱신한다.

## 1. SpotHandle 모델

`SpotHandle`은 mesh 안의 논리 spot 하나를 가리키는 불투명한 capability다. 호출자는
resolver로 handle을 얻어 보관하고 send/request에 전달한다. handle의 public 정보는
논리 `SpotRid`뿐이며 owner node, generation, lease와 연결 상태는 framework가 관리한다.
handle은 특정 activation generation이 아니라 논리 spot identity를 가리킨다. 같은
`SpotRid`가 정상적으로 다시 활성화되면 갱신된 handle은 새 activation을 대상으로 한다.
특정 generation에만 유효한 작업은 domain request에 generation이나 idempotency key를
포함해야 한다.

`SpotRef`는 owner node rid와 spot rid를 담는 framework 내부 주소 snapshot이다. location
store 구현과 runtime 진단에서는 이 값을 사용할 수 있지만 application 메시징 API의
인자나 resolver 결과로 노출하지 않는다.

- handle은 resolver가 선택한 mesh와 논리 key를 기억한다.
- handle은 현재 유효한 내부 `SpotRef` snapshot을 원자적으로 교체할 수 있다.
- application은 owner node rid, generation과 endpoint를 읽거나 바꾸지 않는다.
- entry spot도 같은 handle 계약을 사용하며 별도 주소 특례를 public API에 두지 않는다.
- handle은 thread-safe하거나 해당 언어의 동등한 동시 접근 안전성을 제공해야 한다.
- handle은 caller disposal을 요구하지 않는다. location event subscription은 runtime이
  공유하며 handle 하나마다 독립 listener나 background task를 만들지 않는다.

## 2. 조회 표면

메시징 조회는 [location runtime §5](40-location-runtime.ko.md)의 두 resolver다. 모든 조회가
store에 도달하고 owner lease join으로 유효성을 판정한다.

| resolver | 입력 | 반환 |
|----------|------|------|
| Spot handle resolver | spot rid | `SpotHandle?` |
| actor Spot handle resolver | 전역 actor key | actor가 위치한 spot의 `SpotHandle?` |

- actor 1:1 spot topology에서는 호출자가 아는 것이 transient한 spot rid가 아니라 actor
  id이므로 actor Spot handle resolver가 1차 조회 표면이다. spot rid 조회는 spot rid를
  도메인 key에서 파생하는 topology(player owner spot 등)에서 쓴다.
- 재연결·"없으면 생성"·takeover 같은 lifecycle 흐름은 주소가 아니라 generation을 포함한
  location row가 필요하므로 resolver가 아니라 store/runtime 경로를 쓴다.
- 없는 대상, owner lease가 만료된 대상은 null이다.

## 3. 메시징 표면과 사용 모델

Spot 실행 문맥의 outbound와 외부 route client는 `SpotHandle`과 메시지만 받는다. handle이
전송 mesh를 소유하므로 caller가 route channel을 함께 고르지 않는다. 정확한 함수 이름과 call 반환 타입은
언어별 스펙에서 고정한다.

- spot rid와 node rid를 나란히 받는 전송 overload는 없다.
- 첫 resolve와 이후 주소 갱신은 handle이 소유한다. outbound는 handle의 내부 snapshot을
  사용하되 application에 조회 순서를 요구하지 않는다.
- **정상 전송 경로는 store를 읽지 않는다.** 전송은 handle의 in-memory snapshot만 본다.
  snapshot 갱신 트리거는 셋뿐이다: ① location 변경 event ② framework가 소유한 **주기적
  재조회**(살아 있는 handle 키를 polling interval마다 store에서 다시 읽는 백그라운드 작업)
  ③ 안전한 stale 실패 시 resolver 1회 호출. ②는 전송 경로가 아니라 runtime의 백그라운드
  작업이며, **주소에 TTL cache를 두는 것과는 다르다** — 임의 TTL cache는 두지 않는다
  ([40 location runtime §1](40-location-runtime.ko.md)).
- **원격 spot 전달의 wire form은 route socket 위의 spot route bridge relay framing
  하나로 고정한다.** 소켓 수준 framing과 혼용하면 수신 pump가 한쪽을 일반 envelope로
  오인해 drop한다. 포팅 언어는 이 framing 하나만 구현한다. 이 고정은 framework node 간
  route channel 평면에 한정하며, 외부 connector client가 spot node router 평면으로 직접
  보내는 inbound는 기존 계약 그대로다.

사용 패턴은 **handle 조회 후 재사용**이다. 호출자는 handle만 보관하며 주소 snapshot,
location event 구독과 stale 갱신 시점은 framework가 관리한다.

## 4. stale 주소 실패 계약

owner 이동, node 장애, 정상 lifecycle의 spot destroy 후 보유 주소는 낡는다. 그때의
실패는 **구분 가능**해야 한다 — 이것이 이 설계의 핵심 계약이다.

| 경로 | framework 동작 |
|------|----------------|
| **request** | handler 미실행이 확정된 stale 실패이면 **handle을 한 번 갱신하고 한 번 재전송한다.** 두 번째 실패는 typed error로 반환한다. **timeout은 재전송하지 않는다** |
| **send** | 최신 handle snapshot으로 **best-effort 전송한다.** location event가 도착하면 이후 전송에 새 snapshot을 사용한다. **전달 여부를 확인하기 위한 숨은 request를 만들지 않는다** |

### 4.1 request 실패 분류 — fail-fast

이 spot request 분류는 기존 `ZLinkFrameworkErrorKind`만 쓴다(이 표를 위한 새 종류를 추가하지 않는다).
actor 대상 표면의 실패 분류는 [framework API 오류 계약](../05-framework-api.ko.md)을 따른다.

| 상태 | 판정 위치 | framework 동작 | 최종 결과 |
|------|-----------|----------------|-----------|
| local runtime이 대상 mesh 미참여 | local | 갱신하지 않음 | 구성 오류 |
| mesh가 모르는 node rid | local | handle 갱신 후 한 번 재전송 | 다시 실패하면 `RequestTargetNotFound` |
| node는 알지만 미연결 | local | 기존 send readiness 한계 안에서 연결 수렴을 기다림 | 한계를 넘으면 request는 `RouteNotConnected`, **one-way send는 timeout 오류** |
| spot route bridge가 아직 준비되지 않음 | local | 대기하지 않음 | 즉시 `RouteNotConnected` |
| node 도달, spot 부재 | 수신측 | handler 미실행을 확인하고 handle 갱신 후 한 번 재전송 | 다시 실패하면 `SpotRouteNotFound` |
| 전송 후 무응답 | timeout | 재전송하지 않음 | timeout |

- "모르는 node"와 "미연결"의 로컬 판정은 자동 연결 reconciler의 **mesh 구성원 snapshot**과
  소켓 연결 상태에서만 나온다. 기준은 desired dial set이 아니라 **구성원 전체**다 —
  pairwise initiator 때문에 상대가 나를 dial하는 peer는 desired set에 없지만 rid 지정
  가능한 도달 대상이다. 이 판정을 위해 전송 경로에서 store를 읽지 않는다.
- 자동 연결 없이 수동 connect만 쓰는 구성은 snapshot이 없으므로 이 구분 없이 기존
  동작(연결 수렴 대기)을 유지한다.
- 연결 수렴 대기는 기존 send readiness와 timeout 경계 안에서 framework가 처리한다.
  application에 별도 warmup, sleep 또는 재시도 helper를 요구하지 않는다.

### 4.2 자동 갱신의 한계

framework의 자동 동작은 **주소 갱신 1회와 안전한 재전송 1회**로 제한한다. handler 실행
여부가 불확실한 timeout, cancellation과 연결 종료 뒤에는 자동 재전송하지 않는다.
도메인 idempotency가 필요한 일반 retry는 application 정책이며 handle이 대신하지 않는다.

### 4.3 경계와 보정

- destroy된 spot의 handle을 갱신해도 대상이 없으면 target-not-found 오류를 반환한다.
  재활성(placement)할지 포기할지는 도메인 결정이다.
- **spot 이동·재활성은 메시지 순서·전달 경계다.** 이동 창에서 이전 주소로의 in-flight
  전송은 drop될 수 있고 새 주소 전송은 도달하므로, 이동을 가로지르는 전달 순서와 전달
  자체를 보장하지 않는다. 순서에 민감한 도메인은 dedupe·reconcile 보정으로 흡수한다.
- send-only handle도 location event가 도착하면 snapshot을 갱신한다. event가 유실되거나
  늦게 도착한 구간의 one-way 전달은 보장하지 않는다.
- handle 갱신 직후에도 store 반영 지연 동안 이전 주소가 나올 수 있다. 자동 갱신은 한
  번으로 끝나며 같은 호출 안에서 반복 조회하지 않는다.

## 5. 회귀 기준

- resolver stale 제외·handle snapshot 갱신은 언어 공통 unit/contract 테스트로,
- fail-fast 분류와 안전한 1회 갱신은 E2E Config 2
  (spot service)·Config 1(location messaging)로,
- 이동·재활성 경계는 spot takeover/재활성 시나리오로 검증한다.

모든 정상 전송에서 store를 읽는 변경, handler 실행 여부가 불확실한 요청을 자동 재전송하는
변경, 갱신 횟수를 무제한으로 늘리는 변경은 이 스펙 위반이다.
