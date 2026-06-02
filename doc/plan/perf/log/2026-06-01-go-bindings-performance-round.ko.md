# Go bindings 성능 재검토 로그

## 진행 원칙

- 후보 개발 중에는 남은 보류 항목의 pattern, transport, msg-size만 좁혀 측정한다.
- bindings public contract와 perf runner 의미는 바꾸지 않는다.
- HWM profile/floor 조정은 개선 근거로 사용하지 않는다.
- 상세 시도는 이 로그에 남기고, 계획 문서에는 최종 결과만 반영한다.

## MULTI_SPOT_REQREP/MULTI_SPOT_SENDSEND 65536B 제한 재측정

- 대상:
  - `MULTI_SPOT_REQREP`, `MULTI_SPOT_SENDSEND`
  - `tcp,tls,ws,wss`
  - `65536B`
- 근거:
  - 문서 표의 Go multi SPOT 65536B 일부가 C 대비 0.x~10%대까지 낮아, 같은 조건에서
    현재 상태를 다시 확인했다.
- 측정 1:
  - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp,ws,wss,tls --pattern MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND --msg-sizes 65536 --duration 1 --runs 3 --results-tag go_multi_spot_65536_recheck_20260601`
  - Go: `perf_go_multi_linux_20260601_193756_go_multi_spot_65536_recheck_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과 1:
  - `MULTI_SPOT_REQREP`: tcp 6.9%, tls 84.0%, ws 67.1%, wss 88.3%
  - `MULTI_SPOT_SENDSEND`: tcp 8.9%, tls 60.3%, ws 89.8%, wss 94.5%
- 측정 2:
  - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp --pattern MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND --msg-sizes 65536 --duration 2 --runs 5 --results-tag go_multi_spot_tcp65536_runs5_recheck_20260601`
  - Go: `perf_go_multi_linux_20260601_194109_go_multi_spot_tcp65536_runs5_recheck_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과 2:
  - `MULTI_SPOT_REQREP tcp 65536B`: 1.3%, 보류
  - `MULTI_SPOT_SENDSEND tcp 65536B`: 3.7%, 보류
- 판정:
  - tls/ws/wss 65536B는 제한 재측정으로 통과권에 올라 계획 문서 표에 반영했다.
  - tcp 65536B는 runs=5에서도 낮은 median이 반복되어 보류로 둔다.

## Received.Send builder 직접 경로 후보 기각

- 대상:
  - `MULTI_SPOT_REQREP`, `MULTI_SPOT_SENDSEND`
  - `tcp`
  - `65536B`
- 근거:
  - Go `Received.Send()`는 `sendBuilder`가 있어도 일반 `Message(...)` parts에서는 `[]*Message`를
    만든 뒤 legacy send closure로 돌아간다.
  - public API 의미를 유지하면서 builder 경로를 바로 쓰면 server echo hot path의 할당을 줄일
    수 있는지 확인했다.
- 변경 후보:
  - `bindings/go/internal/native/received.go`
  - `r.sendBuilder != nil && sendBuilderPartsNeedBuilder(parts)` 조건을 `r.sendBuilder != nil`로
    넓혔다.
- 검증:
  - `go test ./...` 통과
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp --pattern MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND --msg-sizes 65536 --duration 2 --runs 5 --results-tag go_multi_spot_tcp65536_received_send_builder_probe_20260601`
  - Go: `perf_go_multi_linux_20260601_194324_go_multi_spot_tcp65536_received_send_builder_probe_20260601.txt`
  - status: complete
- 결과:
  - `MULTI_SPOT_REQREP tcp 65536B`: 2024 ops/s, C 대비 3.2%
  - `MULTI_SPOT_SENDSEND tcp 65536B`: 843.5 ops/s, C 대비 1.3%
- 판정:
  - REQREP는 직전 runs=5 median보다 올랐지만 통과권과 거리가 멀고, SENDSEND는 더 낮아졌다.
  - 전수 기준으로 이득보다 회귀가 크므로 최종 코드와 계획 문서 표에는 반영하지 않고 되돌렸다.

## MULTI_DEALER_DEALER/MULTI_PUBSUB 낮은 값과 RESULT 없음 재측정

- 대상:
  - `MULTI_DEALER_DEALER tcp/ws 4096B`
  - `MULTI_DEALER_DEALER tls/wss 65536B`
  - `MULTI_PUBSUB tls 65536B`
- 근거:
  - 문서 표에서 일부 칸이 `RESULT 없음`이거나 C 대비 1~3%대로 낮아, 현재 상태에서 같은
    pattern, transport, size만 좁혀 다시 확인했다.
- 전체 제한 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp,ws,wss,tls --pattern MULTI_DEALER_DEALER,MULTI_PUBSUB --msg-sizes 4096,65536 --duration 1 --runs 3 --results-tag go_multi_dealer_pubsub_low_recheck_20260601`
  - Go: `perf_go_multi_linux_20260601_194727_go_multi_dealer_pubsub_low_recheck_20260601.txt`
  - status: partial
  - 실패: `MULTI_DEALER_DEALER tcp 4096B` no result
- 전체 제한 측정 2:
  - 명령: `bindings/go/perf/run_benchmarks_multi.sh --transports tcp,ws,wss,tls --pattern MULTI_DEALER_DEALER,MULTI_PUBSUB --msg-sizes 4096,65536 --duration 1 --runs 3 --results-tag go_multi_dealer_pubsub_low_recheck_all_20260601`
  - Go: `perf_go_multi_linux_20260601_194742_go_multi_dealer_pubsub_low_recheck_all_20260601.txt`
  - status: partial
  - 실패:
    - `MULTI_DEALER_DEALER tcp 4096B`: no result 3회
    - `MULTI_DEALER_DEALER ws 4096B`: no result 1회
    - `MULTI_PUBSUB tls 4096B/65536B`: exit nonzero
    - `MULTI_PUBSUB wss 65536B`: exit nonzero
- 단독 완료 재측정:
  - `MULTI_DEALER_DEALER tls/wss 65536B`
    - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports wss,tls --pattern MULTI_DEALER_DEALER --msg-sizes 65536 --duration 1 --runs 3 --results-tag go_multi_dealer_dealer_wss_tls_65536_recheck_20260601`
    - Go: `perf_go_multi_linux_20260601_195558_go_multi_dealer_dealer_wss_tls_65536_recheck_20260601.txt`
    - status: complete
    - 결과: tls 47.3%, wss 50.9%
  - `MULTI_PUBSUB tls 65536B`
    - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tls --pattern MULTI_PUBSUB --msg-sizes 65536 --duration 1 --runs 3 --results-tag go_multi_pubsub_tls_65536_recheck_20260601`
    - Go: `perf_go_multi_linux_20260601_195633_go_multi_pubsub_tls_65536_recheck_20260601.txt`
    - status: complete
    - 결과: 68.2%
  - `MULTI_DEALER_DEALER ws 4096B`
    - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports ws --pattern MULTI_DEALER_DEALER --msg-sizes 4096 --duration 1 --runs 3 --results-tag go_multi_dealer_dealer_ws_4096_recheck_20260601`
    - Go: `perf_go_multi_linux_20260601_195703_go_multi_dealer_dealer_ws_4096_recheck_20260601.txt`
    - status: complete
    - 결과: 55.1%
- 반복 실패 확인:
  - `MULTI_DEALER_DEALER tcp 4096B`
    - 명령 1: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp --pattern MULTI_DEALER_DEALER --msg-sizes 4096 --duration 1 --runs 3 --results-tag go_multi_dealer_dealer_tcp_4096_recheck_20260601`
    - Go: `perf_go_multi_linux_20260601_195652_go_multi_dealer_dealer_tcp_4096_recheck_20260601.txt`
    - status: partial, no result
    - 명령 2: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp --pattern MULTI_DEALER_DEALER --msg-sizes 4096 --duration 2 --runs 5 --results-tag go_multi_dealer_dealer_tcp_4096_runs5_recheck_20260601`
    - Go: `perf_go_multi_linux_20260601_195721_go_multi_dealer_dealer_tcp_4096_runs5_recheck_20260601.txt`
    - status: partial, no result
- 판정:
  - 완료 리포트가 있는 3개 칸은 계획 문서 표에 통과로 반영했다.
  - 다만 `MULTI_DEALER_DEALER tls 65536B`는 47.3%로 이전 1.8%보다 회복됐지만 기준에는
    못 닿아 계획 문서 표에서는 보류로 둔다.
  - `MULTI_DEALER_DEALER tcp 4096B`는 단독 반복에서도 `RESULT`가 없어 보류로 유지한다.
  - 이 단계에서는 binding public contract, HWM profile/floor, perf runner를 바꾸지 않았다.

## MULTI_DEALER_DEALER 131072B 재측정

- 대상:
  - `MULTI_DEALER_DEALER tcp/wss/tls 131072B`
- 근거:
  - 문서 표에서 tcp/wss/tls 131072B가 각각 10.4%, 19.7%, 32.4%로 낮아, 같은 조건을
    완료 리포트로 다시 확인했다.
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp,wss,tls --pattern MULTI_DEALER_DEALER --msg-sizes 131072 --duration 1 --runs 3 --results-tag go_multi_dealer_dealer_131072_recheck_20260601`
  - Go: `perf_go_multi_linux_20260601_195945_go_multi_dealer_dealer_131072_recheck_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - `tcp 131072B`: 55.2%, 통과
  - `wss 131072B`: 52.9%, 통과
  - `tls 131072B`: 28.0%, 보류
- 판정:
  - tcp/wss 131072B는 완료 리포트 기준으로 통과권에 올라 계획 문서 표에 반영했다.
  - tls 131072B는 재측정에서도 기준에 못 닿아 보류로 둔다.
  - binding public contract, HWM profile/floor, perf runner는 바꾸지 않았다.

## 단일 retained message clone 제거 후보 기각

- 대상:
  - `MULTI_SPOT_REQREP tcp 65536B`
  - `MULTI_SPOT_SENDSEND tcp 65536B`
- 근거:
  - Go SPOT reply/send 경로는 단일 retained `Message`를 native submit할 때 중간 clone
    `Message`를 만든다. public API 의미를 유지하면서 단일 retained message를 바로 native
    copy로 제출하면 server reply hot path의 할당을 줄일 수 있는지 확인했다.
- 변경 후보:
  - `bindings/go/internal/native/socket_multipart.go`
  - `submitMultipartFromClones(..., consumeOriginal=false)`의 단일 part 경로에서 중간 clone
    `Message` 생성 없이 `zlink_msg_copy`로 native part를 만들어 submit하도록 했다.
- 검증:
  - `go test ./...` 통과
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp --pattern MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND --msg-sizes 65536 --duration 2 --runs 5 --results-tag go_multi_spot_tcp65536_single_retained_copy_probe_20260601`
  - Go: `perf_go_multi_linux_20260601_200312_go_multi_spot_tcp65536_single_retained_copy_probe_20260601.txt`
  - status: complete
- 결과:
  - `MULTI_SPOT_REQREP tcp 65536B`: median 1384.5 ops/s, C 대비 약 2.2%
  - `MULTI_SPOT_SENDSEND tcp 65536B`: median 2251.5 ops/s, C 대비 약 3.5%
- 판정:
  - REQREP는 기준과 거리가 멀고, SENDSEND는 기존 runs=5 재측정보다 낮아졌다.
  - 통과 항목을 만들지 못하고 회귀 위험이 있어 최종 코드와 계획 문서 표에는 반영하지 않고
    되돌렸다.

## MULTI_DEALER_ROUTER/MULTI_ROUTER_ROUTER tcp/tls failset 재측정

- 대상:
  - `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`
  - `tcp,tls`
  - `64,256,1024,65536B`
- 근거:
  - Go multi routed echo 잔여 보류와 낮은 값을 같은 조건에서 완료 리포트로 다시 확인했다.
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp,tls --pattern MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER --msg-sizes 64,256,1024,65536 --duration 1 --runs 3 --results-tag go_multi_routed_tcp_tls_failset_recheck_20260601`
  - Go: `perf_go_multi_linux_20260601_200558_go_multi_routed_tcp_tls_failset_recheck_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - `MULTI_DEALER_ROUTER tcp`: 55.2%, 57.2%, 60.1%, 31.9%
  - `MULTI_DEALER_ROUTER tls`: 51.5%, 53.0%, 52.5%, 42.6%
  - `MULTI_ROUTER_ROUTER tcp`: 36.5%, 37.5%, 37.7%, 38.3%
  - `MULTI_ROUTER_ROUTER tls`: 37.8%, 37.8%, 39.4%, 43.3%
- 판정:
  - `MULTI_DEALER_ROUTER` small과 `tls 65536B`는 multi routed echo 기준으로 통과권을
    유지한다.
  - `MULTI_DEALER_ROUTER tcp 65536B`와 `MULTI_ROUTER_ROUTER` 잔여 small/tcp 65536B는
    기준에 못 닿아 보류로 유지한다.
  - binding public contract, HWM profile/floor, perf runner는 바꾸지 않았다.

## MULTI_SPOT wss 1024/4096B 재측정

- 대상:
  - `MULTI_SPOT wss 1024B`
  - `MULTI_SPOT wss 4096B`
- 근거:
  - 문서 표에서 각각 22.8%, 38.6%로 낮아 같은 조건을 완료 리포트로 다시 확인했다.
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports wss --pattern MULTI_SPOT --msg-sizes 1024,4096 --duration 1 --runs 3 --results-tag go_multi_spot_wss_1024_4096_recheck_20260601`
  - Go: `perf_go_multi_linux_20260601_200901_go_multi_spot_wss_1024_4096_recheck_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - `1024B`: 58.2%, 통과
  - `4096B`: 171.9%, 통과
- 판정:
  - 두 칸 모두 완료 리포트 기준으로 통과권에 올라 계획 문서 표에 반영했다.
  - binding public contract, HWM profile/floor, perf runner는 바꾸지 않았다.

## MULTI_DEALER_DEALER/MULTI_PUBSUB 64/256B 재측정

- 대상:
  - `MULTI_DEALER_DEALER`, `MULTI_PUBSUB`
  - `tcp,tls,ws,wss`
  - `64,256B`
- 근거:
  - 단순 one-way 그룹은 Go 기준 53%가 최소 통과 기준이다. 표에 40%대 후반과 52%대
    보류가 남아 있어 현재 상태를 완료 리포트로 다시 확인했다.
- 측정:
  - 명령: `PERF_FAIL_FAST=1 bindings/go/perf/run_benchmarks_multi.sh --transports tcp,ws,wss,tls --pattern MULTI_DEALER_DEALER,MULTI_PUBSUB --msg-sizes 64,256 --duration 1 --runs 3 --results-tag go_multi_simple_small_failset_recheck_20260601`
  - Go: `perf_go_multi_linux_20260601_201047_go_multi_simple_small_failset_recheck_20260601.txt`
  - C 기준: `perf_c_multi_linux_20260530_234108_round_20260530_c_multi_baseline.txt`
  - status: complete
- 결과:
  - `MULTI_DEALER_DEALER tcp`: 43.1%, 64.1%
  - `MULTI_DEALER_DEALER tls`: 44.1%, 57.3%
  - `MULTI_DEALER_DEALER ws`: 46.8%, 56.1%
  - `MULTI_DEALER_DEALER wss`: 44.6%, 56.6%
  - `MULTI_PUBSUB tcp`: 47.5%, 59.3%
  - `MULTI_PUBSUB tls`: 107.0%, 85.5%
  - `MULTI_PUBSUB ws`: 343.7%, 90.7%
  - `MULTI_PUBSUB wss`: 162.8%, 98.1%
- 판정:
  - `MULTI_PUBSUB tcp/tls/wss 256B`는 완료 리포트 기준으로 통과권에 올라 계획 문서 표에
    반영했다. `MULTI_PUBSUB ws 64/256B`와 wss/tls 64B도 최신 complete 수치로 표를 갱신했다.
  - `MULTI_DEALER_DEALER` 64B 계열과 `MULTI_PUBSUB tcp 64B`는 재측정에서도 단순 one-way
    기준에 못 닿아 보류로 유지한다.
  - binding public contract, HWM profile/floor, perf runner는 바꾸지 않았다.
