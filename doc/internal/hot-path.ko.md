# Hot Path 계약 메모

> 목적: 성능개선 작업과 리팩토링이 동시에 진행될 때,
> small-message steady-state hot path가 무의식적으로 두꺼워지는 일을 막기 위한
> 내부 계약 문서다.
>
> 이 문서는 "어디가 hot path인지", "무엇을 넣으면 안 되는지", "성능개선 중 어떤
> 규칙으로 문서를 갱신할지"를 짧고 명시적으로 유지한다.

## 0. Current Operating Summary

### 0.1 Current State

- 현재 공통 gap의 상위 해석은 `backpressure-only`가 아니라
  `producer-side steady-state send/publication`이다.
- high-HWM probe에서도 `PAIR`, `DEALER_DEALER`, `PUBSUB` 64B 격차가 크게
  남았으므로, queue가 차지 않아도 드는 send/publication 고정비를 우선 본다.

### 0.2 Current Hypothesis

- `libzmq` 대비 common differential은
  [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  / [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  의 public lifecycle coordinator와
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  / [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
  의 send-path serialization 조합에 더 가깝다.

### 0.3 Kept Delta

- current kept 공통 delta는
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  의 recursive `check_hwm()` elide다.

### 0.4 Rejected Families

- `backpressure/HWM`만을 1차 원인으로 보는 해석
- `PAIR` 전용 no-sync lifecycle CAS fast path
- `fast_mutex` common-path TID-lazy 후보
- 새 broad evidence 없이 `dist/xpub/pipe` local helper만 반복 추가하는 탐색

### 0.5 Do Not Revisit

- high-HWM probe를 뒤집는 새 증거 없이 `wakeup latency`를 1차 원인으로 다시
  올리지 않는다.
- broad win 없이 특정 pattern 하나만 좋아지는 helper-level 후보를 계속
  누적하지 않는다.

### 0.6 Next Exact Step

- restart 시 `libzmq` 대응 send/publication path를 먼저 읽고,
  `public lifecycle coordinator`와 `pipe send-path serialization` 중 어느
  구조가 현재 common differential의 더 큰 본체인지 재확인한다.
- `PUBSUB`는 local helper를 누적하기보다 sender publication과 `XSUB`
  receiver-drain differential을 먼저 분리한다.

## 1. 범위

현재 1차 범위는 `with_zmq single` 상대 비교에서 gap이 가장 직접적으로 드러나는
public send/recv 경로다.

- [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
- [`core/src/api/socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
- [`core/src/core/recv_internal.cpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_internal.cpp)
- [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
- [`core/src/sockets/pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
- [`core/src/sockets/dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
- [`core/src/core/recv_tls_view.hpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_tls_view.hpp)

현재 기준 패턴은 아래 두 개를 최우선으로 본다.

- `PAIR`
- `DEALER_DEALER`

이 둘은 pattern-specific framing이 가장 적어서 공통 원인과 공통 회귀를 보기
가장 좋다.

보조 해석 규칙:

- `echo`는 거의 동등한데 `oneway`에서만 gap이 커지면
  recv보다 send-side publication/backpressure 쪽을 먼저 본다.
- `echo`는 round-trip pace 때문에 queue/HWM 경로가 덜 드러나고,
  `oneway`는 success path와 backpressure 복귀 비용이 그대로 드러난다.
- backpressure 분석에서는 `activate_write publication`과
  `sender retry consumption`을 분리해서 본다.
  현재 코드는 후자 쪽이 더 유력하다.
- `PUBSUB`의 delivery-ready monitor bookkeeping은 steady-state에서
  건너뛰어도 bench 수치가 거의 움직이지 않았다.
  현재 `PUBSUB` gap의 상위 축은 monitor-ready 계산보다
  publication/ordering 경로에 더 가깝다.
- 다만 2026-03-28 `dist_t` one-matching-pipe fast path를 넣자
  `PUBSUB tcp 64B`가 `-24.23%`, rerun에서 `-26.00%`까지 즉시 회복됐다.
  즉 monitor-ready는 여전히 secondary지만, distributor loop/index/deactivate
  work 자체는 실제 hot-path 비용 축이다.
- 반면 같은 라운드의 `PUBSUB inproc 64B`는 `-42.51%`,
  multi `pubsub tcp 64B`는 `-26.97%`라서 publication/lifecycle differential은
  아직 남아 있다.
- 이후 `compile_commands.json` 확인으로 current build target mismatch를
  잡아냈고,
  [`core/bench/with_zmq/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/CMakeLists.txt)
  `comp_zlink_pubsub`를
  [`core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp)
  기준으로 다시 연결했다.
- 현재 realigned single `PUBSUB` surface는
  `zlink_publish(NULL, &part, 1)` + `zlink_msg_recv()` single-part payload
  경로다.
- surface realignment 직후 historical single `PUBSUB 64B`는
  `tcp -15.29%`, `inproc -24.92%`였다.
- 같은 realigned surface의 historical probe에서
  `XPUB_NODROP=0`은 `tcp -0.00%`, `inproc -0.64%`였고,
  `HWM=16`은 `tcp -16.17%`, `inproc +47.89%`였다.
- historical multi `pubsub tcp 64B`는
  default HWM `-29.83%`,
  `BENCH_MULTI_PUBSUB_HWM=16 -22.22%`였다.
- 다만 2026-03-28 current retained-code direct recheck에서는
  single `PUBSUB 64B`가 seq1 `tcp/inproc -21.73% / -19.43%`,
  rerun `-22.44% / -31.08%`였고,
  `XPUB_NODROP=0` probe는 `-0.06% / +0.04%`,
  latest multi `pubsub tcp 64B`는 `-22.75%`였다.
- 그 뒤 current retained delta는
  `xsub` empty-subscription accept-all fast path와
  requested-only `last_recv_source_rid` capture를 결합한
  receiver-drain specialization으로 옮겨갔고,
  isolated first/rerun `PUBSUB 64B`는
  `tcp/inproc -9.40% / -20.35%`, `-10.43% / -21.59%`,
  multi `pubsub tcp 64B`는 `+9.25%`, rerun `+8.25%`까지 회복했다.
- 따라서 current `PUBSUB` 잔여 gap의 본체는
  single-subscriber `dist` helper를 더 얹는 문제가 아니라,
  `xsub` receiver steady-state와 `inproc` backpressure differential이
  실제로 얼마나 남는지 더 좁게 구분하는 쪽이다.
- 추가 high-HWM single probe
  [`perf_linux_20260328_124815.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_124815.txt)
  에서는 `hwm/sndhwm/rcvhwm = 1000000`인데도
  `PAIR 64B tcp/ipc/inproc -33.07% / -30.34% / -30.77%`,
  `PUBSUB -38.93% / -22.86% / -34.66%`,
  `DEALER_DEALER -34.54% / -27.66% / -28.64%`였다.
  즉 current gap의 본체를 `HWM` 도달과 backpressure wakeup 하나로
  설명하긴 어렵고, queue가 차지 않아도 남는 steady-state per-message
  고정비를 우선 본다.
- 이 high-HWM 결과와 `echo ~= 동등, oneway = 격차` 패턴을 합치면,
  current 공통 differential의 최우선 해석은
  `backpressure-only`가 아니라
  `producer-side steady-state send/publication`이다.
  현재 가장 유력한 공통 축은
  [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  /
  [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  의 public lifecycle coordinator와
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  /
  [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
  의 send-path serialization 비용이다.
- libzmq 대응 위치는
  [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp),
  [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)이다.
  libzmq는 non-thread-safe send에서 optional lock이 사실상 no-op이고,
  steady-state pipe write/flush가 lock-free SPSC queue 위에서 동작한다.
  반면 zlink는 same-ordering을 더 비싼 lifecycle/serialization 구조로
  유지하고 있어 current oneway gap이 transport 전반에서 같이 드러난다.
- 2026-03-28 no-sync public send admission CAS fast path probe는
  unit/integration contract는 지켰지만
  focused `PAIR/DEALER` bench에서 broad win으로 이어지지 않아 원복했다.
  따라서 current next step은 `PAIR` 전용 no-sync tweak보다
  sync 소켓까지 포함하는 broader lifecycle/serialization 설계 차이를
  더 직접 겨냥하는 것이다.
- 같은 날 `pipe::check_write_status()`, `write()`, `write_and_flush()`에서
  `_out_sync`를 이미 잡은 뒤 `check_hwm()`이 다시 recursive lock을 한 번 더
  밟는 구조를 `check_hwm_unlocked()`로 바꾼 candidate는 keep했다.
  focused `PAIR/DEALER` 64B에서
  `PAIR tcp +3.80`%p, `DEALER_DEALER tcp/inproc +2.04`%p / `+1.16`%p
  수준의 회복이 있었고,
  이는 current send-path differential에
  same-lock recursive bookkeeping이 실제 비용 축이라는 신호다.
- 반면 `fast_mutex` common unlocked path에서 owner==0일 때
  TID 조회를 늦추는 candidate는 clean rerun에서
  `PAIR tcp`를 크게 악화시켜 원복했다.
  현재 differential을 `fast_mutex` helper 표면 하나로 보는 건 맞지 않고,
  다음 단계는 다시 broader lifecycle / same-ordering serialization 구조를
  겨냥해야 한다.
- earlier `perf_pubsub.cpp` auxiliary surface의 추가 HWM sweep
  `16/64/256/1000`도 크게 흔들렸지만, 이 값은 current acceptance surface의
  primary baseline이 아니다.
- current `PUBSUB` gap은 여전히 HWM/backpressure 축에 민감하지만,
  우선 해석은 realigned surface의 `XPUB_NODROP=0` sign flip과
  retained `xsub` receiver-drain delta 이후에도 남아 있는
  `inproc` differential에 둔다.
- 2026-03-28 single comparison report가 queue probe 지표를 저장하도록
  갱신된 뒤 default `PUBSUB tcp/inproc 64B`는
  `snd_pending_max 654 / 1450`,
  `rcv_pending_max 513 / 725`로 기록됐다.
- 같은 report 형식에서 `XPUB_NODROP=0` probe는
  `snd_pending_max 508 / 946`,
  `rcv_pending_max 622 / 1017`,
  `HWM=16` probe는 `snd_pending_max 9 / 20`,
  `rcv_pending_max 15 / 28`였다.
- 즉 current `inproc` differential은 receiver latency보다
  default HWM + `XPUB_NODROP=1` 조건에서 sender backlog가 더 크게 누적되는
  쪽과 더 잘 맞는다.
- 다만 above queue metrics는 earlier `perf_pubsub.cpp` auxiliary surface에서
  얻은 값이고, current realigned `bench_zlink_pubsub.cpp` acceptance surface는
  이 지표를 직접 내보내지 않는다.
- 같은 날 `pipe::check_hwm()` precheck atomic candidate는
  same-day baseline `PUBSUB tcp/inproc 64B -25.63% / -37.06%` 대비
  first `-26.71% / -34.02%`, rerun `-23.86% / -37.64%`로 엇갈려
  stable broad win이 아니어서 현재 코드에는 없다.
- `PERF_SINGLE_QUEUE_SAMPLE_MS=100000` sparse queue-probe semantic run도
  `PUBSUB tcp/inproc 64B -27.48% / -35.63%`여서
  current gap의 본체를 queue-probe overhead로 설명하진 못했다.
- `inproc PUBSUB` peer-progress notify interval tighten candidate도
  single `PUBSUB tcp/inproc 64B -22.42% / -37.17%`,
  multi `pubsub tcp 64B -26.38%`로 keep-worthy delta가 아니었고,
  sender backlog를 줄이는 대신 receiver backlog를 키우는 쪽으로 움직였다.
- `inproc PUBSUB` HWM-full peer snapshot refresh candidate도
  single `PUBSUB tcp/inproc 64B -22.84% / -38.24%`여서
  current gap을 cached peer progress 하나로 설명하진 못했다.
- 최근 recv-mode busy-bit cached guard candidate도
  first/rerun `PUBSUB tcp/inproc 64B`
  `-16.20% / -27.20%`, `-20.29% / -32.27%`로
  first-run `tcp`만 좋아 보이고 rerun `inproc`이 다시 나빠져
  stable broad win이 아니었다.
- no-topic single-part `zlink_publish` fast path도
  first/rerun `PUBSUB tcp/inproc 64B`
  `-16.16% / -28.59%`, `-15.30% / -34.26%`,
  multi `pubsub tcp 64B -31.41%`, rerun `-25.50%`로
  single/multi를 함께 지키지 못해 현재 코드에는 없다.
- `pipe` `activate_read` pending dedupe candidate도
  first/rerun `PUBSUB tcp/inproc 64B`
  `-27.06% / -29.49%`, `-35.43% / -20.57%`,
  multi `pubsub tcp 64B -32.15%`로 rejected candidate가 됐다.
- 다만 current retained code는 위 rejected candidate들과 달리
  `xsub` empty-subscription accept-all fast path와
  requested-only source-rid capture를 결합해
  isolated first/rerun `PUBSUB tcp/inproc 64B`
  `-9.40% / -20.35%`, `-10.43% / -21.59%`,
  multi `pubsub tcp 64B +9.25%`, rerun `+8.25%`를 만들었다.
- 따라서 next overall step은 `PUBSUB` 계열 micro helper를 더 파는 것이 아니라,
  current retained `PUBSUB` delta를 유지한 채
  `ROUTER_ROUTER` routed path differential로 우선순위를 넘기는 것이다.
- 즉 empty-topic frame/topic-aware recv surface mismatch는 실제로 있었지만,
  현재 `PUBSUB` 잔여 gap을 그 차이 하나로 설명할 수는 없다.
- 이후 2026-03-28 same-handle concurrent `PUB` publish regression
  (`test_pubsub_publish_is_safe_from_multiple_threads`)이
  topic+payload interleave로 재현돼,
  현재 코드는 logical multipart publish/send 전체를 하나의 public send
  scope로 묶어 contract를 회복했다.
- 다만 이 fix 뒤 latest single `PUBSUB` public rerun은
  `tcp/inproc -30.71% / -40.37%`였고,
  no-topic single-part direct-send fallback rerun도
  `-31.67% / -38.76%`로 broad win이 아니었다.
- 즉 logical multipart send scope는 이제 correctness contract로 유지하되,
  `PUBSUB` 잔여 gap을 줄이는 다음 단계는 같은 contract를 유지한 채
  publication/lifecycle cost를 더 줄이는 쪽이어야 한다.

## 2. 현재 비교 surface

현재 `with_zmq single`의 `PAIR`/`DEALER_DEALER`는 아래를 비교한다.

- zlink:
  - `zlink_send(parts, 1)`
  - `zlink_recv(&parts, &count)`
- libzmq:
  - `send_exact(buffer)`
  - `zlink_msg_recv(msg)` 즉 `zmq_msg_recv()`

즉 현재 측정은 raw transport core만 비교하는 것이 아니라,
`zlink`의 public API cost를 함께 포함한다.

특히 `PUBSUB` single은 현재
`NULL topic + zlink_msg_recv()` payload-only 경로로 realign됐다.
따라서 current `PUBSUB` single gap은 이전 build-target mismatch보다
publication/lifecycle/backpressure differential을 더 직접적으로 반영한다.

이 문서의 hot path도 그 기준으로 정의한다.

다만 raw/public 해석은 현재 고정된 결론이 아니다.

- 2026-03-28 직렬 rerun에서 `PAIR` zlink 절대 throughput은
  public→raw가 `tcp 3200.10 -> 3367.91`, `inproc 2816.95 -> 3015.08`로
  회복됐다.
- 반면 `DEALER_DEALER`는 public→raw가
  `tcp 2830.18 -> 3263.08`로 회복됐지만,
  `inproc 3145.71 -> 2799.88`로 다시 악화됐다.
- 같은 날 logical multipart publish contract fix 뒤 serial rerun에서도
  `PAIR` public→raw가 `2717.91 -> 3212.40`, `3326.33 -> 3136.33`,
  `DEALER_DEALER` public→raw가 `3126.42 -> 3135.91`,
  `3163.47 -> 3138.42`로 다시 mixed였다.

즉 현재는 "`public wrapper penalty가 이미 low single-digit`"라고
고정하지 않는다. raw/public 분리는 send-side 변경 뒤 매번 다시 찍어야 하는
guardrail이고, 패턴/transport별로 엇갈릴 수 있다.

즉 현재 hot path 계약의 해석은 이렇게 고정한다.

- `surface mismatch`는 현재 비교를 읽을 때 항상 붙여야 하는 해석 축이다.
- 하지만 `raw/public` 분리는 현재 고정 결론이 아니라
  iteration별 guardrail/해석 축이다.
- 실제 코드 개선 우선순위는 그 다음에 오는 send lifecycle,
  send-side ordering/publication, pattern-specific public path 쪽이다.

## 3. 현재 hot path

### 3.1 send

steady-state single-part send hot path:

`zlink_send()`  
→ `send_socket_unrouted_parts()`  
→ `socket_base_t::send()`  
→ `xsend()`

핵심 파일:

- [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
- [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)

### 3.2 recv

steady-state single-part recv hot path:

`zlink_recv()`  
→ `recv_socket_parts()`

현재 `PAIR`/`DEALER`의 direct single-part 경로는 별도 fast path를 가진다.

`zlink_recv()`  
→ `recv_socket_parts()`  
→ `recv_tls_view::begin_with_first_slot()`  
→ `socket_base_t::recv()`  
→ `xrecv()`  
→ `recv_tls_view::commit_reserved_single()`

그 외 routed/strip/multipart 경로는 여전히:

`zlink_recv()`  
→ `recv_socket_parts()`  
→ `recv_msg_socket()` 또는 `recv_msg_routed_socket()`  
→ `socket_base_t::recv()` 또는 `recv_routed()`  
→ `recv_tls_view::export_single()` 또는 `push()/commit()`

핵심 파일:

- [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
- [`recv_internal.cpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_internal.cpp)
- [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
- [`recv_tls_view.hpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_tls_view.hpp)

## 4. 현재 상위 비용 축

현재 코드 기준 상위 비용 축은 아래와 같다.

1. `zlink_send()`의 public lifecycle/backpressure path
2. blocking retry에서 반복되는 `public_api_sync` 재획득 또는
   retry 동안 sync를 유지하지 못해 생기는 복귀 비용
3. send-side `pipe_t`의 per-message serialization cost
4. `zlink_recv()`의 남아 있는 aggregate/TLS export와 routed/strip 경로
5. `recv_internal.cpp`의 dispatch/mode guard
6. `PUBSUB`/`ROUTER` 계열의 pattern-specific public surface 차이

여기서 `PUBSUB inproc` 잔여 gap은 현재도 `tcp`보다 훨씬 크게 남으므로,
`pipe_t`의 `fast_mutex_t`/publication ordering cost를
transport-specific probe 없이 건너뛰지 않는다.

반대로 현재 기준 상위 후보가 아닌 것은:

- `surface mismatch` 자체를 현재 gap의 본체로 보는 해석
- `fq/lb` 재작성
- `PAIR/DEALER` algorithm 자체 변경
- 이미 HEAD에서 빠진 `last_recv_source_rid`
- thread-safe 증명 없이 `pipe` lock 제거

여기서 `pipe` 항목은 현재 가장 큰 단일 후보 중 하나지만,
최근 no-op 제거 실험이 모두 악화됐기 때문에 "바로 제거" 후보가 아니다.
현재는 `object` command 기반 상태 전이와 activation/progress ordering과
묶여 있으므로, "같은 의미를 더 싸게 제공할 방법을 찾아야 하는 고위험 후보"로
본다.

또한 현재 `PAIR`/`DEALER_DEALER` raw/public 분리 결과를 보면,
`zlink_recv()` wrapper를 계속 얇게 만드는 것만으로는 전체 gap이 닫히지 않는다.
즉 recv public path는 여전히 hot path지만, 현재 상위 본체는 send-side에 더 가깝다.

## 5. 리팩토링 금지 규칙

아래 규칙을 깨는 변경은 성능개선 명분으로도 넣지 않는다.

### 5.1 send

- single-part send hot path에 heap allocation을 추가하지 않는다
- single-part send hot path에 retry를 위한 clone/materialization을 다시 넣지 않는다
- validation/helper 분기 계층을 늘릴 때는 `PAIR/DEALER` steady-state 영향부터 확인한다

### 5.2 recv

- single-part recv hot path에 heap allocation을 추가하지 않는다
- single-part recv를 multipart materialization 일반 경로로 밀어 넣지 않는다
- recv mode 경로에 callback/service용 상태 분기를 무심코 추가하지 않는다
- TLS export를 유지하더라도 single-part 경로는 가능한 한 가장 얇게 유지한다

### 5.3 lifecycle / thread-safe

- lifecycle coordinator 의미를 약화하지 않는다
- callback/close handoff 계약을 약화하지 않는다
- command 대상 객체인 `pipe`의 동기화를 증명 없이 제거하지 않는다

특히 현재 [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
의 `check_read/read/check_write/write/flush`는 steady-state message마다
`fast_mutex_t`를 잡는다. libzmq [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)
에는 같은 잠금이 없다. 따라서 이 항목은 "영향이 작은 마지막 후보"가 아니라
"영향은 크지만 correctness risk도 큰 후보"로 유지한다.

## 6. 파일 주석 규칙

아래 파일에는 짧은 hot path 경고 주석을 유지한다.

- [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
- [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
- [`recv_internal.cpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_internal.cpp)
- [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
- [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)

주석은 설명형보다 계약형으로 짧게 쓴다.

예:

- `Hot path: keep single-part recv free of heap allocation and extra indirection.`
- `Hot path: changes here affect PAIR/DEALER small-message throughput.`

## 7. 업데이트 규칙

이 문서는 아래 경우에 반드시 갱신한다.

1. hot path 원인분석 우선순위가 바뀌었을 때
2. 현재 P0 후보가 제거되거나 무효화됐을 때
3. benchmark surface 해석이 바뀌었을 때
4. 새로운 public fast path 또는 raw fast path가 추가됐을 때

특히 stale한 가설을 남겨두지 않는다.

예:

- 과거엔 유력했지만 현재 HEAD에서는 빠진 항목
- benchmark surface가 달라져 더 이상 같은 의미가 아닌 비교

이런 항목은 즉시 문서에서 historical note로 내리거나 제거한다.

## 8. 현재 작업 방향

현재 기준 다음 성능개선 순서는 아래가 맞다.

1. `PAIR`/`DEALER_DEALER` raw/public 분리는 완료됐지만,
   public penalty를 고정 상수처럼 취급하지 않고 serial guardrail로 유지한다
2. `socket_base_t::send()`의 public lifecycle fast path는
   current code 기준 keep-worthy 공통 delta가 없어 actual 구현 우선순위에서
   내린다. 새 broad win 근거가 생길 때만 다시 올린다
3. blocking retry 공통 gate도 현재는 같은 상태다
   - `enter_public_api()` 자체보다 `public_api_sync` 재획득 또는
     retry-side sync 유지 실패가 비용이라는 해석은 유지한다
   - 다만 2026-03-28 current code 기준 keep-worthy 공통 retry delta는 없다
   - `activate_write` 자체는 zlink가 같은-thread에서 즉시
     `process_command()`를 호출하므로 publication이 더 늦다고 단정하지 않는다
   - 따라서 actual next work는 공통 retry gate 자체보다
     pattern-specific publication/wakeup differential 쪽이다
4. send-side `pipe_t`는 전체 lock 제거가 아니라
   activation/flush ordering을 유지한 채 lock 안의 work를 줄이는 방향으로 본다
5. `socket_base_t::send()`의 send-side throttle은 보조 후보로만 본다
   - 현재 `process_commands(0, true)`는 libzmq와 거의 같은 구조다
   - `counter-only` 치환은 현재 보류다
6. `zlink_recv()`의 남은 routed/strip/multipart export 경로를 더 얇게 만든다
7. `recv_internal.cpp` mode guard 비용은 busy-bit specialization 후보가
   stable broad win이 아니었으므로, 지금은 publication/backpressure 정리 뒤의
   보조 가설로만 둔다
8. `PUBSUB` semantic/backpressure map은 current code 기준으로 다시 정리됐다.
   - surface realignment 직후 historical default single:
     `tcp/inproc -15.29% / -24.92%`
   - current retained-code recheck:
     seq1 `tcp/inproc -21.73% / -19.43%`,
     rerun `-22.44% / -31.08%`
   - current `XPUB_NODROP=0` probe:
     `tcp/inproc -0.06% / +0.04%`
   - historical `HWM=16` probe:
     `tcp/inproc -16.17% / +47.89%`
   - queue probe report:
     default `snd_pending_max 654 / 1450`,
     `XPUB_NODROP=0 508 / 946`,
     `HWM=16 9 / 20`
   - latest multi `pubsub tcp 64B` recheck:
     default `-22.75%`
   즉 low-HWM single win이나 `XPUB_NODROP=0` sign flip을 acceptance로
   오해하면 안 된다. current next work는
   default HWM + `XPUB_NODROP=1` publication/backpressure differential이고,
   이 분리 없이 `dist.cpp` / `xpub.cpp` / `pipe publication` 미세 후보를
   다시 추가하지 않는다.
   `ROUTER`는 이 semantic map을 반영한 뒤 다음으로 별도 정리한다.

## 8.1 현재 보류/기각된 방향

아래 방향은 현재 hot path 계약상 바로 진행하지 않는다.

1. `pipe` lock 전체 제거
2. `pipe` activation을 lock 밖으로 이동
3. mailbox read lock 제거
4. send-side throttle의 counter-only 치환
5. pipe 전용 non-reentrant mutex
6. `routing_socket_base` single-out-pipe lookup cache
   - 2026-03-28 `ROUTER_ROUTER` single rerun이 `tcp -56.74%`,
     `inproc -26.10%`로 broad win이 아니었다
   - 즉 현재 `ROUTER` 잔여 gap을 routed map lookup 하나로 설명하진 않는다
7. `XPUB` single matching `nodrop` HWM+write fusion
8. `socket_runtime.cpp` `public_api_state` full enter/leave CAS fast path
   - clean A/B에서 `DEALER` raw는 일부 좋아졌지만
     `PAIR` public `tcp/inproc`가 `-38.34% / -33.15%`,
     `PAIR inproc raw`가 `-36.11%`까지 흔들려 broad win이 아니었다
9. `socket_runtime.cpp` `unlock_public_api_sync_and_leave()` CAS fast path
   - raw는 `PAIR tcp -7.94%`, `DEALER_DEALER inproc -19.30%`까지 회복했지만
     public rerun에서 `DEALER_DEALER tcp/inproc`가
     `-27.23% / -30.85%`로 다시 흔들려 rejected candidate로 둔다
10. `socket_runtime.cpp` `PAIR` no-sync send scope enter+leave fast path
   - raw는 `PAIR tcp/inproc -19.98% / -19.77%`까지 회복했지만
     public seq에서 `PAIR tcp/inproc`가 `-37.97% / -32.71%`로 다시 벌어져
     raw/public guardrail을 깨뜨렸다
11. `socket_runtime.cpp` `PAIR` no-sync send scope leave-only fast path
   - `PAIR` 자체는 `tcp/inproc -17.01% / -19.88%`로 덜 흔들렸지만
     같은 seq run의 `DEALER_DEALER tcp/inproc`가
     `-37.43% / -34.21%`로 내려가 broad win 근거가 없었다
12. `xpub.cpp` all-attached empty-prefix `send_to_all` fast path
   - isolated `PUBSUB tcp 64B`와 multi `pubsub tcp 64B`는 회복했지만
     `PUBSUB inproc 64B -43.96%`, broader single `PUBSUB tcp/inproc`
     `-30.53% / -42.65%`로 broad win이 아니었다
   - 즉 current `PUBSUB` gap을 empty-prefix trie match 제거 하나로
     설명하진 않는다
13. `xpub.cpp` single attached empty-prefix matching fast path
   - first run은 `PUBSUB tcp/inproc -23.74% / -36.67%`로 좋아 보였지만
     clean rerun이 `-31.16% / -47.67%`로 무너져 broad win이 아니었다
   - 즉 current `PUBSUB` gap을 single-pipe empty-prefix match 제거 하나로도
     설명하진 않는다
14. `xpub.cpp` single-subscriber ready-count fast path
   - clean first/rerun이 `PUBSUB tcp/inproc -26.22% / -38.31%`,
     `-28.90% / -42.92%`로 accepted baseline을 넘지 못했다
   - delivery-ready bookkeeping은 current code 기준으로도 secondary다
15. `router.cpp` routed send prefix/HWM second-check elimination
   - `check_write_status()` 뒤 `write_no_hwm_check()+flush()`로
     한 번 더 도는 HWM 확인을 줄여도
     `ROUTER_ROUTER tcp/inproc -55.19% / -25.05%`라 broad win이 아니었다
   - 즉 current `ROUTER` gap을 prefix/HWM recheck 하나로 설명하진 않는다
16. `socket_message_recv_api.cpp` / `router.cpp` routed recv
    source-rid zero-elision
   - direct routed recv에서 256B `zlink_routing_id_t` zero-fill 하나를 줄여도
     `ROUTER_ROUTER tcp/inproc -58.34% / -33.47%`로 오히려 더 흔들렸다
   - 즉 current `ROUTER` gap을 source-rid zero-fill 하나로도 설명하진 않는다
17. `xpub.cpp` / `xsub.cpp` `xwrite_activated()` delivery-ready refresh 제거
   - write re-activation은 current ready-count 정의를 바꾸지 않으므로
     여기서 recompute를 빼도 될 것처럼 보였지만,
     single `PUBSUB tcp/inproc -27.31% / -44.93%`로 baseline보다 악화됐다
   - 즉 current `PUBSUB` gap을 `write_activated` monitor-ready refresh
     하나로 설명하진 않는다
18. `dist.cpp` single-pipe `match()/activated()` bookkeeping fast path
   - sequential seq1/seq2/seq3 `PUBSUB tcp/inproc`가
     `-24.81% / -43.41%`, `-22.71% / -34.67%`,
     `-24.42% / -41.68%`로 흔들렸다
   - seq2만 보면 candidate처럼 보였지만 seq1/seq3는 accepted baseline
     `-23.63% / -39.84%`를 stable하게 넘지 못했다
   - 즉 current `PUBSUB` 잔여 gap을 single-pipe dist bookkeeping 하나로
     설명하진 않는다
19. `dist.cpp` final-part same-thread `send_activate_read()` inline wakeup
   - current accepted `dist` helper 위에서 same-thread flush wakeup을
     inline으로 전달해도 isolated `PUBSUB tcp/inproc 64B`가
     `-25.70% / -42.49%`로 accepted baseline보다 둘 다 나빠졌다
   - 즉 current `PUBSUB` 잔여 gap을 same-thread `activate_read`
     mailbox bounce 하나로 설명하진 않는다
20. `XPUB` same first-part retry matching cache
   - default HWM + `XPUB_NODROP=1` retry/publication differential을
     좁히기 위해 same first-part `EAGAIN` retry에서 trie rematch를
     cache해 봤지만 `PUBSUB tcp/inproc 64B`가
     `-26.40% / -44.32%`로 `tcp`는 noise 수준,
     `inproc`은 semantic-map baseline보다 악화됐다
   - 즉 current `PUBSUB` 잔여 gap을 retry rematch elision 하나로
     설명하진 않는다
21. same-thread `activate_write` mailbox 정렬
   - libzmq와 맞춰 same-thread `activate_write`도 mailbox command로
     보내 봤지만 `PUBSUB tcp/inproc 64B`가
     `-26.81% / -43.47%`로 `tcp`는 noise 수준,
     `inproc`은 semantic-map baseline보다 더 나빠졌다
   - 즉 current `PUBSUB` 잔여 gap을
     `activate_write` publication channel mismatch 하나로 설명하진 않는다
22. `pipe/dist/xpub` single-matching `XPUB_NODROP=1` one-lock helper
   - single-subscriber default-HWM path에서 `_dist.check_hwm()`와
     actual `write/flush` 사이의 double `_out_sync` 진입을
     one-lock helper로 합쳐 봤지만,
     isolated seq1/seq2/seq3 `PUBSUB tcp/inproc 64B`가
     `-27.73% / -38.36%`, `-20.44% / -38.87%`, `-12.75% / -38.71%`로
     흔들렸다
   - broader single `PUBSUB tcp/inproc`는 `-21.12% / -26.31%`까지
     회복했지만, multi `pubsub tcp 64B` first/rerun/`--runs 3`가
     `-25.93%`, `-21.63%`, `-25.68%`로 latest baseline `-17.24%`를
     안정적으로 지키지 못했다
   - 즉 current `PUBSUB` 잔여 gap을
     "single-pipe NODROP precheck와 write를 한 lock으로 합치자" 하나로
     설명하진 않는다

이 항목들은 최근 A/B 실험이나 current code invariant 기준으로
이미 역효과가 확인됐거나 correctness risk가 높다.

- `XPUB` single matching `nodrop` HWM+write fusion은
  `PUBSUB tcp 64B`를 `-34.30%`, rerun `-36.14%`로 다시 악화시켰다.
  `check_hwm()` precheck를 single-pipe write와 합치는 발상 자체는 타당했지만,
  현재 구현에서는 keep-worthy broad win이 아니므로 rejected candidate로 둔다.
- `public_api_state` CAS fast path 실험 둘은 "send-side lifecycle atomic을 더
  싸게 만들면 회복할 수 있다"는 방향성 자체는 유지시켰지만,
  current code 기준으로는 raw/public guardrail과 `PAIR`/`DEALER` broad win을
  동시에 만족시키지 못했다. 즉 이 축은 여전히 P0이지만, 지금 형태의
  uncontended CAS 치환은 유지 후보가 아니다.
- `PAIR` no-sync send scope fast path 둘도 같은 이유로 rejected candidate로 둔다.
  `PAIR` admission/leave를 더 싸게 만들고 싶다는 방향은 맞지만,
  current public surface에서는 오히려 raw/public 분리나 non-`PAIR` guardrail을
  동시에 깨뜨렸다.
- `XPUB` single attached empty-prefix matching fast path와
  single-subscriber ready-count fast path도 같은 이유로 rejected candidate다.
  둘 다 `PUBSUB` single first run에서는 일부 회복이 보였지만
  clean rerun과 broader 해석에서 keep-worthy broad win을 만들지 못했다.
  즉 current `PUBSUB` 잔여 gap의 본체는 trie match나 ready-count bookkeeping보다
  publication/wakeup differential 쪽이다.
- `ROUTER` routed send prefix/HWM recheck elimination과
  routed recv source-rid zero-elision도 같은 이유로 rejected candidate다.
  둘 다 routed public 미세 비용 축은 건드렸지만 current single acceptance에서
  keep-worthy broad win을 만들지 못했다.
- `XPUB` / `XSUB` `xwrite_activated()` delivery-ready refresh 제거도
  같은 이유로 rejected candidate다. monitor-ready recompute를 빼는 발상은
  그럴듯했지만 current `PUBSUB` single acceptance에서는 오히려 악화됐다.
- `dist.cpp` single-pipe `match()/activated()` bookkeeping fast path도
  같은 이유로 rejected candidate다. seq2만 보면 좋아 보였지만
  seq1/seq3가 accepted baseline 아래로 다시 내려가
  stable broad win을 만들지 못했다.
- `XPUB` same first-part retry matching cache도 같은 이유로
  rejected candidate다. default retry path의 repeated rematch를 줄이려는
  방향 자체는 맞지만, current acceptance에서는 `inproc`이 더 나빠졌다.
- same-thread `activate_write` mailbox 정렬도 같은 이유로
  rejected candidate다. mailbox wakeup 채널과 retry wait를 맞추려는
  방향은 타당했지만, current acceptance에서는 broad win을 만들지 못했다.
- `pipe/dist/xpub` single-matching `XPUB_NODROP=1` one-lock helper도
  같은 이유로 rejected candidate다. single steady-state와 broader single은
  일부 회복했지만, current acceptance에서는 multi `pubsub tcp` baseline을
  안정적으로 지키지 못했다.

## 9. 현재 반영된 개선

2026-03-28 기준으로 아래 개선이 들어갔다.

- [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
  에 `PAIR`/`DEALER` public single-part direct recv fast path 추가
- [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
  에 `ROUTER` public single-part direct routed recv fast path 추가
- [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
  에 `SUB/XSUB` topic frame direct recv 경량화 추가
- [`recv_tls_view.hpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_tls_view.hpp)
  에 first-slot reserve/commit helper 추가
- [`message_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/message_api.cpp)
  와 [`recv_tls_view.hpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_tls_view.hpp)
  에 TLS-view `multipart_close` release 최적화 추가
- [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  / [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  에 `public_api_sync` shadow atomic 제거
- [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  에 send-ready handler가 없는 blocking send는 retry 동안
  `public_api_sync`를 유지하도록 조정
- [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp) 의
  `check_read()` / `read()` 에서 steady-state `fast_mutex_t` 제거
- [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp),
  [`pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp),
  [`lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp),
  [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp),
  [`router.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.cpp),
  [`stream.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/stream.cpp)
  에 `write_and_flush()` 경로 추가
- [`pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  / [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  / [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
  에 `PUBSUB` publication path 전용 non-recursive HWM check helper 추가
- [`lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
  에 one-active-pipe `DEALER` send fast path 추가
- [`dist.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.hpp)
  / [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
  에 one-matching-pipe `PUBSUB` send fast path와
  index-stable deactivate helper 추가
- [`xsub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.hpp)
  / [`xsub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.cpp)
  에 empty-subscription accept-all fast path 추가
- [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
  / [`socket_base_dispatch.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_dispatch.cpp)
  / [`spot_sub_recv.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_sub_recv.cpp)
  에 requested-only `last_recv_source_rid` capture scope 추가
- [`test_public_inproc_multipart_send.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_public_inproc_multipart_send.cpp)
  에 `DEALER` single/multipart/concurrent send 회귀 추가
- [`test_multi_socket_contract_regressions.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_multi_socket_contract_regressions.cpp)
  에 same-handle concurrent `PUB` publish 회귀 추가
- [`test_router_mandatory_hwm.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_router_mandatory_hwm.cpp)
  / [`CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/tests/CMakeLists.txt)
  에 `ROUTER` mandatory-HWM 회귀를 ctest surface에 등록하고
  `zlink_send_rid()` 경로까지 포함하도록 확장

의미:

- common `PAIR`/`DEALER` recv가 더 이상 임시 `first` message init/move를
  반드시 거치지 않는다
- single-part는 TLS slot 0으로 바로 recv한 뒤 바로 commit한다
- 이전 multipart 뒤 다음 single-part를 받을 때도 같은 TLS storage를
  안전하게 재사용한다
- caller가 `zlink_multipart_close()`를 이미 호출한 경우, 다음 recv begin에서
  같은 TLS slot을 다시 close/init 하는 낭비를 줄인다
- recv는 공개 thread-safe hot path가 아니므로, recv-side `pipe` steady-state
  잠금은 send-side concurrent contract보다 먼저 줄일 수 있다
- send는 thread-safe 계약을 유지한 채 `write` + `flush`를 같은 pipe lock으로
  묶어, 별도 `write`/`flush` lock pair는 제거했다
- 다만 현재 [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  의 `write()` / `write_and_flush()` / `check_write_status()`는
  `_out_sync`를 잡은 뒤 `check_hwm()`에서 같은 recursive fast mutex를
  다시 잡는다.
- 이건 여전히 plausible cost axis지만, 2026-03-28
  generic `check_hwm_locked()` helper A/B는 `PAIR`/raw guardrail까지 포함한
  broad win을 만들지 못했다.
- 대신 current accepted delta는 `dist_t`가 쓰는 `PUBSUB` publication path에만
  non-recursive HWM check를 좁게 적용한 변형이다.
- 따라서 현재 남은 send-side `pipe` 과제는 "이 self-reentry를 무조건 없애자"가
  아니라, ordering과 HWM semantics를 유지한 채 실제 broad win이 나는 좁은
  work 축소를 pattern별로 찾는 것이다.
- `DEALER_DEALER`처럼 outbound pipe가 하나뿐인 steady-state는
  일반 `lb_t` loop와 fairness bookkeeping을 반복하지 않도록 줄였다
- `PUBSUB`처럼 matching/active pipe가 하나뿐인 steady-state는
  일반 `dist_t` loop와 pipe index lookup을 반복하지 않도록 줄였다
- realigned `PUBSUB` steady-state `SUB/XSUB`는 empty subscription이 살아 있을 때
  trie `match()` 대신 accept-all fast path를 사용한다
- `xsub::xrecv()`는 normal steady-state에서 더 이상 항상
  `last_recv_source_rid` snapshot을 저장하지 않고,
  `spot_sub_t::recv()`처럼 caller가 실제 source-rid를 요청할 때만
  그 비용을 지불한다
- `PAIR`/`DEALER_DEALER` raw/public 분리는 send-side 변경 뒤 매번 다시 찍는
  guardrail이고, 현재도 pattern/transport에 따라 방향이 엇갈린다
- `ROUTER` mandatory-HWM 회귀는 이제 standalone source file만 존재하는 게 아니라
  ctest lane에서 실제 실행되고, prefix-frame 경로와 `zlink_send_rid()` 경로를
  같이 검증한다
- current `PUBSUB` accepted delta는 dist-only non-recursive HWM check와
  one-matching-pipe send fast path 위에
  `xsub` receiver-drain specialization까지 포함한다
- 위 current HEAD 기준 isolated first/rerun은
  `PUBSUB tcp/inproc -9.40% / -20.35%`,
  `-10.43% / -21.59%`였다
- 같은 delta의 broader single은
  `PAIR tcp/inproc -16.64% / -21.71%`,
  `PUBSUB tcp/inproc -11.57% / -20.78%`,
  `DEALER_DEALER tcp/inproc -26.40% / -21.90%`,
  `DEALER_ROUTER tcp/inproc -24.17% / -18.30%`,
  `ROUTER_ROUTER tcp/inproc -55.78% / -21.48%`였다
- multi `pubsub tcp 64B` smoke는 `+9.25%`, rerun `+8.25%`였다

현재 quick 결과 해석:

- `3s quick run` 기준
  - `PAIR tcp 64B`: `libzmq 3908.78 Kmsg/s`, `zlink 3046.03 Kmsg/s`, `-22.07%`
  - `DEALER_DEALER tcp 64B`: `libzmq 3765.72 Kmsg/s`,
    `zlink 3180.02 Kmsg/s`, `-15.55%`
- `2026-03-28 PUBSUB` quick run 기준
  - `PUBSUB tcp 64B`: `-9.40%`
  - `PUBSUB tcp 64B` rerun: `-10.43%`
  - `PUBSUB inproc 64B`: `-20.35%`
  - `PUBSUB inproc 64B` rerun: `-21.59%`
  - multi `pubsub tcp 64B`: `+9.25%`
  - multi `pubsub tcp 64B` rerun: `+8.25%`

따라서 현재 해석은:

- public recv single-part 경로는 실제 병목 축이 맞다
- recv-side `pipe` 잠금도 실제 병목 축이 맞다
- send-side `write + flush` 이중 잠금도 실제 병목 축이 맞다
- `dist_t`의 single-subscriber distributor loop/index work도
  `PUBSUB tcp`에서는 실제 병목 축이 맞다
- `dist_t` publication path의 recursive HWM self-reentry도
  generic rollout은 실패했지만 `PUBSUB` 전용 좁은 적용은
  current code 기준으로 의미 있는 병목 축이다
- `zlink_multipart_close()` 이후 TLS slot release 최적화도 steady-state recv
  비용을 낮추는 데 실제로 기여한다
- `xsub` empty-subscription steady-state와 source-rid snapshot은
  current `PUBSUB` gap의 실제 hot-path 축이었다
- 하지만 남은 전체 gap을 혼자 설명하는 축은 아니다
- current `PUBSUB tcp`와 multi `pubsub`는 크게 줄었지만,
  `PUBSUB inproc`과 `ROUTER_ROUTER` routed path는 여전히 다음 우선순위다
- `PAIR`는 `xsend()`가 `pipe::write_and_flush()`만 타고 별도 `lb/dist`
  상태를 건드리지 않으므로, public send sync를 반드시 같이 잡아야 하는지
  따로 볼 여지가 있다

추가 관찰:

- 현재 환경에서는 `perf`를 사용할 수 없어 `perf stat`으로
  `resource_stalls.sb`, `L1-dcache-load-misses`를 직접 비교하진 못했다.
- 대신 `_out_sync`를 `pipe.cpp` 전체에서 no-op으로 만드는 직접 A/B 실험을
  해봤지만, 오히려 quick throughput이 더 나빠졌다.
  - `PAIR tcp 64B`: 약 `3.046M -> 2.158M msg/s`
  - `DEALER_DEALER tcp 64B`: 약 `3.180M -> 2.468M msg/s`
- 이 결과는 `pipe` lock이 "전부 제거하면 바로 빨라지는 순수 고정비"가 아니라,
  현재 구현에서는 활성화/직렬화/진행 보장과도 얽혀 있다는 뜻이다.
- 따라서 `pipe`를 상위 원인 후보로 유지하더라도,
  naive한 전체 lock 제거는 올바른 해결책이 아니다.

즉 여기서 얻은 가장 중요한 교훈은:

- `lock cost`를 줄이는 것이 목표가 아니라
- hot path가 같은 ordering을 더 적은 work로 달성하게 만드는 것이 목표다.

실험적으로 `PAIR`에서 `socket_base_t::send()`의 public sync를 우회하는
경로를 다시 검토했다.

- 현재는 `PAIR`에만 한정해서 public sync를 우회하고 있다.
- same-handle concurrent send 회귀 테스트는 계속 통과한다.
- 현재 안정 상태 `5s quick run` 기준 `PAIR tcp 64B`는
  `zlink 3.058M msg/s`, `libzmq 3.643M msg/s`로 약 `-16.1%` 차이다.
- 폭이 크진 않아서 과대해석하면 안 되지만,
  `PAIR`에서는 public send sync가 실제 비용 축이라는 근거는 더 강해졌다.
- 반대로 `DEALER`는 `lb_t`의 `_active/_current/_more/_dropping` 상태 때문에
  같은 우회를 바로 적용하면 안 된다.
- 대신 `DEALER`는 "동일 의미를 더 적은 work로 달성"하는 좁은 후보로
  one-active-pipe `lb_t::sendpipe()/has_out()` fast path를 유지한다.
  - 유지 기준 quick 결과:
    - `DEALER_DEALER tcp 64B`
      `3693.91 Kmsg/s` vs `3265.99 Kmsg/s`, `-11.58%`
    - `DEALER_DEALER inproc 64B`
      `4356.51 Kmsg/s` vs `3177.03 Kmsg/s`, `-27.07%`
  - 즉 `tcp`는 크게 회복됐지만 `inproc`과 multi guardrail은 아직 미달이다.

배제된 후보:

- `pipe.cpp` 전체 `_out_sync` no-op
  - `PAIR tcp 64B`: 약 `3.046M -> 2.158M`
  - `DEALER_DEALER tcp 64B`: 약 `3.180M -> 2.468M`
  - 결론: `pipe` lock은 단순 순수 고정비가 아니라 현재 구현의 직렬화 순서와
    얽혀 있어 naive 제거는 역효과
- `pipe::write_and_flush()` / `flush()`에서 peer activation을 lock 밖으로 이동
  - `PAIR tcp 64B`: 약 `3.122M -> 2.970M`
  - 결론: 현재 경로에서는 activation ordering이 성능에도 직접 영향
- `mailbox.cpp`의 recv/check_read 측 read-side lock 제거
  - `PAIR tcp 64B`: 약 `3.058M -> 2.860M`
  - `DEALER_DEALER tcp 64B`: 약 `3.133M -> 2.809M`
  - 결론: 이 경로도 현재 구현과는 독립적인 순수 오버헤드가 아니다
- `fq.cpp` one-active-pipe recv fast path
  - [`perf_linux_20260327_234037_dealer_single_pipe_fastpath_lb_fq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_234037_dealer_single_pipe_fastpath_lb_fq.txt)
  - `DEALER_DEALER tcp 64B`: `-12.98%`
  - `DEALER_DEALER inproc 64B`: `-34.71%`
  - 결론: `DEALER` one-pipe recv는 이번 형태로는 오히려 `inproc`을 악화시켜
    유지 후보가 아니다
- `lb.cpp` one-active-pipe no-recursive HWM helper
  - [`perf_linux_20260328_053159_codex_20260328_dealer_lb_no_recursive.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_053159_codex_20260328_dealer_lb_no_recursive.txt)
  - [`perf_linux_20260328_053222_codex_20260328_dealer_lb_no_recursive_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_053222_codex_20260328_dealer_lb_no_recursive_rerun.txt)
  - [`perf_linux_20260328_053315_codex_20260328_dealer_router_lb_no_recursive_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_053315_codex_20260328_dealer_router_lb_no_recursive_rerun.txt)
  - [`perf_linux_20260328_053439_codex_20260328_lb_no_recursive_guardrail_public_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_053439_codex_20260328_lb_no_recursive_guardrail_public_serial.txt)
  - `DEALER_DEALER` isolated first/rerun `tcp/inproc`: `-14.30% / -33.01%`,
    `-24.41% / -23.27%`
  - `DEALER_ROUTER` isolated first/rerun `tcp/inproc`: `-25.76% / -32.27%`,
    `-19.09% / -25.35%`
  - serial public guardrail `PAIR tcp/inproc`: `-23.95% / -31.30%`
  - 결론: `DEALER` one-pipe path만 좁게 바꿔도 isolated win은 보이지만
    `PAIR` public guardrail을 깨뜨려 broad win이 아니다. current accepted
    `lb_t` one-active-pipe fast path는 유지하되 no-recursive helper는 넣지 않는다.
- `pair.cpp` final-part no-recursive HWM helper
  - [`perf_linux_20260328_054250_codex_20260328_pair_no_recursive_flush.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_054250_codex_20260328_pair_no_recursive_flush.txt)
  - [`perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_rerun.txt)
  - [`perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_guardrail_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_guardrail_public.txt)
  - [`perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_054325_codex_20260328_pair_no_recursive_flush_guardrail_raw.txt)
  - `PAIR` isolated first/rerun `tcp/inproc`: `-8.49% / -18.39%`,
    `-11.64% / -21.18%`
  - serial public guardrail `DEALER_DEALER tcp/inproc`: `-8.22% / -31.36%`
  - serial raw guardrail `DEALER_DEALER tcp/inproc`: `-19.70% / -30.57%`
  - 결론: `PAIR` final-part path만 좁게 바꿔도 isolated `tcp`는 회복했지만
    rerun `inproc`와 `DEALER_DEALER` public/raw guardrail을 동시에 깨뜨렸다.
    current `PAIR` 잔여 gap을 final-part helper 하나로 설명하진 않는다.
- `XPUB` prechecked no-HWM-recheck
  - [`perf_linux_20260328_055013_codex_20260328_pubsub_prechecked_no_hwm_recheck.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_055013_codex_20260328_pubsub_prechecked_no_hwm_recheck.txt)
  - [`perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_rerun.txt)
  - [`perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_public.txt)
  - [`perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_055049_codex_20260328_pubsub_prechecked_no_hwm_recheck_guardrail_raw.txt)
  - `PUBSUB` isolated first/rerun `tcp/inproc`: `-21.70% / -35.47%`,
    `-19.65% / -41.46%`
  - serial public guardrail `PAIR tcp/inproc`: `-13.42% / -17.59%`
  - serial public guardrail `DEALER_DEALER tcp/inproc`: `-18.56% / -19.78%`
  - 결론: nodrop precheck 뒤 second HWM check를 줄이면 first run은 좋아 보였지만
    clean rerun `PUBSUB inproc`가 accepted baseline보다 다시 나빠졌다.
    current `PUBSUB` 잔여 gap을 precheck/HWM recheck 하나로 설명하진 않는다.
- `object.cpp` same-thread `send_activate_read()` direct delivery
  - generic 적용:
    - [`perf_linux_20260327_235547_pair_activate_read_direct.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_235547_pair_activate_read_direct.txt)
    - [`perf_linux_20260327_235621_dealer_activate_read_direct.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_235621_dealer_activate_read_direct.txt)
    - `PAIR inproc`은 `-23.82%`까지 회복됐지만
      `DEALER_DEALER tcp`가 `-25.06%`로 악화됐다
  - `PAIR` no-handler 전용 gate:
    - [`perf_linux_20260328_000053_pair_inline_activate_read_pair_only.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_000053_pair_inline_activate_read_pair_only.txt)
    - `PAIR tcp 64B`: `-26.32%`
    - `PAIR inproc 64B`: `-32.96%`
  - `dist.cpp` final-part same-thread inline wakeup:
    - [`perf_linux_20260328_064859_codex_20260328_pubsub_dist_same_thread_activate_read.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_064859_codex_20260328_pubsub_dist_same_thread_activate_read.txt)
    - `PUBSUB tcp/inproc 64B`: `-25.70% / -42.49%`
  - 결론: 현재 `activate_read` publication은 direct delivery로 줄일
    순수 mailbox overhead가 아니다. progress ordering / callback / engine
    wakeup과 얽혀 있어 기본 후보에서 제외한다.
- `socket_message_send_api.cpp` single-part public fast path의
  wrapper-side `msg->check()` 제거
  - [`perf_linux_20260328_000714_pair_singlepart_public_check_elision.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_000714_pair_singlepart_public_check_elision.txt)
  - [`perf_linux_20260328_000746_dealer_singlepart_public_check_elision.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_000746_dealer_singlepart_public_check_elision.txt)
  - `PAIR inproc`은 `-21.42%`까지 회복됐지만
    `DEALER_DEALER inproc`이 `-31.51%`로 악화됐다
  - 결론: 현재 public single-part send wrapper의 중복 `msg->check()`는
    broad guardrail을 깨뜨릴 만큼 상위 원인이 아니다. 이 축은
    유지 후보에서 제외한다.
- `socket_base_msg.cpp` blocking retry의 idle send-ready handler sync 유지
  - [`perf_linux_20260328_021040_codex_idle_send_ready_retry_public_seq_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_021040_codex_idle_send_ready_retry_public_seq_serial.txt)
  - [`perf_linux_20260328_021124_codex_idle_send_ready_retry_raw_seq_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_021124_codex_idle_send_ready_retry_raw_seq_serial.txt)
  - `DEALER_DEALER tcp` public은 `-9.81%`까지 회복됐지만
    `PAIR inproc` public이 `-24.33%`,
    `DEALER_DEALER tcp/inproc` raw가 `-27.29% / -24.59%`로 흔들렸다
  - 결론: blocking retry에서 "handler installed"와 "notification armed"를
    구분하는 발상은 타당했지만, installed-but-idle handler를 모두 sync-held
    쪽으로 보내는 현재 형태는 broad win이 아니다
- `socket_message_send_api.cpp` no-topic single-part `PUBSUB` public fast path
  - [`perf_linux_20260328_030104_pubsub_no_topic_singlepart_fastpath_isolated.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_030104_pubsub_no_topic_singlepart_fastpath_isolated.txt)
  - `PUBSUB tcp/inproc 64B`: `-32.84% / -45.80%`
  - 결론: aligned no-topic single surface에서 generic multipart wrapper를
    바로 우회하는 현재 형태는 오히려 broad regression이라 rejected candidate로 둔다
- `socket_message_recv_api.cpp` `SUB/XSUB` raw multipart single-part recv fast path
  - [`perf_linux_20260328_030652_pubsub_sub_raw_multipart_recv_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_030652_pubsub_sub_raw_multipart_recv_fastpath.txt)
  - [`perf_linux_20260328_030723_pubsub_sub_raw_multipart_recv_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_030723_pubsub_sub_raw_multipart_recv_fastpath_rerun.txt)
  - first/rerun `PUBSUB tcp 64B`: `-30.67% / -26.74%`
  - first/rerun `PUBSUB inproc 64B`: `-41.47% / -50.68%`
  - 결론: raw recv single-part export를 더 직접화해도 방향이 엇갈려
    current `PUBSUB` 잔여 gap의 broad answer는 아니었다
- `xpub.cpp` no-monitor delivery-ready tracking gate
  - [`perf_linux_20260328_052420_codex_20260328_pubsub_monitor_tracking_gate.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_052420_codex_20260328_pubsub_monitor_tracking_gate.txt)
  - [`perf_linux_20260328_052446_codex_20260328_pubsub_monitor_tracking_gate_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_052446_codex_20260328_pubsub_monitor_tracking_gate_rerun.txt)
  - first/rerun `PUBSUB tcp/inproc 64B`: `-26.72% / -37.92%`,
    `-27.12% / -43.79%`
  - 결론: monitor가 없는 steady-state에서 delivery-ready recompute를
    통째로 건너뛰고 monitor open 시 count를 priming해도 accepted baseline보다
    나빠졌다. current `PUBSUB` gap은 no-monitor ready bookkeeping 하나로
    설명되지 않는다.
- `xpub/xsub` delivery-ready lazy tracking + reopen snapshot regression
  - [`perf_linux_20260328_111321_codex_20260328_pubsub_delivery_ready_lazy_default.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_111321_codex_20260328_pubsub_delivery_ready_lazy_default.txt)
  - [`perf_linux_20260328_111416_codex_20260328_delivery_ready_lazy_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_111416_codex_20260328_delivery_ready_lazy_broader_single.txt)
  - [`perf_linux_20260328_111611_codex_20260328_pubsub_delivery_ready_lazy_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_111611_codex_20260328_pubsub_delivery_ready_lazy_rerun.txt)
  - isolated first `PUBSUB tcp/inproc 64B`: `-14.47% / -23.50%`
  - isolated rerun `PUBSUB tcp/inproc 64B`: `-14.44% / -29.17%`
  - broader single `PUBSUB tcp/inproc 64B`: `-15.52% / -28.17%`
  - broader single `ROUTER_ROUTER tcp/inproc 64B`: `-57.21% / -28.95%`
  - multi `pubsub tcp 64B`: `-20.48%`
  - 결론: late monitor reopen / snapshot correctness는
    [`test_monitor_socket_contract.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_socket_contract.cpp)
    의 `test_pubsub_delivery_ready_snapshot_and_reopen_after_ready()`로
    고정할 가치가 있었지만, ready-count recompute를 monitor 구독 시에만
    lazy하게 수행하는 candidate 자체는 `tcp` isolated improvement만 보이고
    `inproc` rerun, broader single, multi guardrail을 동시에 지키지 못했다.
    current `PUBSUB` 잔여 gap은 delivery-ready tracking lazy gate 하나로
    설명되지 않는다.
- `pipe::process_activate_write()` already-active fast path
  - [`perf_linux_20260328_112849_codex_20260328_pubsub_activate_write_active_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_112849_codex_20260328_pubsub_activate_write_active_fastpath.txt)
  - [`perf_linux_20260328_112920_codex_20260328_pubsub_activate_write_active_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_112920_codex_20260328_pubsub_activate_write_active_fastpath_rerun.txt)
  - first `PUBSUB tcp/inproc 64B`: `-9.53% / -28.30%`
  - rerun `PUBSUB tcp/inproc 64B`: `-22.77% / -24.83%`
  - 결론: `activate_write` consumer에서 already-active steady-state lock만
    건너뛰어도 libzmq와의 차이가 줄어들 것 같았지만, 실제로는 `tcp`와
    `inproc`가 first/rerun에서 서로 엇갈렸다. current `PUBSUB` 잔여 gap을
    activation command consumer micro helper 하나로 설명하진 않는다.
- `pipe::read()` peer read-progress direct publish
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -V -R '^test_xpub_nodrop$' -j1`
  - candidate 적용 상태에서
    `test_xpub_nodrop`가
    `/home/hep7/project/kairos/zlink/core/tests/integration/test_xpub_nodrop.cpp:383:test:PASS`
    뒤 `***Timeout 10.01 sec`로 hang했고, 원복 후 동일 묶음이 다시 통과했다
  - 결론: `_lwm` boundary에서 peer `_peers_msgs_read`를 직접 publish해
    `activate_write` command를 우회하는 발상은 receiver-drain differential에
    닿아 있었지만, 실제 current code에서는 teardown invariant를 깨뜨렸다.
    current `PUBSUB` 잔여 gap은 command publication shortcut 하나로
    설명되지 않는다.
- `xsub::xrecv()` last-recv source-rid snapshot 제거
  - [`perf_linux_20260328_114210_codex_20260328_pubsub_xsub_skip_last_recv_source_rid.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_114210_codex_20260328_pubsub_xsub_skip_last_recv_source_rid.txt)
  - [`perf_linux_20260328_114239_codex_20260328_pubsub_xsub_skip_last_recv_source_rid_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_114239_codex_20260328_pubsub_xsub_skip_last_recv_source_rid_rerun.txt)
  - first/rerun `PUBSUB tcp/inproc 64B`: `-20.13% / -28.12%`,
    `-19.83% / -23.68%`
  - 결론: `xsub` normal recv에서 routed-source snapshot을 걷자
    current recheck baseline `-22.44% / -31.08%` 대비
    `tcp/inproc` 모두 개선 신호가 있었다.
    다만 broader single / multi acceptance와 `spot` source-rid contract를
    함께 확인하지 못해 이 형태 단독으로는 retained delta가 되지 못했다.
- `xsub` empty-subscription accept-all fast path
  - [`perf_linux_20260328_114624_codex_20260328_pubsub_xsub_accept_all.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_114624_codex_20260328_pubsub_xsub_accept_all.txt)
  - [`perf_linux_20260328_114646_codex_20260328_pubsub_xsub_accept_all_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_114646_codex_20260328_pubsub_xsub_accept_all_rerun.txt)
  - first/rerun `PUBSUB tcp/inproc 64B`: `-16.33% / -20.00%`,
    `-15.20% / -27.16%`
  - 결론: empty subscription steady-state의 trie `match()`를 걷자
    first run은 current `PUBSUB` receiver-drain 후보 중 가장 좋은 신호를
    보였고, current recheck baseline `-22.44% / -31.08%` 대비
    rerun도 `tcp/inproc` 모두 회복했다.
    다만 broader single / multi guardrail과 contract-safe integration을
    함께 확인하기 전에는 단독 retained delta로 삼지 않았다.
- retained `xsub` receiver-drain specialization
  - [`perf_linux_20260328_123151_codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_123151_codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand.txt)
  - [`perf_linux_20260328_123215_codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_123215_codex_20260328_pubsub_xsub_accept_all_rid_store_on_demand_rerun.txt)
  - [`perf_linux_20260328_123242_codex_20260328_xsub_accept_all_rid_store_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_123242_codex_20260328_xsub_accept_all_rid_store_broader_single.txt)
  - isolated first/rerun `PUBSUB tcp/inproc 64B`: `-9.40% / -20.35%`,
    `-10.43% / -21.59%`
  - broader single `PAIR/PUBSUB/DEALER_DEALER/DEALER_ROUTER/ROUTER_ROUTER`
    `tcp/inproc 64B`:
    `-16.64% / -21.71%`, `-11.57% / -20.78%`,
    `-26.40% / -21.90%`, `-24.17% / -18.30%`,
    `-55.78% / -21.48%`
  - multi `pubsub tcp 64B` stdout first/rerun: `+9.25%`, `+8.25%`
  - 결론: empty-subscription steady-state trie match 제거와
    requested-only source-rid capture를 함께 묶어야
    current `spot`/public contract를 유지하면서 receiver-drain broad win이 된다.
    이 조합은 current `PUBSUB tcp`와 multi를 크게 줄였으므로 retained delta다.
- current accepted `dist` helper 위 `XPUB` all-attached empty-prefix
  `send_to_all()` v2
  - [`perf_linux_20260328_060304_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq1.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_060304_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq1.txt)
  - [`perf_linux_20260328_060332_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq2.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_060332_codex_20260328_xpub_empty_prefix_send_all_fastpath_v2_seq2.txt)
  - sequential seq1 `PUBSUB tcp/inproc 64B`: `-25.77% / -40.89%`
  - sequential seq2 `PUBSUB tcp/inproc 64B`: `-23.12% / -40.39%`
  - 결론: accepted `dist` helper 위에 다시 얹어도
    empty-prefix match elimination은 stable broad win이 아니다.
    `tcp`는 들쭉날쭉하고 `inproc`는 seq1/seq2 모두 accepted baseline보다
    더 나빴다. 따라서 current `PUBSUB` 잔여 gap을
    all-attached empty-prefix `send_to_all()` 계열로 다시 설명하진 않는다.
- `ROUTER` blocking envelope / `zlink_send_rid()` multipart routed-data view
  - [`perf_linux_20260328_062033_codex_20260328_router_routed_data_view.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_062033_codex_20260328_router_routed_data_view.txt)
  - [`perf_linux_20260328_062105_codex_20260328_router_routed_data_view_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_062105_codex_20260328_router_routed_data_view_rerun.txt)
  - first/rerun `ROUTER_ROUTER tcp/inproc 64B`: `-58.62% / -30.04%`,
    `-55.12% / -29.06%`
  - 결론: routing-id frame copy/elision과 multipart first-payload direct routed
    transaction을 묶어도 `tcp` broad win은 나오지 않았다.
    current `ROUTER` 잔여 gap을 send-side routed-data view 하나로
    설명하진 않는다.
- current code에는
  [`test_public_inproc_multipart_send.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_public_inproc_multipart_send.cpp)
  의 `test_public_inproc_router_send_rid_multipart_blocking()`만 retained
  regression으로 남겨 `zlink_send_rid()` multipart blocking contract를 잡는다.

이 순서는 thread-safe 계약을 유지하면서도 실제 `single` gap에 직접 닿는
순서다.
