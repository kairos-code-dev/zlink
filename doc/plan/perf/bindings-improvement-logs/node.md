# Node binding 성능 개선 라운드 로그

관련 계획 문서: [bindings-library-performance-improvement-plan.ko.md](../bindings-library-performance-improvement-plan.ko.md)

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
