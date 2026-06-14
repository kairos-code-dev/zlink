# 라운드 3: SPOT publish ingress 직접 forward

- goal: `MULTI_SPOT` one-way 64B 회귀를 줄인다.
- 시작 시각: 2026-06-14 15:54:55 +0900
- 기준 commit: `1fcae8dc8`
- 시작 git status: `bindings/node/src/zlink/runtime/*` 변경과 새 perf baseline 산출물이 있음. core 소스 변경 없음.
- 기준 report: `bindings/c/perf/baseline/perf_c_multi_linux_20260513_101034.txt`
- 비교 report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260614_151925.txt`
- 대상 pattern/transport/size: `MULTI_SPOT` / `tcp,tls,ws,wss` / `64B`

## 가설

- 가설 1: public SPOT publish 경로는 호출 스레드에서 publish ingress queue에 넣은 뒤, data-plane 스레드에서 다시 staged queue로 복사하고 곧바로 flush한다. 정상 경로에서 이 두 번째 복사와 queue 왕복을 제거하면 64B one-way 처리량이 오른다.
- 가설 2: SPOT one-way 회귀는 local fanout 또는 mesh PUB send 직전 `pump_socket_commands()` 호출 빈도와 관련이 있다. 매 메시지마다 command pump를 반복하면 작은 메시지에서 비용이 커질 수 있다.
- 선택한 가설: 먼저 가설 1을 검증한다. 정상 경로의 의미는 유지하면서 backpressure가 걸릴 때만 staged queue에 남긴다.

## 읽은 코드

- `core/src/runtime/services/spot/pubsub/spot_subject_publish.cpp`: public SPOT publish가 `enqueue_publish_ingress()`로 들어간다.
- `core/src/runtime/services/spot/data_plane/spot_data_plane_forwarding.cpp`: publish ingress queue drain 후 `stage_message()`로 한 번 더 복사하고 `flush_staged_messages()`가 local/mesh forward를 수행한다.
- `core/src/runtime/services/spot/data_plane/spot_data_plane_loop.cpp`: data-plane 루프가 매 tick마다 publish ingress queue를 drain하고 pending output을 flush한다.
- `core/src/runtime/services/spot/data_plane/spot_data_plane_pending.cpp`: staged/pending queue에 남길 때 메시지 frame을 복사한다.

## 변경

- 예정 변경 파일: `core/src/runtime/services/spot/data_plane/spot_data_plane_forwarding.cpp`
- 변경 이유: 정상 publish ingress drain 경로에서 staged queue 복사와 즉시 재flush를 피한다.
- perf 전용 변경이 아닌 이유: public SPOT publish의 실제 data-plane hot path에서 불필요한 내부 queue 복제를 줄이는 변경이다.

## 보안 하드닝 보존 확인

- 참조 report: `core/doc/report/odl/2026-06-13-core-src-security-review.ko.md`
- 이번 변경이 건드린 보안 항목: 없음
- 보안 의미를 유지한 근거: 메시지 guard, WebSocket buffer, port parsing, IPC unlink, mtrie 처리는 건드리지 않는다.
- 추가로 실행한 회귀 테스트: 예정

## 검증

- build: 예정
- test: 예정
- targeted perf: 예정

## 결과

- 진행 중
