# `bindings/python/perf` POSD 후속 리팩토링 계획

> 후속 검토 결론: Python perf는 이미 정책과 측정 의미에 맞게 정리되어 있어서, 큰 follow-up refactor 는 불필요하다.
> 다만 runner/bootstrap 경계에는 유지보수 수준의 coupling 이 남아 있으므로, 그 부분만 조건부로 재검토할 가치가 있다.
> 전제: perf 목적 범위 내, PERF 정책 준수, `core/perf` 와 동일한 측정 의미 유지.
> 대상: `bindings/python/perf/`

## TODO

- [ ] 현재 maintenance-only 판단 유지 여부 재검토
- [ ] `sys.path` 보정 중복 축소 필요성 재판정
- [ ] dynamic loader coupling 유지/분리 기준 확정
- [ ] `perf_metrics.py` nucleus 유지 근거 재확인
- [ ] py_compile 검증 완료
- [ ] single/multi smoke 재검증 필요 여부 판정
- [ ] 완료 정의 충족 여부 최종 리뷰
- [ ] follow-up 재개 조건 문서화

## 1. 목표

- 현재의 공통 유틸리티 단일 진입점을 유지한다.
- single callback-only, multi 의 정책상 허용 recv 조합을 그대로 보존한다.
- `core/perf` 와 동일한 측정 의미를 유지한다.
- runner/bootstrap 경계를 유지하되, 측정 경로에 불필요한 분해를 늘리지 않는다.

## 2. 현재 구조 요약

- [perf_metrics.py](/home/hep7/project/kairos/zlink/bindings/python/perf/perf_metrics.py) 가 callback metrics, payload header, endpoint helper, result metrics, report helper, socket wait helper를 한 곳에서 제공한다.
- [single/perf_common.py](/home/hep7/project/kairos/zlink/bindings/python/perf/single/perf_common.py) 와 [multi/perf_multi_common.py](/home/hep7/project/kairos/zlink/bindings/python/perf/multi/perf_multi_common.py) 는 같은 perf 디렉토리를 `sys.path` 에 1회 보정한 뒤 `perf_metrics` 를 재사용한다.
- [single/run_benchmarks.py](/home/hep7/project/kairos/zlink/bindings/python/perf/single/run_benchmarks.py) 는 `importlib.util.spec_from_file_location()` 으로 패턴 모듈을 직접 로드하고, 실행 범위에서만 `sys.path` 와 `PYTHONPATH` 를 잠깐 바꾼다.
- [multi/run_benchmarks.py](/home/hep7/project/kairos/zlink/bindings/python/perf/multi/run_benchmarks.py) 는 server/client subprocess orchestration 과 report assembly 를 담당한다.
- 최근 smoke 에서 single 과 multi callback 경로가 모두 `status: complete` 로 재검증되었다.

## 3. 남은 POSD 문제

### 3.1 import/bootstrap 경계의 보일러플레이트

`single/perf_common.py` 와 `multi/perf_multi_common.py` 는 둘 다 perf 디렉토리를 `sys.path` 에 삽입한 뒤 `perf_metrics` 를 import 한다.
이건 startup-only 코드라 hot path 는 아니지만, 동일 목적의 경로 보정이 두 군데에 반복된다.

근거:
- [single/perf_common.py](/home/hep7/project/kairos/zlink/bindings/python/perf/single/perf_common.py)
- [multi/perf_multi_common.py](/home/hep7/project/kairos/zlink/bindings/python/perf/multi/perf_multi_common.py)

POSD 관점 판단:
- 변경 증폭은 작다.
- 측정 의미는 바뀌지 않는다.
- 그러나 패키징/경로 규칙이 다시 바뀌면 같은 수정이 양쪽에 다시 퍼질 수 있다.

### 3.2 single runner 의 dynamic loader coupling

`single/run_benchmarks.py` 는 각 패턴을 `importlib.util.spec_from_file_location()` 으로 직접 로드하고, 실행 동안만 `sys.path` 와 `PYTHONPATH` 를 바꾼다.
이 구조는 runner 전용이며 측정 hot path 에는 들어가지 않지만, import/setup 의 소유권이 runner 내부에 숨어 있다.

근거:
- [single/run_benchmarks.py](/home/hep7/project/kairos/zlink/bindings/python/perf/single/run_benchmarks.py)

POSD 관점 판단:
- 숨은 coupling 은 있다.
- 다만 이것을 지금 별도 추상화로 분해하면 import churn 이 더 커질 가능성이 높다.
- 현재 perf 목표에는 영향을 주지 않으므로, maintenance-only 로 두는 편이 낫다.

### 3.3 `perf_metrics.py` 는 넓지만 아직 nucleus 로서 유효하다

`perf_metrics.py` 는 measurement primitive 와 report helper 를 함께 담고 있다.
하지만 이 파일은 runner 결과 포맷과 공통 측정 규칙의 nucleus 로서, 지금은 응집도가 유지된다.

근거:
- [perf_metrics.py](/home/hep7/project/kairos/zlink/bindings/python/perf/perf_metrics.py)
- `CallbackMetrics`, `result_metrics()`, `print_result_lines()`, `build_report_path()`, `wait_socket_event()`

POSD 관점 판단:
- 더 쪼갤 수는 있지만, 그 자체가 목표가 되면 import surface 만 늘어난다.
- 현재는 `core/perf` 와 같은 측정 의미를 잘 유지하고 있으므로, 구조 분해의 우선순위는 낮다.

## 4. 우선순위

- P0: major refactor 없음.
- P1: 새 패키징 규칙이나 새 runner 계약이 추가될 때만 bootstrap helper 분리를 재검토한다.
- P2: 새 transport, 새 metric policy, 새 report 포맷이 추가될 때만 `perf_metrics.py` 재분해 여부를 검토한다.

우선순위 기준은 성능 위험이 아니라 change amplification 이다.
측정 의미를 건드리지 않는 이상, 현재 경계는 유지하는 편이 더 낫다.

## 5. 단계별 작업

### 단계 0. 현재 경계 고정

- `perf_metrics.py` 를 공통 핵심으로 둔다.
- runner 파일은 패턴/프로세스 orchestration 만 담당하고, measurement semantics 는 건드리지 않는다.
- `sys.path` 보정과 dynamic import 는 유지하되, startup-only 범위에 묶어 둔다.

완료 기준:
- single/multi smoke 가 현재 결과와 동일하게 `status: complete` 를 유지한다.
- `core/perf` 와 다른 측정 의미가 새로 도입되지 않는다.

### 단계 1. 확장 시에만 재검토

- 새로운 패키징 규칙, 신규 패턴, 신규 report 포맷이 들어오면 bootstrap helper 분리 여부를 다시 본다.
- 그 전까지는 추가 분해를 하지 않는다.

완료 기준:
- 새 요구가 생기기 전까지는 코드 변경이 없다.
- follow-up refactor 는 정책 변화나 구조 변화가 생길 때만 재개한다.

## 6. 검증 방법

- `python -m py_compile /home/hep7/project/kairos/zlink/bindings/python/perf/perf_metrics.py /home/hep7/project/kairos/zlink/bindings/python/perf/single/*.py /home/hep7/project/kairos/zlink/bindings/python/perf/multi/*.py`
- `./run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64 --warmup 1 --duration 1`
- `./run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback --transports tcp --msg-sizes 64 --clients 4 --warmup 1 --duration 1`
- `rg -n "sys\\.path.insert|importlib\\.util\\.spec_from_file_location" /home/hep7/project/kairos/zlink/bindings/python/perf -g '*.py'`
- `rg -n "RESULT,current|status: complete" /home/hep7/project/kairos/zlink/bindings/python/perf/results -g '*.txt'`

## 7. 완료 정의

- 현재 공통 모듈 경계가 유지된다.
- single callback-only, multi recv/callback 정책이 그대로 유지된다.
- `RESULT,current` 포맷과 `core/perf` 와 동일한 측정 의미가 유지된다.
- bootstrap/loader coupling 은 maintenance-only 수준으로 판단된다.
- follow-up refactor 는 새 policy 요구나 새 패키징 요구가 있을 때만 재개한다.

## 8. 비범위

- hot path measurement 로직 재설계
- `perf_metrics.py` 를 명분 없이 쪼개는 작업
- callback-only 모델 재도입
- `core/perf` 와 다른 측정 의미 도입
- runner output 형식 변경

## 9. 결론

현재 Python perf 는 큰 POSD 후속 리팩토링을 진행할 필요가 없다.
남아 있는 coupling 은 runner/bootstrap 경계에 국한되며, 측정 의미나 정책 준수와 충돌하지 않는다.
따라서 이 디렉토리는 maintenance-only 로 두고, 패키징이나 신규 perf 요구가 생길 때만 bootstrap/helper 분리를 재검토하는 것이 맞다.
