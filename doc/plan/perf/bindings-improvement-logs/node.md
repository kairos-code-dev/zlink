# Node binding 성능 개선 라운드 로그

관련 계획 문서: [bindings-library-performance-improvement-plan.ko.md](../bindings-library-performance-improvement-plan.ko.md)

### 2026-05-13 Node round 2

- 동일 조합 C 결과:
  - `bindings/c/perf/baseline/perf_c_multi_linux_20260513_101034.txt`
- 대상 언어 최신 결과:
  - `bindings/node/perf/results/multi/report/perf_node_multi_linux_20260513_230615_node_goal_pass_all_sizes_20260513.txt`
- 목표 통과 조합:
  - `MULTI_ROUTER_ROUTER,tcp,64/256/1024/65536/131072/262144B`
  - `MULTI_DEALER_ROUTER,tcp,64/256/1024/65536/131072/262144B`
- 선택한 병목 가설:
  - round 1의 `Received` 재사용만으로는 JS `Message` wrapper 생성과 payload `Buffer` 복사가 hot path에 남았다.
  - client는 perf metric header 29바이트만 읽으면 되는데, 이전 경로는 echo payload 전체를 JS `Message`로 materialize했다.
  - server echo는 payload를 해석하지 않는데도 JS `Received`와 routing id wrapper를 매번 만들었다.
  - 64KB 이상 크기에서는 Node 기본 I/O thread `4`가 충분하지 않아 core와 같은 데이터 이동을 따라가지 못했다.
- 변경한 라이브러리 파일:
  - `bindings/node/native/src/addon_core.cc`: caller-provided `Buffer`에서 borrowed native message를 만들고, nowait send가 해당 buffer를 바로 전송하는 helper를 추가했다. caller-provided buffer로 recv payload를 직접 채우는 helper와 ROUTER echo를 native에서 바로 되돌려 보내는 `routerTryRecvEchoNoWait`도 추가했다.
  - `bindings/node/native/src/addon_api.h`, `bindings/node/native/src/addon.cc`: 새 native helper를 addon surface에 연결했다.
  - `bindings/node/src/canonical.ts`: perf에서 필요한 buffer send/recv helper를 canonical socket class에 연결했다.
- 변경한 perf 파일:
  - `bindings/node/perf/multi/perf_multi_runtime.ts`: non-stream multi 기본 I/O thread를 `8`로 맞췄다.
  - `bindings/node/perf/multi/perf_multi_*_client.ts`: send는 borrowed buffer nowait 경로를 쓰고, reply는 metric header 크기 buffer로만 받는다.
  - `bindings/node/perf/multi/perf_multi_*_server.ts`: ROUTER echo hot path를 native `recvEchoNoWait`로 처리해 JS object materialization을 제거했다.
- 실행한 검증 명령:
  - perf 전 프로세스 확인 후 `bindings/node/perf/run_benchmarks_multi.sh --reuse-build --pattern ROUTER_ROUTER,DEALER_ROUTER --transports tcp --msg-sizes 64,256,1024,65536,131072,262144 --duration 3 --runs 3 --results-tag node_goal_pass_all_sizes_20260513`
  - 테스트 전 프로세스 확인 후 `cd bindings/node && npm test`
- 결과:
  - 최신 median:
    - `MULTI_ROUTER_ROUTER`: `269.62/271.11/269.32/139.89/66.52/31.08 Kops/s`
    - `MULTI_DEALER_ROUTER`: `341.31/341.35/340.66/164.73/64.59/30.61 Kops/s`
  - C 기준 대비 ratio:
    - `MULTI_ROUTER_ROUTER`: `0.619/0.616/0.603/0.758/0.779/0.978`
    - `MULTI_DEALER_ROUTER`: `0.727/0.732/0.748/0.865/0.817/1.012`
  - Node 목표 `64B >= 0.42`, `256B >= 0.52`, `1024B >= 0.57`, `64KB >= 0.62`, `128KB >= 0.64`, `256KB >= 0.65`를 RR/DR 두 pattern 모두 통과했다.
- 다음 판단:
  - Node multi echo의 목표 조합은 통과 상태다.
  - 새 helper는 perf hot path에서 caller-owned buffer를 쓰기 위한 얕은 native message 경로다. `zlink_msg_copy`처럼 payload를 깊게 복사하지 않고 lifetime은 submit 완료까지 caller buffer가 살아 있다는 전제로 perf loop 안에서만 사용한다.

### 2026-05-13 Node round 1

- 동일 조합 C 결과:
  - `bindings/c/perf/baseline/perf_c_multi_linux_20260513_101034.txt`
- 대상 언어 초기 결과:
  - `bindings/node/perf/results/multi/report/perf_node_multi_linux_20260513_211304_node_echo_current_20260513.txt`
- 대상 언어 최신 결과:
  - `bindings/node/perf/results/multi/report/perf_node_multi_linux_20260513_215117_node_echo_contract_safe_20260513.txt`
- 목표 미달 조합:
  - `MULTI_ROUTER_ROUTER,tcp,64/256/1024B`: C 기준 대비 약 `0.37/0.34/0.30`으로 Node 목표 `0.42/0.52/0.57`에 미달한다.
  - `MULTI_DEALER_ROUTER,tcp,64/256/1024B`: C 기준 대비 약 `0.46/0.41/0.37`이다. 64B는 목표를 넘었고, 256B와 1024B는 아직 미달한다.
- 선택한 병목 가설:
  - multi echo에서 Node 기본 I/O thread 수가 다른 언어와 달리 non-stream 패턴에서 `2`로 떨어져 있었다.
  - caller-provided `Received`를 쓰더라도 `recv(result, DontWait)`가 내부에서 임시 `Received`를 만든 뒤 다시 adopt하여 per-recv 할당을 남겼다.
  - 클라이언트 perf loop가 recv마다 새 `Received`를 만들고 있었다.
  - ROUTER echo 서버가 받은 routing id를 다시 `RoutingId` 객체로 감싸는 비용을 매 echo마다 냈다.
- 변경한 라이브러리 파일:
  - `bindings/node/src/canonical.ts`: 단일 part 배열 payload를 scalar send 경로로 정규화하고, `recv(result, flags)`가 target `Received`를 직접 채우도록 `_replace` 기반 materialization을 추가했다. ROUTER received-send 일반 경로는 raw routing id buffer를 그대로 재사용한다.
  - `bindings/node/src/message.ts`: `Received._replace(...)`를 추가해 caller-provided storage를 직접 갱신한다. native snapshot으로 만든 `Message`는 불필요한 `Object.freeze`를 생략한다.
- 변경한 perf 파일:
  - `bindings/node/perf/multi/perf_multi_runtime.ts`: non-stream multi 기본 I/O thread도 `4`로 맞추고, 기존 `recvNoWait` 위에 `recvNoWaitInto(socket, received)` helper를 추가했다.
  - `bindings/node/perf/multi/perf_multi_*_client.ts`: socket별 reply `Received` buffer를 재사용한다.
  - `bindings/node/perf/multi/perf_multi_*_server.ts`: server receive buffer를 재사용하고, pending queue에 넘길 때만 새 `Received`를 준비한다.
- 추가/수정한 회귀 테스트:
  - `bindings/node/tests/optimization_guard.test.ts`를 기존 테스트 묶음과 함께 실행했다.
- 실행한 검증 명령:
  - `cd bindings/node && npm run rebuild-native && npm run build && npm run typecheck`
  - `cd bindings/node && node dist-tools/tests/dealer_router.test.js && node dist-tools/tests/socket_surface.test.js && node dist-tools/tests/optimization_guard.test.js`
  - perf 전 프로세스 확인 후 `bindings/node/perf/run_benchmarks_multi.sh --reuse-build --pattern ROUTER_ROUTER,DEALER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 3 --runs 3 --results-tag node_echo_contract_safe_20260513`
- 결과:
  - 초기 Node current median:
    - `MULTI_ROUTER_ROUTER`: `117.27/116.07/106.81 Kops/s`
    - `MULTI_DEALER_ROUTER`: `141.04/137.63/132.05 Kops/s`
  - 최신 contract-safe median:
    - `MULTI_ROUTER_ROUTER`: `160.10/148.44/135.40 Kops/s`
    - `MULTI_DEALER_ROUTER`: `214.68/190.86/167.86 Kops/s`
  - scalar payload builder 실험은 `node_echo_payload_scalar_20260513`에서 최신값보다 낮아 유지하지 않았다.
  - native message snapshot에서 `zlink_msg_gets`와 `zlink_msg_refcnt`를 생략하는 실험은 공개 `Message.getProperty()`와 `refCount()` 의미를 약화하므로 유지하지 않았다.
- 다음 판단:
  - 이번 라운드는 Node multi echo hot path의 allocation과 I/O thread 정책을 정리한 단계다.
  - 남은 병목은 native snapshot 생성, JS `Message` wrapper 생성, routed receive 결과 배열 생성 비용이다. 공개 API 의미를 줄이지 않는 범위에서 native에서 single-part receive materialization 비용을 더 줄일 방법을 찾아야 한다.
