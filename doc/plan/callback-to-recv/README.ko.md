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

이 디렉터리는 callback surface를 축소하려던 이전 의사결정 기록으로 남겨 둔다.
현재 구현/테스트/문서 정렬은 이 문서를 기준으로 진행하지 않는다.

현재 기준에서 확인해야 할 내용은 모두
[`doc/plan/recv-with-callback/`](/home/hep7/project/kairos/zlink/doc/plan/recv-with-callback)
에 정리되어 있다.

정리 원칙:

- 이 디렉터리는 historical context다.
- 현재 source of truth는 `recv-with-callback`이다.
- 구현자는 이 문서의 축소 방향을 다시 코드에 적용하면 안 된다.
- callback/recv 공통 규칙, `EBUSY` gate, perf canonical lane은 모두 새 디렉터리의
  계획을 따른다.

문서 목록:

- [core-surface-reduction-plan.ko.md](./core-surface-reduction-plan.ko.md)
  - 과거의 public surface 축소안
- [perf-policy-alignment-plan.ko.md](./perf-policy-alignment-plan.ko.md)
  - 과거 perf 축소 정책 정렬안
