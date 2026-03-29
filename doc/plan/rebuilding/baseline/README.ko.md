# Baseline Result Files

이 디렉터리는 rebuilding 작업에서 직접 참조하는 baseline 성능 결과 파일을
모아 두는 위치다.

현재 보관 중인 기준 파일:

- `perf_linux_20260329_200606.txt`
  - single compare baseline
  - pattern: `PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER`
  - transport: `tcp,ipc,inproc`
- `perf_linux_20260329_155434.txt`
  - single zlink-only `tcp,ws,wss,tls`
  - pattern: `PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER`
- `perf_linux_20260329_162318_multi_baseline_20260329.txt`
  - multi compare baseline
  - pattern: `dealer_dealer,dealer_router,router_router,pubsub,stream`
- `perf_linux_20260329_163829_multi_stream_ws_wss_tls_baseline_20260329_rerun.txt`
  - multi `stream` zlink-only `ws,wss,tls` baseline

주의:

- `2026-03-05` single compare anchor raw result txt 두 개는 현재 worktree에
  존재하지 않는다.
- 해당 anchor의 authority는
  [feature-port-from-20260305-baseline.ko.md](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/feature-port-from-20260305-baseline.ko.md)
  에 기록된 실행 명령과 기준 throughput 수치다.
