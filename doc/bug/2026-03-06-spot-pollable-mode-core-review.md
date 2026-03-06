# [Core Bug Report] Spot pollable transport mode is not yet safe for multi perf

- Date: 2026-03-06
- Repo: `/home/hep7/project/kairos/zlink`
- Reporter area: `.NET perf` review with cross-check against Java/C++ perf reports
- Severity: High

## Summary

`Spot`의 pollable transport mode는 현재 core 계약이 완결되지 않았다고 판단합니다.

정확히는:

- `process_sub()`를 pollable mode에서 skip하도록 바꿔도
- 순수 pollable path에서 core/control-thread가 `_sub`를 여전히 건드릴 수 있고
- 결과적으로 Java/C++ multi perf 경로에서 abort / heap corruption / SIGFPE가 보고되고 있습니다

이 상태에서는 `SpotNode.pub/sub socket`을 정식 pollable transport handle로 승격한 의미와
실제 구현이 아직 일치하지 않습니다.

## Why this is a core bug

현재 정책은 다음입니다.

- `SpotNode.pubSocket()/subSocket()` 호출 시 pollable mode 진입
- facade `Spot.publish()/recv()/subscribe()`와 혼용 금지
- multi perf `SPOT`는 facade가 아니라 pollable socket 기반 event-loop 경로를 사용해야 함

즉 이 경로는 "고급 사용 예시"가 아니라, 이미 core/binding contract가 허용한 정식 경로입니다.

그런데 순수 pollable path에서 바인딩별로 다음 현상이 관측됩니다.

- Java: native heap corruption / abort
- C++: `fq_t::recvpipe()` 경로 SIGFPE 보고
- .NET: 기존에는 allocator corruption / assert, 현재 local patch 후에도 perf-level lifecycle이 불안정

공통점은 전부 `Spot pollable sub recv` 중심 경로라는 점입니다.

## Repro status by stack

### 1. Java report

문서:
- [2026-03-06-multi-spot-pollable-mode-core-report.md](/home/hep7/project/kairos/zlink/doc/bug/2026-03-06-multi-spot-pollable-mode-core-report.md)

핵심:
- facade 혼용 없음
- `node.subSocket()` + raw `SUBSCRIBE`
- `Poller + recv(DONTWAIT)` drain
- client abort:
  - `corrupted double-linked list`
  - `malloc(): unsorted double linked list corrupted`

### 2. C++ report

문서:
- [2026-03-06-spot-pollable-and-native-runtime.md](/home/hep7/project/kairos/zlink/doc/bug/2026-03-06-spot-pollable-and-native-runtime.md)

핵심:
- facade 제거 후 raw `pub/sub` socket만 사용
- client abort / `SIGFPE`
- Valgrind stack:
  - `zlink::fq_t::recvpipe()`
  - `zlink::xsub_t::xrecv()`
  - `zlink_msg_recv()`

### 3. .NET review result

제가 직접 확인한 범위:

- `SpotNode::process_sub()`가 pollable mode에서 `_sub`를 소비하지 않도록 바꾼 뒤
  targeted core test는 통과:
  - `ctest --test-dir build_mode --output-on-failure -R 'test_spot_mode_split$'`
- 최소 C API repro:
  - `spot_sub_subscribe()`
  - `zlink_spot_node_sub_socket()`
  - raw `zlink_msg_recv()`
  - clean exit
- 최소 C# repro:
  - `Spot.Subscribe("bench")`
  - `SpotNode.GetSubSocket()`
  - raw `Socket.Receive()`
  - clean exit

하지만 이 두 최소 repro는 **순수 pollable path**가 아니라,
`subscribe`를 facade/control-plane에 의존하는 경로입니다.

반면 Java/C++에서 재현된 건:

- `spot_sub_subscribe()/Spot.subscribe()`가 아니라
- raw sub socket에 직접 `SUBSCRIBE`
- raw `recv(dontwait)` drain

즉, 제가 성공시킨 최소 repro만으로는 순수 pollable path의 안전성을 증명할 수 없습니다.

## Suspected root cause

현재 core에서 pollable sub mode에 대해 보장되는 것은 일부뿐입니다.

이미 수정한 부분:
- `process_sub()`는 `_sub_pollable_mode != 0`이면 skip
  - [spot_node.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_node.cpp)

하지만 여전히 control thread 경로에서 `_sub`를 건드립니다.

의심 구간:
- `flush_pending()`:
  - `_sub->setsockopt(...)`
  - `_sub->connect(...)`
  - `_sub->term_endpoint(...)`
- control tick 경로에서 `flush_pending()` 호출
  - [spot_node.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_node.cpp)

즉 현재 pollable mode는:

- application thread가 raw `_sub`를 ownership 가진다고 문서화됐지만
- core control thread도 같은 `_sub`에 대해 side-effect를 계속 수행할 수 있음

이건 `recv ownership`만의 문제가 아니라 `_sub` 객체 전체 ownership이 정리되지 않았다는 뜻입니다.

## Expected

`SpotNode.subSocket()/pubSocket()`을 획득해 pollable mode로 들어간 뒤에는,

- application event loop가 data-plane socket ownership을 갖고
- core control thread는 해당 socket에 대해 unsafe concurrent mutation을 하지 않아야 합니다

적어도 다음은 정리돼야 합니다.

1. pollable mode 진입 후 `_sub` / `_pub`에 대한 control-thread mutation 범위
2. raw `SUBSCRIBE` 사용 허용 여부
3. 허용한다면, facade subscription registry 없이도 안정적으로 동작해야 함
4. 허용하지 않는다면, 현재 mode-split 계약과 예제/바인딩 정책을 수정해야 함

## Actual

현재는 순수 pollable path에서 다음 cross-stack signal이 있습니다.

- Java: heap corruption abort
- C++: SIGFPE report
- .NET: 동일 계열 실패 이력 + perf-level instability

따라서 `Spot pollable transport mode`는 아직 "정식 안전 경로"라고 보기 어렵습니다.

## Requested actions

1. `Spot` pollable mode ownership 계약을 core에서 명확히 완성
   - `_sub` / `_pub`에 대해 control thread가 무엇을 해도 되는지 재정의
2. `flush_pending()`와 control tick이 pollable mode socket에 미치는 영향 점검
3. 순수 pollable path 기준 regression test 추가
   - raw `SUBSCRIBE`
   - raw `recv(DONTWAIT)` sustained loop
   - multi-client (`1`, `10+`) warmup/active phase
4. 문서 보강
   - raw `SUBSCRIBE` 허용/비허용
   - `connectPeerPub`, `subSocket()`, `SUBSCRIBE`, `poll/recv` 호출 순서 계약

## Notes

- 이 리포트는 `packaging mismatch` 문제와 별개입니다.
- `bindings/cpp/native`의 `libzlink.so` vs `libzlink.so.5` 불일치도 실제 버그이지만,
  이 문서는 `Spot pollable transport mode` 자체의 안전성 문제만 다룹니다.
