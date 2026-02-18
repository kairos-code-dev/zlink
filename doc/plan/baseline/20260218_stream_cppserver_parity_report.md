# STREAM 성능 개선 최종 리포트 (zlink vs cppserver)

Date: 2026-02-18
Workspace: `/home/hep7/project/kairos/zlink-perf-wt`

## 적용한 핵심 변경 (core-only)
1. I/O thread 선택 로직 개선 (accept 분산)
- `core/src/core/ctx.cpp`
- `core/src/core/ctx.hpp`
- 변경 내용: `choose_io_thread()`에서 affinity 미사용 시 strict round-robin으로 분산.
- 목적: 초기 accept burst에서 특정 I/O thread 편중 방지.

2. STREAM poll hot path 제거
- `core/src/api/zlink.cpp`
- 변경 내용: `zlink_poll()`의 STREAM POLLIN-only 우회 분기 제거, 표준 `get_events()` 경로 사용.
- 목적: STREAM에서 실제 처리 경로와 이벤트 상태 불일치로 인한 성능 저하 제거.

## 벤치 실행 조건
스크립트(동일):
`core/tests/scenario/stream/run_stream_compare.sh`

파라미터(동일):
- `--ccu 10000 --duration 5 --repeats 3 --inflight 1`
- `--client-io-threads 4 --server-io-threads 2`
- `--size 64,1024,65536`

## 비교 결과 (종료조건 판정 기준 런)
- cppserver: `doc/plan/baseline/20260218_rrtie_cpp_all/summary.json`
- zlink: `doc/plan/baseline/20260218_final_zlink_all/summary.json`

| size | cppserver throughput(msg/s) | zlink throughput(msg/s) | zlink/cpp | 판정 |
|---:|---:|---:|---:|---:|
| 64 | 365,124.2 | 372,251.6 | 101.95% | PASS |
| 1024 | 331,927.0 | 346,414.4 | 104.37% | PASS |
| 65536 | 70,516.8 | 76,755.0 | 108.85% | PASS |

결론(종료조건):
- **충족** (`64/1024/65536` 모두 `zlink >= cppserver`)

## 변동성 참고
동일 조건 재측정에서 런 간 변동이 존재함:
- cppserver 재확인: `doc/plan/baseline/20260218_final_cpp_all_confirm/summary.json`
- zlink 재확인: `doc/plan/baseline/20260218_final_zlink_all_rerun/summary.json`

운영/CI에서는 단일 런 PASS/FAIL보다 N회 반복 중앙값 기반 게이트를 권장.
