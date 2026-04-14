[스펙 목차](../README.ko.md)

# Draft -- Socket Default Revision

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 기본 동작 변경을
> 보장하지 않는다.
> 구현과 공개 헤더, errno 계약, 관련 spec 문서가 확정되면 정식 spec 문서에
> 합친다.

## 1. 목적

이 초안은 일부 소켓 옵션의 **기본값**을 더 안전하고 예측 가능한 방향으로 바꾸는
안을 정리한다.

이 초안이 다루는 핵심 목표는 아래와 같다.

- raw `ROUTER`가 peer 충돌과 잘못된 지정 송신을 더 일찍 surface하게 한다.
- `PUB`/`XPUB` 계열이 과부하 상황에서 조용히 drop하기보다 실패를 드러내게 한다.
- 사용자가 옵션을 일일이 켜지 않아도, 운영에서 더 보수적인 기본 동작을
  얻을 수 있게 한다.

## 2. 범위

이 초안은 아래 세 옵션의 기본값 변경만 다룬다.

- `ZLINK_ROUTER_OPT_HANDOVER`
- `ZLINK_ROUTER_OPT_MANDATORY`
- `ZLINK_PUB_OPT_NODROP`

이 문서는 새 옵션을 추가하지 않는다.
또한 각 옵션의 의미 자체를 바꾸는 것이 아니라, **기본값만 바꾸는 방향**을
다룬다.

## 3. 현재 기본값과 제안 값

현재 구현 기준 기본값은 아래와 같다.

| 옵션 | 현재 기본값 | 제안 기본값 |
|---|---|---|
| `ZLINK_ROUTER_OPT_HANDOVER` | `0` | `1` |
| `ZLINK_ROUTER_OPT_MANDATORY` | `0` | `1` |
| `ZLINK_PUB_OPT_NODROP` | `0` | `1` |

현재 기본값 표는 내부 정리 문서에도 같은 방향으로 적혀 있다.

- `ROUTER_MANDATORY = 0`
- `ROUTER_HANDOVER = 0`
- `PUB_NODROP = 0`

이 초안은 위 세 값을 모두 기본 활성화하는 방향을 제안한다.

## 4. 옵션별 의도

### 4.1 ROUTER_HANDOVER 기본 활성화

`ROUTER_HANDOVER`를 기본 활성화하면, 같은 peer identity로 새 연결이 들어왔을 때
기존 연결을 새 연결이 인수하는 방향이 기본 동작이 된다.

이 초안이 기대하는 효과는 아래와 같다.

- duplicate peer identity가 생겼을 때 기본 동작이 더 일관되게 보인다.
- discovery 기반 자동 연결이나 재연결 상황에서 기존 pipe를 조용히 붙잡는
  surprising case를 줄인다.
- `routing_id` 충돌 또는 재접속이 생겼을 때 새 연결을 더 자연스럽게 받아들인다.

다만 이 옵션이 peer identity 충돌 자체를 해결해 주는 것은 아니다.
충돌이 일어났을 때 어떤 pipe를 남길지의 기본 선택을 바꾸는 것이다.

### 4.2 ROUTER_MANDATORY 기본 활성화

`ROUTER_MANDATORY`를 기본 활성화하면, ROUTER가 도달할 수 없는 peer로 directed
send를 시도했을 때 조용히 넘어가지 않고 실패를 surface한다.

이 초안이 기대하는 효과는 아래와 같다.

- 잘못된 `routing_id` 대상 지정이 더 빨리 드러난다.
- peer가 내려갔거나 아직 붙지 않은 상태에서 send가 묵살되는 일을 줄인다.
- 호출자가 submit 실패를 보고 재시도나 fallback을 더 명시적으로 결정할 수 있다.

즉 이 변경은 "실패를 감춘다"보다 "실패를 드러낸다"에 가깝다.

이 옵션은 submit 실패만 바꾸는 것이 아니다. 현재 구현에서는 `MANDATORY`가
꺼져 있으면 ROUTER가 사실상 항상 writable처럼 보일 수 있지만, 켜져 있으면
실제로 쓸 수 있는 peer가 있을 때만 write-ready가 된다.

따라서 기본 활성화 후에는 아래 관찰도 함께 달라질 수 있다.

- `POLLOUT` 또는 send-ready 관찰값
- non-blocking send를 보내기 전 readiness 판단
- monitor나 event loop가 보는 writable 상태

### 4.3 PUB_NODROP 기본 활성화

`PUB_NODROP`를 기본 활성화하면, HWM에 걸렸을 때 메시지를 조용히 버리지 않고
실패를 surface한다.

이 초안이 기대하는 효과는 아래와 같다.

- slow subscriber 때문에 생긴 손실이 호출자에게 보인다.
- 중요 이벤트가 조용히 유실되는 기본 동작을 줄인다.
- 응용이 backpressure를 다루거나 실패를 기록할 기회를 갖는다.
- 손실 허용을 통해 진행률을 우선하던 workload는 throughput과 지연 특성의
  tradeoff가 달라질 수 있다.

즉 이 변경은 "기본 fan-out은 best-effort drop 허용"에서
"기본 fan-out도 실패를 드러낸다" 쪽으로 무게 중심을 옮긴다.

다만 `SpotNode` 내부 fanout 경로는 이미 내부 `XPUB_NODROP = 1`을 강제로 쓰는
부분이 있다. 따라서 이 초안의 직접 영향은 아래 쪽이 더 크다.

- 공개 `PUB` / `XPUB`
- 공개 `spot-pub` / `spotnode-pub`

즉 `SpotNode` 내부 데이터면 전체가 새 의미로 바뀐다고 보기보다, 공개 publish
surface의 기본 동작이 더 일관되게 바뀐다고 이해하는 편이 맞다.

## 5. 관찰 가능한 동작 변화

기본값이 바뀌면 사용자가 옵션을 명시하지 않았을 때 아래 동작이 달라진다.

### 5.1 ROUTER

- `zlink_send_rid()`가 잘못된 대상이나 미연결 peer에 대해 더 자주 실패를
  반환할 수 있다.
- duplicate peer identity가 들어오면 기본적으로 handover가 작동할 수 있다.
- 기존에는 조용히 지나가던 케이스가 이제는 submit 실패나 peer 교체로
  관찰될 수 있다.
- writable / `POLLOUT` 관찰값도 이전보다 더 보수적으로 바뀔 수 있다.

### 5.2 PUB / XPUB / spot-pub

- HWM 상황에서 조용한 drop 대신 `EAGAIN` 계열 실패가 surface될 수 있다.
- 호출자는 이전보다 backpressure와 실패 경로를 더 자주 보게 될 수 있다.
- 응용이 성공을 가정하고 있던 publish 경로는 호환성 영향을 받을 수 있다.

## 6. 호환성 영향

이 초안은 호환성 영향이 큰 변경으로 본다.

### 6.1 ROUTER_MANDATORY

기존에는 명시적으로 켜지 않으면 도달 불가 directed send가 조용히 지나갈 수
있었다. 기본 활성화 후에는 그 자리에 submit 실패가 보일 수 있다.

따라서 아래 같은 기존 응용이 영향을 받을 수 있다.

- 실패를 신경 쓰지 않고 best-effort routed send를 하던 코드
- `NOT_CONNECTED` 또는 내부 `EHOSTUNREACH`를 처리하지 않던 코드

### 6.2 ROUTER_HANDOVER

기존에는 duplicate peer identity가 들어와도 기존 연결이 남는 방향이 기본이었다.
기본 활성화 후에는 새 연결이 기존 연결을 인수하는 방향이 기본이 된다.

따라서 아래 같은 기존 관찰이 달라질 수 있다.

- 어떤 pipe가 살아남는가
- reconnect 직후 어떤 peer session이 active로 보이는가
- duplicate peer가 들어왔을 때 monitor에서 보이는 연결 전이 순서

### 6.3 PUB_NODROP

기존에는 과부하 상황에서 publish가 성공처럼 보이더라도 일부 subscriber에 대한
전달은 drop될 수 있었다. 기본 활성화 후에는 publish 호출 자체가 실패를 더 자주
surface할 수 있다.

따라서 아래 같은 기존 응용이 영향을 받을 수 있다.

- publish는 항상 성공한다고 가정한 코드
- `EAGAIN` 또는 `BACKPRESSURED` 처리를 하지 않던 코드
- fan-out 손실을 허용하는 대신 진행률을 중시하던 코드

## 7. 문서 반영 범위

이 변경이 구현되면 아래 문서들이 함께 갱신되어야 한다.

- `doc/spec/core/socket/router.ko.md`
- `doc/spec/core/socket/router.md`
- `doc/spec/core/socket/pub.ko.md`
- `doc/spec/core/socket/pub.md`
- `doc/spec/core/socket/xpub.ko.md`
- `doc/spec/core/socket/xpub.md`
- `doc/spec/core/errno-map.ko.md`
- `doc/spec/core/errno-map.md`
- `doc/internals/socket-option-defaults.ko.md`
- `doc/internals/socket-option-defaults.md`

필요하면 guide 문서에도 "예전보다 submit 실패가 기본적으로 더 자주 surface된다"는
운영 영향 설명을 추가할 수 있다.

또한 `ROUTER_MANDATORY`와 `PUB_NODROP` 변화는 readiness와 backpressure
관찰값까지 바꾸므로, monitor / polling 관련 가이드나 예제가 있다면 함께
검토하는 편이 좋다.

## 8. 비목표

이 초안은 아래를 직접 바꾸지 않는다.

- 옵션의 공개 상수 이름
- 각 옵션의 on/off 의미
- retry 정책이나 application-level recovery 정책
- admission state 또는 auto connect 정책

즉 이 문서는 새 기능 정의가 아니라 **기본 프로파일 변경**을 다룬다.

## 9. 미결 사항

구현 전에 아래 사항은 더 확정해야 한다.

- 세 옵션을 한 번에 바꿀지, 단계적으로 바꿀지
- major/minor release 중 어느 시점에 기본값 변경을 허용할지
- `PUB_NODROP = 1`이 spot 내부 pub 기본값과 어떤 관계를 가지는지
- guide 문서에서 migration note를 별도로 둘지
- monitor/diagnostic 문서에 기본 handover 영향 설명을 추가할지
- `ROUTER_MANDATORY = 1` 기본화에 따른 writable / `POLLOUT` 의미 변화를
  얼마나 전면에 문서화할지

## 10. 구현 순서 메모

이 절은 구현 전 초안의 **비규범 작업 메모**다.

세 초안 전체를 함께 본다면, 이 변경은 구현 순서상 **1순위**로 보는 편이
자연스럽다.

- 기본값 소스 위치가 비교적 명확하다.
- Discovery나 peer 상태 전파 같은 새 제어면 추가가 필요하지 않다.
- 호환성 영향은 크지만, 구현 자체는 다른 초안보다 독립적이다.

## 11. 회귀 테스트 포인트

이 절은 구현 전 초안의 **비규범 검증 메모**다. 공개 계약을 새로 정의하지는
않고, 구현 후 어떤 관찰 항목을 회귀 테스트로 확인해야 하는지 정리한다.

- 옵션을 명시적으로 설정하지 않은 새 `ROUTER`가 기본적으로
  `MANDATORY=1`, `HANDOVER=1` 상태로 동작하는지 확인한다.
- 옵션을 명시적으로 설정하지 않은 새 `PUB`/`XPUB`가 기본적으로
  `NODROP=1` 상태로 동작하는지 확인한다.
- 기본값 상태에서 `zlink_send_rid()`가 미연결 peer에 대해 조용히 성공하지 않고
  `NOT_CONNECTED` 계열 실패를 surface하는지 확인한다.
- duplicate peer identity가 들어오면 기본값 상태에서 handover가 실제로
  작동하는지 확인한다.
- HWM 상황에서 기본값 상태의 `publish`가 조용히 drop하지 않고 `EAGAIN` 계열
  실패를 surface하는지 확인한다.
- 사용자가 각 옵션을 명시적으로 `0`으로 설정하면 이전 동작으로 되돌아가는지
  확인한다.
