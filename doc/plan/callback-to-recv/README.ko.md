# `callback-to-recv` 계획 문서

> `superseded`
>
> 이 디렉터리는 callback surface를 축소하려던 이전 계획이다.
> 현재 구현/문서 기준의 source of truth는
> [`doc/plan/recv-with-callback/`](/home/hep7/project/kairos/zlink/doc/plan/recv-with-callback)
> 이다.
> 새 계획은 callback과 recv를 다시 공통 규칙으로 정렬하며,
> perf 정책도 `single=callback only`, `multi=recv only`,
> `SPOT`/`STREAM` dual-mode 예외, monitor callback 고정 기준으로 바뀌었다.

이 디렉터리는 `core/` 기준 public receive surface를 다시 정리하는 계획 문서를
모은다.

이번 계획의 고정 결정은 아래와 같다.

- `callback + recv` dual-mode 유지 대상
  - `STREAM`
  - `SPOT` / `SPOT node`
  - socket monitor
  - service monitor
- `send_ready` 유지 대상
  - `STREAM` callback 모드
  - `SPOT` / `SPOT node` callback 모드
  - recv 모드 또는 recv-only subject는 `poller POLLOUT` 사용
- `recv` only로 축소할 대상
  - raw `PAIR`
  - raw `DEALER`
  - raw `ROUTER`
  - raw `SUB`
  - raw `XSUB`
  - raw `XPUB`
  - `gateway`
- `send_ready` 제거 대상
  - raw `PAIR`
  - raw `PUB`
  - raw `XPUB`
  - raw `DEALER`
  - raw `ROUTER`
  - `gateway`
- perf dual-mode 측정 유지 대상
  - single: `SPOT`만 `recv` / `callback`
  - multi: `SPOT`, `STREAM`만 `recv` / `callback`
  - monitor는 callback 유지 대상이지만 perf pattern은 아니다

핵심 의도는 "모든 recv-capable subject에 callback을 일괄 제공"하는 방향을
되돌리고, 실제 사용자 가치가 큰 high-level/event surface에만 callback을
남기는 것이다.

여기서 핵심은 남길 handler 함수와 삭제할 handler 함수를 한 번에 정리하고,
남겨 두는 함수도 지원 대상을 축소하는 것이다.

- `zlink_recv_handler()`, `zlink_subscribe_handler()`,
  `zlink_send_ready_handler()`는 남기되 지원 대상을 축소한다.
- `zlink_subscription_event_handler[_fn]`는 호환성 단계 없이 이번 작업에서
  즉시 제거한다.

동시에 writable readiness 정책도 다시 고정한다.

- recv 모드의 readiness는 `poller POLLOUT`가 canonical이다.
- callback 모드의 readiness callback은 callback receive를 유지하는
  `STREAM`, `SPOT`, `SPOT node`에만 남긴다.
- low-level/raw surface에 대한 `send_ready` callback은 확대하지 않는다.

문서 목록:

- [core-surface-reduction-plan.ko.md](core-surface-reduction-plan.ko.md)
  - `core/include/zlink.h`, `core/src/`, `core/tests/` 기준 public surface 축소 계획
- [perf-policy-alignment-plan.ko.md](perf-policy-alignment-plan.ko.md)
  - `doc/perf/` 정책 문서와 `core/perf/` 구현 정렬 계획
