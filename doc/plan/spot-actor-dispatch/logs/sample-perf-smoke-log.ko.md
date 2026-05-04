# Sample And Perf Smoke Log

- 날짜: 2026-05-04
- 대상: core sample/perf smoke
- 수행한 명령: 없음
- 발견한 문제: smoke 실행 전
- 수정한 파일: 없음
- 남은 위험: core 변경 뒤 `core/build` 기준 runtime rebuild 필요
- 다음 확인: 구현 완료 뒤 sample build와 perf smoke 실행

## 2026-05-05 core C API Actor sample smoke

- 대상: 단계 14, 단계 16 sample build, 단계 17 sample smoke
- 추가한 sample:
  - `bindings/c/samples/actor_room_server_sample.c`
  - `bindings/c/samples/actor_gateway_relay_sample.c`
  - `bindings/c/samples/actor_single_player_queue_sample.c`
- 수행한 명령:
  - `bindings/c/samples/run_samples.sh`
- 검증 결과:
  - C sample build 성공
  - 기존 sample 10개와 Actor sample 3개, 총 13개 sample smoke 통과
- 범위:
  - 새 sample은 core C API인 `zlink_spot_node_actor_new()`, `zlink_actor_join_spot()`,
    `zlink_spot_node_create_remote_actor()`, `zlink_stream_bind_actor()`,
    `zlink_stream_send_bound_actor_part()`, `zlink_actor_recv_part()`를 직접 사용한다.
  - binding별 typed Actor object, codec, DI, async runtime wrapper는 이 단계에서
    구현하지 않았다.
- 남은 위험:
  - perf smoke는 아직 실행하지 않았다.

## 2026-05-05 cleanup 후 sample/perf smoke

- 대상: 단계 17
- 수행한 명령:
  - `bindings/c/samples/run_samples.sh`
  - `./bindings/c/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_single`
  - `./bindings/c/perf/run_benchmarks_multi.sh --runs 1 --duration 1 --clients 8 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_multi`
- runtime 확인:
  - single runner: `Perf runtime libzlink`가
    `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`를 가리킴
  - multi runner: `Perf runtime libzlink`가
    `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`를 가리킴
  - stale runtime 오류 없이 시작함
- sample 결과:
  - C sample 13개 통과
  - Actor sample 3개가 runner에 포함되어 통과
- perf 결과:
  - single smoke 결과 파일:
    `bindings/c/perf/results/single/report/perf_c_single_linux_20260505_044543_actor_smoke_single.txt`
  - multi smoke 결과 파일:
    `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260505_044623_actor_smoke_multi.txt`
  - single: `status=complete`, expected result lines `120`, actual result lines `120`
  - multi: `status=complete`, success `32`, fail `0`, expected result lines `160`,
    actual result lines `160`
  - 지정 size `64`, `1024`, `4096`, `65536` 모두 성공
  - `SPOT`, `MULTI_SPOT`, `MULTI_SPOT_REQREP`, `MULTI_SPOT_SENDSEND`,
    `MULTI_STREAM` result line 확인
- 남은 위험:
  - 이 smoke는 실행 성공 여부만 확인한다. 수치 비교는 별도 perf 계획에서 다룬다.
