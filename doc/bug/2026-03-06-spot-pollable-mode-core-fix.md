# [Bug Fix Report] Service-instance poller direction aligned in core v4.0.0

- Date: 2026-03-07
- Repo: `/home/hep7/project/kairos/zlink`
- Baseline release: `core/v4.0.0`
- Runtime sync commit: `da1d308a`
- Runtime sync subject: `chore(bindings): sync runtimes for core v4.0.0`
- Release tag: `core/v4.0.0`

## 1) Summary

`SPOT pollable SUB` 관련 core ownership fix 는 유지된다. 다만 public 방향은 더 이상
socket handover 중심이 아니라 service instance poller 기준으로 정렬되었다.

핵심 변화는 다음과 같다.

1. poller 가 raw socket 뿐 아니라 service instance 를 직접 등록할 수 있다.
2. Spot/Gateway/Receiver 의 public 문서/샘플/고수준 wrapper 는 이 경로를 우선해야 한다.
3. `SpotNode.open_*()` 류 helper 확장 방향은 새 권장 API 가 아니다.

## 2) Core API changes

`core/include/zlink.h` 에 아래 public C API 가 추가되었다.

- `zlink_poller_add_spot_sub`
- `zlink_poller_add_spot_pub`
- `zlink_poller_add_gateway`
- `zlink_poller_add_receiver`
- 대응 `modify/remove` API 전부 추가

즉, public poller 등록 대상은 socket 이 아니라 service instance 다.

## 3) Binding direction

bindings 는 `core/v4.0.0` native/runtime 으로 이미 동기화되었다.

- 동기화 커밋: `da1d308a`
- 사용 기준: service instance poller
- 비권장 방향: `SpotNode.open_*()` helper 또는 socket handover wrapper 확대

정리하면:

- Spot: `Spot` service instance 를 poller 에 등록
- Gateway: `Gateway` service instance 를 poller 에 등록
- Receiver: `Receiver` service instance 를 poller 에 등록

## 4) Impact on docs and samples

문서/샘플/고수준 wrapper 에서 아래 원칙을 유지해야 한다.

1. poller 등록 대상은 service instance 로 쓴다.
2. socket accessor 는 내부 구현/저수준 경로가 정말 필요할 때만 제한적으로 다룬다.
3. public 예제는 `addSpotSub/addSpotPub/addGateway/addReceiver` 계열 흐름을 보여준다.

## 5) Verification status

사용자가 전달한 기준 검증 상태는 다음과 같다.

- core targeted tests 통과
- C++ bindings 70개 테스트 통과
- .NET 99개 테스트 통과
- Java `test` / `integrationTest` 통과
- Node 24개 테스트 통과
- Python 37개 테스트 통과

## 6) Practical note for perf

Java perf 구현도 같은 기준으로 맞춘다.

- `MULTI_SPOT`: `Spot` service instance + poller 등록
- `MULTI_GATEWAY`: `Receiver` service instance + poller 등록
- 핵심 send/recv 루프는 각 패턴 파일 안에서 명시적으로 유지

## 7) One-line handoff

> `core/v4.0.0` 부터 Spot/Gateway/Receiver 의 public poller 방향은 socket handover 가 아니라 service instance registration 이며, bindings runtime 은 `da1d308a` 기준으로 이미 동기화되어 있다.
