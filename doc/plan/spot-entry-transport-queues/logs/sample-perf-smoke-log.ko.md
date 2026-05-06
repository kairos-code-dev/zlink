# Sample And Perf Smoke Log

## 2026-05-06

- 날짜: 2026-05-06
- 대상: 단계 11 sample과 perf smoke
- 수행한 명령:
  - `cmake --build core/build`
  - `bindings/c/samples/run_samples.sh`
  - `bindings/c/perf/run_benchmarks.sh --msg-sizes 64`
  - `bindings/c/perf/run_benchmarks_multi.sh --msg-sizes 64`
- 확인한 draft spec 절: Actor와 Entry Spot 흐름, Gateway/session 흐름, Game room 흐름, Single-player 흐름
- 발견한 문제:
  - 없음.
  - smoke 기준은 기본 실행에서 패턴별 메시지 크기만 64로 제한하는 방식으로 정리했다.
- 수정한 파일:
  - `bindings/c/samples/actor_sample_common.h`
  - `bindings/c/samples/actor_room_server_sample.c`
  - `bindings/c/samples/actor_gateway_relay_sample.c`
  - `bindings/c/samples/actor_single_player_queue_sample.c`
  - `doc/spec/sample/SAMPLE_POLICY.md`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - core build 성공.
  - C sample runner 13/13 통과.
  - single perf smoke 성공. 결과 파일:
    `bindings/c/perf/results/single/report/perf_c_single_linux_20260506_101117.txt`
  - multi perf smoke 성공. 결과 파일:
    `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260506_101754.txt`
  - perf runner는 `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.7`
    runtime을 출력했다.
  - stale runtime guard는 runner 시작 시 실행됐다.
- 남은 위험: bindings native library는 core release 뒤 `bindings/update_zlink_libs.sh`로 최신화해야 한다.
- 다음 확인: 구현 후 문서-코드 반복 리뷰와 POSD gate
