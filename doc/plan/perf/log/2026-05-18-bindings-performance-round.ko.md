# 2026-05-18 bindings 성능 작업 로그

이 문서는 `bindings-library-performance-improvement-plan.ko.md`에서 분리한 측정 기록이다.
계획 문서 본문에는 실행 규칙과 현재 상태 표만 유지한다.

## C++

- `MULTI_ROUTER_ROUTER/tcp/65536`
  - C 기준: `184497.8`
  - C++ best: `97370.8`
  - C 대비: `52.8%`
  - 목표: `65%`
  - 상태: 보류
  - 결과: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260518_114136_codex_cpp_tcp_rr_64_after_raw_revert.txt`
- `MULTI_ROUTER_ROUTER/tcp/131072`
  - C 기준: `85400.0`
  - C++ best: `57028.4`
  - C 대비: `66.8%`
  - 목표: `65%`
  - 상태: 통과
  - 결과: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260518_112701_codex_cpp_tcp_rr_large_local_send_msg.txt`
- 확인한 내부 후보
  - 단일 파트 routed send fast path
  - raw routed private state
  - poller socket cache
  - latency reserve
  - sparse poll event construction
- 보류 이유
  - public API 변경 없이 시도한 후보가 목표 미달이거나 128KB 회귀를 만들었다.
  - 현재 public builder 경로는 이미 단일 파트 raw routed send로 내려간다.
  - 승인 후보: 단일 파트 routed send API, 반복 전송용 pre-bound routed sender API,
    source routing id materialization 생략 API.

### C++ ws full multi

- 실행 명령
  - `bindings/cpp/perf/run_binding_multi.sh --transports ws --results-tag codex_cpp_ws_full_status`
- 결과 파일
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260518_124314_codex_cpp_ws_full_status.txt`
- C 재측정 기준
  - baseline 결과가 현재 core와 맞지 않는 것으로 보여 같은 transport로 C full 결과를
    다시 측정했다.
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260518_125011_codex_c_ws_full_current_compare.txt`
- 상태
  - partial
- 통과
  - `MULTI_DEALER_DEALER/ws/256`: C 대비 `96.4%`
  - `MULTI_DEALER_DEALER/ws/1024`: C 대비 `93.5%`
  - `MULTI_DEALER_DEALER/ws/65536`: C 대비 `94.8%`
  - `MULTI_DEALER_DEALER/ws/131072`: C 대비 `101.6%`
  - `MULTI_DEALER_ROUTER/ws/64`: C 대비 `88.1%`
  - `MULTI_DEALER_ROUTER/ws/256`: C 대비 `87.7%`
  - `MULTI_DEALER_ROUTER/ws/1024`: C 대비 `92.6%`
  - `MULTI_DEALER_ROUTER/ws/131072`: C 대비 `70.9%`
  - `MULTI_DEALER_ROUTER/ws/262144`: C 대비 `66.0%`
  - `MULTI_ROUTER_ROUTER/ws/64`: C 대비 `95.6%`
  - `MULTI_ROUTER_ROUTER/ws/256`: C 대비 `94.7%`
  - `MULTI_ROUTER_ROUTER/ws/1024`: C 대비 `93.1%`
  - `MULTI_ROUTER_ROUTER/ws/262144`: C 대비 `100.6%`
  - `MULTI_PUBSUB/ws/64`: C 대비 `95.8%`
  - `MULTI_PUBSUB/ws/256`: C 대비 `98.2%`
  - `MULTI_PUBSUB/ws/1024`: C 대비 `92.6%`,
    `perf_cpp_multi_linux_20260518_124911_codex_cpp_ws_pubsub_1024_debug.txt`
  - `MULTI_PUBSUB/ws/65536`: C 대비 `113.9%`,
    `perf_cpp_multi_linux_20260518_124924_codex_cpp_ws_pubsub_large_recheck.txt`
  - `MULTI_PUBSUB/ws/131072`: C 대비 `87.6%`,
    `perf_cpp_multi_linux_20260518_124924_codex_cpp_ws_pubsub_large_recheck.txt`
  - `MULTI_PUBSUB/ws/262144`: C 대비 `88.7%`,
    `perf_cpp_multi_linux_20260518_124924_codex_cpp_ws_pubsub_large_recheck.txt`
  - `MULTI_STREAM/ws/64,256,1024,65536`: C 대비 `82.0%~108.1%`
- 미달
  - `MULTI_DEALER_DEALER/ws/64`: 제한 C 대비 `76.5%`
    - C: `perf_c_multi_linux_20260518_130452_codex_c_ws_dd_recheck_compare.txt`
    - C++: `perf_cpp_multi_linux_20260518_130911_codex_cpp_ws_dd_after_reuse_wait_overload.txt`
  - `MULTI_DEALER_DEALER/ws/262144`: C current 대비 `78.8%`
    - 제한 C 측정은 server non-zero exit로 partial이라 기준으로 쓰지 않았다.
    - C++ best: `perf_cpp_multi_linux_20260518_130205_codex_cpp_ws_dd_recheck.txt`
  - `MULTI_DEALER_ROUTER/ws/65536`: 제한 C 대비 `59.0%`
    - C: `perf_c_multi_linux_20260518_130512_codex_c_ws_dr_65536_recheck_compare.txt`
    - C++: `perf_cpp_multi_linux_20260518_130228_codex_cpp_ws_dr_65536_recheck.txt`
  - `MULTI_ROUTER_ROUTER/ws/65536`: 제한 C 대비 `60.3%`
    - C: `perf_c_multi_linux_20260518_130524_codex_c_ws_rr_large_recheck_compare.txt`
    - C++: `perf_cpp_multi_linux_20260518_130237_codex_cpp_ws_rr_large_recheck.txt`
  - `MULTI_ROUTER_ROUTER/ws/131072`: 제한 C 대비 `60.8%`
    - C: `perf_c_multi_linux_20260518_130524_codex_c_ws_rr_large_recheck_compare.txt`
    - C++: `perf_cpp_multi_linux_20260518_130237_codex_cpp_ws_rr_large_recheck.txt`
- 확인한 후보
  - `poller_t` socket-only event fill 방식 변경: 개선 없음, 되돌림.
  - `MULTI_DEALER_DEALER` client/server가 기존 public API의 재사용 wait overload를 쓰도록 정렬.
  - `MULTI_DEALER_DEALER` server 수신 메시지를 C 기준처럼 처리 후 명시 close.
  - routed echo client에서 framed transport payload를 C 기준처럼 공유 buffer에서 복사하도록
    정렬했다.
    - `MULTI_DEALER_ROUTER/ws/65536`은 한 차례 `91,164.6`까지 올랐지만,
      `62,237.0`, `50,134.6`으로 재측정되어 통과 근거로 쓰지 않는다.
    - 직접 `message_t` payload에 stamp하는 방식은 `MULTI_DEALER_ROUTER/ws/65536`을
      `61,695.6`으로 낮춰 폐기했다.
- 실행 실패
  - `MULTI_SPOT/ws/64`: `Unknown error 204 (errno=14)`, `MsgUnit(B)=4096`
    - `perf_cpp_multi_linux_20260518_131030_codex_cpp_ws_spot64_debug_recheck.txt`
  - `MULTI_SPOT_REQREP/ws/*`: `CLIENT_READY,64`, `MsgUnit(B)=4096`
  - `MULTI_SPOT_SENDSEND/ws/*`: `CLIENT_READY,64`, `MsgUnit(B)=4096`
- 보류 이유
  - `MULTI_DEALER_DEALER/ws/64,262144`
    - 기존 public API의 재사용 wait overload와 명시 close로 C 기준 처리 흐름에 맞췄지만
      목표를 넘지 못했다.
    - 직접 `message_t` payload에 stamp하는 방식은 64B만 소폭 개선하고 256KB latency를
      크게 악화시켜 폐기했다.
    - 승인 후보: 반복 전송용 owned message builder 또는 benchmark target 재검토.
  - `MULTI_DEALER_ROUTER/ws/65536`, `MULTI_ROUTER_ROUTER/ws/65536,131072`
    - framed transport 공유 payload buffer 정렬과 직접 stamp 후보를 확인했지만 안정적인
      통과 수치를 만들지 못했다.
    - 현재 public routed recv/send API는 C처럼 native routing id pointer를 그대로
      재사용하는 hot path를 노출하지 않는다.
    - 승인 후보: routing id materialization 없이 받은 route context로 단일 part를
      다시 보내는 public routed echo/send context API.
  - C++ public API에는 SPOT node 또는 SPOT handle의 pub/sub auto-HWM message unit을
    message size로 설정하는 typed facade가 없다.
  - raw option bag이나 C API 직접 호출은 public API hot path 원칙에 맞지 않는다.
  - 승인 후보: SPOT node pub/sub default auto-HWM message unit facade와 SPOT handle
    auto-HWM message unit facade의 공개 계약 추가.

### C++ wss full multi

- 실행 명령
  - `bindings/cpp/perf/run_binding_multi.sh --reuse-build --transports wss --results-tag codex_cpp_wss_full_status`
- 결과 파일
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260518_132049_codex_cpp_wss_full_status.txt`
- C 기준
  - `bindings/c/perf/baseline/perf_c_multi_linux_20260513_101034.txt`
  - baseline이 오래되어 미달 항목은 같은 transport와 pattern으로 제한 C 재측정이 필요하다.
- 통과
  - `MULTI_DEALER_DEALER/wss/1024,65536`: C 대비 `83.7%~88.2%`
  - `MULTI_DEALER_ROUTER/wss/64,256,1024,65536,131072,262144`: C 대비 `84.4%~95.5%`
  - `MULTI_ROUTER_ROUTER/wss/64,256,1024,65536,131072,262144`: C 대비 `83.0%~93.5%`
  - `MULTI_STREAM/wss/64,256,1024,65536`: C 대비 `90.0%~100.1%`
- 미달
  - `MULTI_DEALER_DEALER/wss/64`: 제한 C 대비 `73.7%`
  - `MULTI_PUBSUB/wss/256`: 제한 C 대비 `78.3%`
  - `MULTI_PUBSUB/wss/65536`: 제한 C 대비 `67.2%`
- 실행 실패
  - `MULTI_DEALER_DEALER/wss/262144`: timeout
  - `MULTI_PUBSUB/wss/131072,262144`: timeout
  - `MULTI_SPOT/wss/*`: `Unknown error 204 (errno=14)`, `MsgUnit(B)=4096`
  - `MULTI_SPOT_REQREP/wss/*`: `CLIENT_READY,64`, `MsgUnit(B)=4096`
  - `MULTI_SPOT_SENDSEND/wss/*`: `CLIENT_READY,64`, `MsgUnit(B)=4096`
- 제한 C 재측정
  - `DEALER_DEALER`: `perf_c_multi_linux_20260518_133226_codex_c_wss_dd_recheck_compare.txt`
    - `256`: C 대비 `99.7%`, 통과
    - `131072`: C 대비 `92.6%`, 통과
  - `PUBSUB`: `perf_c_multi_linux_20260518_133255_codex_c_wss_pubsub_recheck_compare.txt`
    - `64`: C 대비 `89.4%`, 통과
    - `1024`: C 대비 `104.3%`, 통과
- 확인한 후보
  - `PUBSUB` server를 typed `pub_socket_t::publish()` 경로로 바꿨지만
    `wss/256,65536,131072,262144`가 모두 timeout으로 악화되어 되돌렸다.
- 보류 이유
  - `MULTI_PUBSUB` client hot path는 현재 public `subscribe(topic_message_t&)`가
    message마다 topic string과 parts vector를 물질화한다. C 기준처럼 topic buffer와
    단일 part를 재사용하는 public subscribe facade가 없다.
  - 승인 후보: single-part pub/sub receive를 topic buffer와 message out parameter에
    직접 받는 public API.

### C++ tls full multi

- 실행 명령
  - `bindings/cpp/perf/run_binding_multi.sh --reuse-build --transports tls --results-tag codex_cpp_tls_full_status`
- 결과 파일
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260518_133640_codex_cpp_tls_full_status.txt`
- C 기준
  - 첫 비교: `bindings/c/perf/baseline/perf_c_multi_linux_20260513_101034.txt`
  - 미달 또는 timeout 항목은 같은 transport와 pattern으로 제한 재측정했다.
- 통과
  - `MULTI_DEALER_DEALER/tls/256,1024,65536`: C 대비 `83.4%~88.4%`
  - `MULTI_DEALER_ROUTER/tls/64,256,1024,65536,131072`: C 대비 `83.6%~89.1%`
  - `MULTI_DEALER_ROUTER/tls/262144`: 제한 C 대비 `88.2%`
  - `MULTI_ROUTER_ROUTER/tls/64,256,1024,65536,131072`: C 대비 `89.3%~94.8%`
  - `MULTI_ROUTER_ROUTER/tls/262144`: 제한 C 대비 `112.5%`
  - `MULTI_PUBSUB/tls/64,256`: 제한 C 대비 `80.9%~85.5%`
  - `MULTI_PUBSUB/tls/1024`: C 대비 `80.2%`
  - `MULTI_PUBSUB/tls/131072`: 제한 C 대비 `97.8%`
  - `MULTI_STREAM/tls/64,256,1024,65536`: C 대비 `94.8%~103.2%`
- 제한 C 재측정
  - `DEALER_DEALER`: `perf_c_multi_linux_20260518_140428_codex_c_tls_dd_recheck_compare.txt`
  - `PUBSUB`: `perf_c_multi_linux_20260518_140850_codex_c_tls_pubsub_recheck_compare.txt`
  - `DEALER_ROUTER/262144`: `perf_c_multi_linux_20260518_141339_codex_c_tls_dr_262_recheck_compare.txt`
  - `ROUTER_ROUTER/262144`: `perf_c_multi_linux_20260518_141403_codex_c_tls_rr_262_recheck_compare.txt`
- 제한 C++ 재측정
  - `DEALER_DEALER`: `perf_cpp_multi_linux_20260518_140451_codex_cpp_tls_dd_recheck.txt`
  - `PUBSUB`: `perf_cpp_multi_linux_20260518_140925_codex_cpp_tls_pubsub_recheck.txt`
  - `DEALER_ROUTER/262144`: `perf_cpp_multi_linux_20260518_141353_codex_cpp_tls_dr_262_recheck.txt`
  - `ROUTER_ROUTER/262144`: `perf_cpp_multi_linux_20260518_141416_codex_cpp_tls_rr_262_recheck.txt`
- 보류
  - `MULTI_DEALER_DEALER/tls/64`: 제한 C 대비 `75.5%`
  - `MULTI_DEALER_DEALER/tls/131072`: 제한 C 대비 `51.6%`
  - `MULTI_DEALER_DEALER/tls/262144`: C++ timeout, C 제한 측정은 성공
  - `MULTI_PUBSUB/tls/65536`: 제한 C 대비 `70.2%`
  - `MULTI_PUBSUB/tls/262144`: C++ timeout, C 제한 측정은 성공
  - `MULTI_SPOT/tls/*`: `Unknown error 204 (errno=14)`, `MsgUnit(B)=4096`
  - `MULTI_SPOT_REQREP/tls/*`: `CLIENT_READY,64`, `MsgUnit(B)=4096`
  - `MULTI_SPOT_SENDSEND/tls/*`: `CLIENT_READY,64`, `MsgUnit(B)=4096`
- 보류 이유
  - `DEALER_DEALER`은 기존 public API의 재사용 wait overload와 수신 메시지 명시 close
    정렬 후에도 목표를 넘지 못했다. 직접 message payload stamp 방식은 앞선 ws 검토에서
    큰 메시지 latency를 악화시켜 폐기했다.
  - `PUBSUB`은 앞선 wss 검토에서 typed publish 경로가 timeout으로 악화되어 되돌렸다.
    남은 병목은 public `subscribe(topic_message_t&)`의 topic string과 parts vector
    물질화 비용으로 본다.
  - SPOT 계열은 public C++ SPOT node 또는 handle에 pub/sub auto-HWM message unit을
    message size로 맞추는 typed facade가 없어 raw option이나 C API로 우회하지 않았다.
  - 승인 후보는 ws/wss와 같다. 반복 전송용 owned message builder, single-part
    pub/sub receive facade, SPOT auto-HWM message unit typed facade가 필요하다.

## .NET

- `MULTI_SPOT/tcp/64`
  - .NET: `3119842.2`
  - C 기준: `5971358.8`
  - C 대비: `52.2%`
  - 목표: `60%`
  - 상태: 미달
  - 결과: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260518_111857_codex_tcp64_contract_hotloop_check.txt`
- `MULTI_SPOT/ws/64`
  - .NET: `3115855`
  - C 기준: `6735632`
  - C 대비: `46.3%`
  - 목표: `60%`
  - 상태: 미달
  - 결과: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260518_103205_codex_ws64_client_hotloop_probe.txt`
- 확인한 내부 후보
  - publish builder fast path
  - payload 생성 방식 변경
  - client hot loop 개선
- 남은 후보
  - tcp-first 원칙상 `MULTI_SPOT/tcp/64`부터 다시 직접 개선한다.
- 2026-05-18 재측정
  - C 제한 기준: `perf_c_multi_linux_20260518_141639_codex_c_tcp_spot64_dotnet_compare.txt`
    - C: `5,832,409.8 msg/s`
  - .NET current: `perf_dotnet_multi_linux_20260518_141619_codex_dotnet_tcp_spot64_current_recheck.txt`
    - .NET: `3,001,629.4 msg/s`, C 대비 `51.5%`
  - `PERF_DOTNET_SPOT_RECV_WORKERS=8` probe:
    `perf_dotnet_multi_linux_20260518_141837_codex_dotnet_tcp_spot64_recv_workers8_probe.txt`
    - .NET: `2,718,605.2 msg/s`, 개선 없음
    - 이 값은 C perf와 worker 조건이 달라지는 실험이므로 통과/보류 판정 근거로 쓰지 않는다.
      이후 라운드에서는 테스트 의미가 달라지는 worker 수 변경 실험을 하지 않는다.
- 중단한 public API 프로토타입
  - 직접 publish overload 후보: `3,147,725.4 msg/s`
  - 단일 part subscribe buffer 후보: `3,269,798.4 msg/s`
  - span publish 후보: `3,069,849.4 msg/s`
  - 목표 60%에 필요한 약 `3,499,446 msg/s`에 미달했고 개선 폭도 크지 않아 public API
    변경 후보로 계속 진행하지 않는다. 프로토타입 코드는 측정 직후 원복했다.
- 기존 API 내부 프로토타입
  - routed echo tcp client payload를 C와 같은 의미로 맞추는 perf 정렬:
    `perf_dotnet_multi_linux_20260518_145439_codex_dotnet_tcp_routed_full_borrow_payload.txt`
    - `Effective Options`: `routed_echo_borrow_payload=tcp`
    - `MULTI_DEALER_ROUTER`: `62.5%`, `64.8%`, `65.8%`, `80.6%`, `103.6%`, `130.6%`
    - `MULTI_ROUTER_ROUTER`: `54.9%`, `56.0%`, `54.5%`, `73.9%`, `93.1%`, `145.7%`
    - `MULTI_ROUTER_ROUTER/tcp/1024`는 절대 기준은 넘지만 C의
      `ROUTER_ROUTER / DEALER_ROUTER` 상대 비율보다 15%p를 조금 넘게 낮아
      제한 재측정 대상으로 남겼다.
  - tcp borderline 제한 재측정:
    `perf_dotnet_multi_linux_20260518_145742_codex_dotnet_tcp_borderline_recheck.txt`
    - `MULTI_DEALER_DEALER/tcp/64`: `1,601,415.8 msg/s`, C 대비 `53.5%`, 미달
    - `MULTI_DEALER_DEALER/tcp/256`: `1,205,802.6 msg/s`, C 대비 `62.5%`, 미달
    - `MULTI_PUBSUB/tcp/64`: `1,343,102.4 msg/s`, C 대비 `61.3%`, 미달
    - `MULTI_ROUTER_ROUTER/tcp/1024`: `202,117.4 msg/s`, C 대비 `56.9%`, 통과
    - `MULTI_SPOT_SENDSEND/tcp/64`: `155,329.8 msg/s`, C 대비 `69.5%`, 통과
  - raw send/publish builder inline 후보:
    `perf_dotnet_multi_linux_20260518_150141_codex_dotnet_tcp_oneway_inline_candidate.txt`
    - 단위 테스트: `bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj`, 145개 통과
    - `MULTI_PUBSUB/tcp/64`: `1,468,394.2 msg/s`, C 대비 `67.0%`, 통과
    - `MULTI_DEALER_DEALER/tcp/64`: `1,608,215.0 msg/s`, C 대비 `53.7%`, 보류
    - `MULTI_DEALER_DEALER/tcp/256`: `1,167,607.0 msg/s`, C 대비 `60.5%`, 보류
    - 남은 `DEALER_DEALER` small miss는 public API builder와 `Message` 생성 비용이
      결합된 경로다. public API 변경 없이 적용 가능한 얇은 builder inline 후보는
      목표에 닿지 못했고, 의미가 달라지는 borrowed payload 전환은 C one-way 조건과
      맞지 않아 시도하지 않았다.
  - SPOT echo large timeout 제한 재측정:
    `perf_dotnet_multi_linux_20260518_150313_codex_dotnet_tcp_spot_timeout_recheck.txt`
    - `MULTI_SPOT_REQREP/tcp/65536`: timeout
    - `MULTI_SPOT_REQREP/tcp/262144`: timeout
    - `MULTI_SPOT_SENDSEND/tcp/65536`: timeout
    - `MULTI_SPOT_SENDSEND/tcp/262144`: timeout
    - `MsgUnit(B)`는 각 message size와 같았다. timeout, HWM, client 수를 바꾸면
      C 비교 조건과 달라지므로 조정하지 않았다.
- `ws` full
  - .NET: `perf_dotnet_multi_linux_20260518_150810_codex_dotnet_ws_full_status.txt`
    - 상태: partial, 42 success / 4 fail
    - fail: `MULTI_SPOT_REQREP/ws/65536`, `MULTI_SPOT_REQREP/ws/262144`,
      `MULTI_SPOT_SENDSEND/ws/65536`, `MULTI_SPOT_SENDSEND/ws/262144`
    - `MsgUnit(B)`는 출력된 모든 size에서 message size와 일치했다.
  - C SPOT 제한 기준: `perf_c_multi_linux_20260518_151858_codex_c_ws_spot_dotnet_compare.txt`
    - baseline SPOT의 `MsgUnit(B)=4096` 이력이 있어 SPOT 계열만 같은 조건으로 재측정했다.
  - 판정 요약
    - `DEALER_DEALER`: 64/256/131072 보류, 나머지 통과.
    - `DEALER_ROUTER`: 전체 통과.
    - `ROUTER_ROUTER`: 64/256/1024/65536 보류, 131072/262144 통과.
    - `PUBSUB`: 64/256/1024 보류, 65536/131072/262144 통과.
    - `SPOT`: 전체 보류. C 제한 기준 대비 `33.7%~51.6%`.
    - `SPOT_REQREP`: 64/256/1024/131072 통과, 65536/262144 보류.
    - `SPOT_SENDSEND`: 64/256/1024/131072 통과, 65536/262144 보류.
    - `STREAM`: 전체 통과.
  - `Message.WrapBytes`가 기존 thread-local `Message` pool을 쓰도록 하는 후보:
    `perf_dotnet_multi_linux_20260518_145139_codex_dotnet_tcp_spot64_message_pool.txt`
    - .NET: `2,919,071.4 msg/s`, C 대비 `50.1%`
    - 같은 public API와 같은 조건을 유지한 내부 변경이지만 목표에 미달했고, 직전 제한
      측정보다 낫지 않아 원복했다.
  - SPOT publish에서 borrowed send를 우회하고 native message copy를 강제하는 후보:
    `perf_dotnet_multi_linux_20260518_142632_codex_dotnet_tcp_spot64_copy_send_proto.txt`
    - .NET: `3,094,403.6 msg/s`, C 대비 `53.1%`
  - 목표에 미달하고 `Message.WrapBytes`의 zero-copy 의도와도 맞지 않아 원복했다.
- 보류 이유
  - 현재 public API를 유지한 내부 후보는 목표에 닿지 못했다.
  - public API 변경 후보도 실측 개선 폭이 작아 최후 수단으로 계속 진행할 근거가 부족하다.

## Java

- `MULTI_PUBSUB/tcp/64`
  - Java: `2075974.2`
  - C 기준: `3518022.8`
  - C 대비: `59.0%`
  - 목표: `63%`
  - 상태: 미달
  - 결과: `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260518_111324_codex_java_mpubsub_tcp64_after_sendbuilder_single_storage.txt`
- `MULTI_PUBSUB/tcp/256`
  - Java: `1960535.6`
  - C 기준: `3004889.0`
  - C 대비: `65.2%`
  - 목표: `63%`
  - 상태: 통과
  - 결과: `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260518_110614_codex_java_mpubsub_tcp64_256_after_reuse_topicmsg.txt`
- `MULTI_DEALER_ROUTER/tcp/65536`
  - Java: `88922.6`
  - C 기준: `190471.4`
  - C 대비: `46.7%`
  - 목표: `50%`
  - 상태: 미달
  - 결과: `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260518_111220_codex_java_mdr_tcp65536_131072_after_router_single_fastpath.txt`
- `MULTI_DEALER_ROUTER/tcp/131072`
  - Java: `44766.0`
  - C 기준: `79036.6`
  - C 대비: `56.6%`
  - 목표: `50%`
  - 상태: 통과
  - 결과: `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260518_111220_codex_java_mdr_tcp65536_131072_after_router_single_fastpath.txt`
- SPOT 계열
  - `MsgUnit(B)=4096` 불일치 때문에 64B 유효 비교로 보지 않는다.

## Node

- `PUBSUB/tcp/64`
  - Node: `454589.0`
  - C 기준: `1226642.4`
  - C 대비: `37.06%`
  - 목표: `35%`
  - 상태: 통과
  - 결과: `bindings/node/perf/results/single/report/perf_node_single_linux_20260518_111604.txt`
- `PUBSUB/tcp/256`
  - Node: `371307.4`
  - C 기준: `1022904.8`
  - C 대비: `36.30%`
  - 목표: `35%`
  - 상태: 통과
  - 결과: `bindings/node/perf/results/single/report/perf_node_single_linux_20260518_111503.txt`
- 주의
  - 위 두 조합만 확인했다. 모든 pattern, size, transport 완료가 아니다.

## Go

- `PAIR/tcp/64`: C 대비 `79.80%`, 상태 통과
- `DEALER_DEALER/tcp/64`: C 대비 `78.05%`, 상태 통과
- `DEALER_ROUTER/tcp/64`: C 대비 `45.12%`, 목표 `47%`, 상태 미달
- `ROUTER_ROUTER/tcp/64`: C 대비 `50.49%`, 상태 통과
- `PUBSUB/tcp/64`: C 대비 `9.78%`, 상태 미달
- `SPOT/tcp/64`: C 대비 `29.65%`, 상태 미달
- 결과
  - `bindings/go/perf/results/single/report/perf_go_single_linux_20260518_115650_codex_go_tcp64_single_recv_into.txt`
  - `bindings/go/perf/results/single/report/perf_go_single_linux_20260518_120037_codex_go_tcp64_pubsub_adopt_recv.txt`
- 주의
  - Go single report는 raw socket `MsgUnit(B)`를 출력하지 못한다.
  - 기존 SPOT 계열은 `MsgUnit(B)=4096`이라 64B 비교 기준으로 쓰지 않는다.

## Python

- `MULTI_DEALER_DEALER/tcp/64`: C 대비 `4.55%`, 상태 미달
- `MULTI_PUBSUB/tcp/64`: C 대비 `4.42%`, 상태 미달
- `MULTI_DEALER_ROUTER/tcp/64`: C 대비 `9.98%`, 상태 미달
- `MULTI_ROUTER_ROUTER/tcp/64`: C 대비 `8.47%`, 상태 미달
- `MULTI_STREAM/tcp/64`: C 대비 `0.82%`, 상태 미달
- `MULTI_SPOT_REQREP/tcp/64`: C 대비 `0.24%`, 상태 미달
- `MULTI_SPOT_SENDSEND/tcp/64`: C 대비 `3.53%`, 상태 미달
- `MULTI_SPOT/tcp/64`: client timeout, 상태 미달
- 결과
  - `bindings/python/perf/results/multi/report/perf_python_multi_linux_20260518_115738_codex_python_tcp64_owned_recv.txt`
- 주의
  - SPOT 계열은 `MsgUnit(B)=4096`이라 64B 비교 기준으로 쓰지 않는다.
