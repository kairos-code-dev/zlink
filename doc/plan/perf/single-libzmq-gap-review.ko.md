# `single` 상대 성능 격차 재분석 메모

> 이 문서는
> [`single-libzmq-gap-ralph-guide.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-ralph-guide.ko.md)
> 가 참조하는 지속 로그 파일이다.
>
> 랄프루프를 돌리는 동안 이 파일에는 최소한 아래를 계속 갱신한다.
>
> - 최신 가설과 배제된 가설
> - 실제 실행 명령
> - 결과 파일 경로
> - 현재 best / latest throughput 비교
> - 다음 iteration 우선순위

> 범위: [`core/bench/with_zmq/single/`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single)
> 기준의 `zlink` 대 `libzmq` 상대 성능 차이
>
> 목적: 현재 남아 있는 격차가 `core` 엔진 자체 회귀인지,
> 아니면 benchmark surface 차이와 public API 비용이 섞인 결과인지
> 현재 코드 기준으로 다시 정리한다.

## 1. 현재 결론

현재 `with_zmq single` 격차는 "libzmq와 같은 수준의 raw message path"를
비교한 결과가 아니다. 다만 raw/public 분리는 최근 rerun에서도 패턴과
transport에 따라 다시 엇갈렸으므로, 더 이상 "`public wrapper penalty는 이미
항상 작다`"는 식의 고정 전제로 읽으면 안 된다.

현재 HEAD 기준에서는 raw/public을 iteration별 guardrail로 다시 찍으면서,
남은 본체를 send-side core engine differential과 public penalty 변화로 함께
읽는 것이 가장 맞다.

핵심은 다음 네 가지다.

- `3/23 -> 3/27` POSD 직접 회귀는 대부분 회복됐다.
- 현재 `with_zmq single`은 `zlink`와 `libzmq`가 같은 API surface를 타지 않는다.
- 현재 `zlink`의 `pipe_t`는 steady-state `read/write/flush`에서
  `fast_mutex_t`를 잡지만, libzmq `pipe_t`는 같은 잠금이 없다.
- 다만 raw/public 분리 결과는 send-side 변경에 따라 다시 벌어질 수 있다.
  즉 이 값은 "한 번 측정해서 끝나는 결론"이 아니라 매 send-path 변경 뒤
  다시 확인해야 하는 guardrail이다.
- 따라서 지금 보이는 큰 격차는 public wrapper 하나로 설명하면 안 되고,
  현재는 send-side core differential이 더 상위 본체다.
- 다만 2026-03-27에 넣은 `PAIR`/`DEALER` public recv direct fast path로
  `PAIR` 수치가 바로 반응한 것을 보면, recv public surface는 실제 원인 축이
  맞다. 다만 그것만으로 전체 gap이 닫히지는 않는다.
- 최근 `echo`는 거의 동등하고 `oneway`에서 gap이 더 크게 보이는 점은,
  남은 본체가 recv보다 send-side publication/backpressure 쪽이라는 해석과
  맞는다.
- 2026-03-28 `dist_t` one-matching-pipe fast path가
  `PUBSUB tcp 64B`를 `-37.57%` 근처에서 `-24.23%`, rerun `-26.00%`까지
  회복시켰다. 즉 distributor loop/index/deactivate work는 실제 hot-path
  비용 축이다.
- 다만 같은 라운드의 `PUBSUB inproc 64B`는 `-42.51%`,
  multi `pubsub tcp 64B`는 `-26.97%`여서 publication/lifecycle differential을
  전부 설명하진 못한다.

즉 지금 결과는 더 정확히 말하면:

- `zlink`의 현재 public API path
- 대
- `libzmq`의 더 얇은 raw message path

를 비교한 결과에 가깝다.

## 1.1 `echo` 대비 `oneway` 해석

최근 bench/perf를 함께 읽으면 `echo`에서는 zlink가 거의 동등하거나 차이가
작고, `oneway`에서는 throughput gap이 더 크게 드러난다.

이 패턴은 다음 해석과 맞는다.

- `echo`는 round-trip 특성상 sender가 자연스럽게 pace된다.
- 그래서 queue가 깊게 차거나 HWM/EAGAIN 복귀 경로가 오래 노출되지 않는다.
- 반면 `oneway`는 sender가 계속 밀어 넣기 때문에
  - `socket_base_t::send()` success path
  - HWM 도달 이후 backpressure 복귀
  - `pipe` publication / flush / activation
  - `PUBSUB`이면 distributor fanout
  의 비용이 더 직접적으로 드러난다.

즉 현재 남은 gap은 recv가 완전히 무관하다는 뜻은 아니지만,
우선순위는 recv보다 send-side ordering/publication/backpressure 쪽에 더 가깝다.

같은 맥락에서 `XPUB/XSUB`의 delivery-ready monitor bookkeeping을
steady-state hot path에서 건너뛰도록 줄여봤지만, `PUBSUB 64B` single bench는
유의미하게 움직이지 않았다. 따라서 현재 `PUBSUB` gap의 상위 축은
monitor-ready 유지 비용보다 publication/ordering 쪽으로 본다.

여기에 2026-03-28 `dist_t` one-matching-pipe fast path 결과를 합치면,
현재 `PUBSUB` 해석은 더 좁혀진다.

- monitor-ready bookkeeping은 secondary다.
- 반면 single-subscriber steady-state의 distributor loop/index/deactivate
  work는 실제 비용 축이다.
- 그런데 `inproc`와 multi가 여전히 크게 남으므로, publication/backpressure와
  pattern-specific 잔여 비용이 함께 남아 있다고 본다.

### 1.2 backpressure / wakeup 경로에 대한 현재 판정

`echo`와 `oneway` 차이를 더 좁히기 위해 `HWM 도달 -> activate_write ->
sender 복귀` 경로를 zlink/libzmq 코드 기준으로 다시 대조했다.

현재 확정 가능한 점은 다음과 같다.

- `pipe_t::compute_lwm()`는 zlink와 libzmq가 동일하다.
  - 둘 다 `LWM = (HWM + 1) / 2`
- `pipe_t::read()`에서 `_msgs_read % _lwm == 0`일 때
  `send_activate_write(_peer, _msgs_read)`를 보내는 구조도 동일하다.
- zlink의 [`object_t::send_activate_write()`](/home/hep7/project/kairos/zlink/core/src/core/object.cpp)
  는 같은 thread일 때 `destination_->process_command(cmd)`를 직접 호출한다.
  libzmq는 같은 경우에도 mailbox command를 보낸다.
  즉 "activate_write publication 자체가 zlink에서 더 늦다"는 가설은 현재
  코드만 보면 오히려 약하다.
- `pipe_t::process_activate_write()`도 두 구현 모두
  - `_peers_msgs_read` 갱신
  - `_out_active = true`
  - `write_activated()` 통지
  흐름은 동일하다.

즉 현재 더 유력한 차이는 wakeup publication보다
"sender가 wakeup을 소비하고 다시 send를 시도하는 비용" 쪽이다.

특히 중요한 차이는 여기다.

- libzmq [`socket_base_t::send()`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)는
  함수 시작에서 `scoped_optional_lock_t`를 한 번 잡고,
  blocking retry loop 안에서는 `process_commands(timeout, false)` 뒤
  바로 `xsend()`를 다시 시도한다.
- zlink [`socket_base_t::send()`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)는
  blocking retry loop에서 매번 `public_api_sync`를 다시 잡고 푼다.
  현재 코드는 `send_scope.relock_sync() -> xsend() -> unlock_sync()`
  경로를 반복한다.
- 2026-03-27 현재 HEAD는 send-ready handler가 없는 blocking send에 한해
  retry 동안 `public_api_sync`를 유지하도록 조정됐다.
  이 변경 뒤 `PAIR tcp 64B` public throughput이
  `2.774M -> 3.238M msg/s`로 올라갔다.

따라서 현재 backpressure 해석은 이렇게 고정한다.

- 1차 후보는 "wakeup이 너무 늦게 올라온다"가 아니다.
- 1차 후보는 "wakeup 이후 sender 복귀 경로가 libzmq보다 비싸다"이다.
- 특히 `public_api_sync` 재획득과 send-side lifecycle/admission 구조가
  backpressure 시나리오에서 더 직접적인 cost axis다.

## 2. 데이터 해석

### 2.1 `3/23 pre-POSD` 대비 `3/27 current perf`

`core/perf` 기준으로는 POSD 이후 직접 추가된 병목 대부분이 이미 정리된 쪽이다.

예시:

- `single callback PAIR tcp 64B`
  - `2026-03-23`: `2919.29 Kmsg/s`
  - `2026-03-27`: `3090.08 Kmsg/s`
- `multi recv DEALER_DEALER tcp 64B`
  - `2026-03-23`: `1496.697 Kmsg/s`
  - `2026-03-27`: `1565.549 Kmsg/s`
- `multi recv ROUTER_ROUTER tcp 64B`
  - `2026-03-23`: `897.329 Kops/s`
  - `2026-03-27`: `902.288 Kops/s`

이건 multipart heap materialization 문제와 POSD 직접 병목은 대부분 닫혔다는
해석과 맞는다.

### 2.2 `3/05` baseline은 현재와 surface가 달랐다

오래된 `with_zmq single` baseline:

- [`perf_linux_20260305_204428.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260305_204428.txt)

예시:

- `PAIR tcp 64B`
  - `libzmq 2610.75`
  - `zlink 3632.43`
  - `zlink +39.13%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 2566.54`
  - `zlink 3634.35`
  - `zlink +41.61%`

이 시점의 bench source를 보면 `zlink`와 `libzmq` 모두
`send_exact()` + `zlink_msg_recv()` 중심의 얇은 raw message path를 탔다.

즉 `3/05` baseline은 현재 benchmark와 측정 surface 자체가 다르다.

이 차이는 실제 source diff로도 확인된다.

- `3/05` 근처 [`bench_zlink_pair.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pair.cpp)
  는 `send_exact()` + `zlink_msg_recv()`를 사용했다.
- 현재 같은 파일은 `zlink_send()` + `zlink_recv()`를 사용한다.
- 반면 현재 [`bench_zmq_pair.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zmq/bench_zmq_pair.cpp)
  는 여전히 `send_exact()` + `zlink_msg_recv()`를 사용한다.

즉 `zlink` 쪽만 더 높은 public surface로 옮겨 갔고, `libzmq` 쪽은 raw path를
유지하고 있다.

### 2.3 현재 quick 수치

현재 워크스페이스 기준 quick 결과:

- `PAIR`: `libzmq 4,354,295 msg/s` vs `zlink 2,615,843 msg/s`, `-39.93%`
- `DEALER_DEALER`: `libzmq 3,725,870 msg/s` vs `zlink 2,823,829 msg/s`, `-24.21%`

이 숫자는 gap이 크다는 사실은 보여주지만, 그 차이를 전부 `core` 엔진 회귀로
읽으면 안 된다. 현재 측정 surface가 다르기 때문이다.

### 2.4 current recv fast path 적용 후 quick 수치

2026-03-27에 `PAIR`/`DEALER` public `zlink_recv()` single-part fast path를
조정한 뒤 다시 `1s quick run`으로 찍은 결과:

- `PAIR tcp 64B`
  - `libzmq 3,452.55 Kmsg/s`
  - `zlink 3,012.40 Kmsg/s`
  - `-12.75%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3,537.54 Kmsg/s`
  - `zlink 2,760.03 Kmsg/s`
  - `-21.98%`

해석:

- `PAIR`는 public recv single-part 경량화에 즉시 반응했다.
- `DEALER_DEALER`는 같은 quick run 기준으로 개선이 뚜렷하지 않았다.
- 따라서 현재 남은 격차는 `public recv` 한 축만이 아니라,
  `pipe` steady-state 잠금과 `send` public lifecycle/backpressure 비용이
  함께 남아 있다고 보는 것이 맞다.

### 2.5 current recv-side `pipe` lock 축소 후 3s quick 수치

같은 날 `pipe_t::check_read()` / `read()` 의 steady-state `fast_mutex_t`를
걷고 다시 `3s quick run`으로 찍은 결과:

- `PAIR tcp 64B`
  - `libzmq 3,757.78 Kmsg/s`
  - `zlink 3,025.02 Kmsg/s`
  - `-19.50%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3,751.27 Kmsg/s`
  - `zlink 2,661.38 Kmsg/s`
  - `-29.05%`

해석:

- recv-side public/API 경량화와 recv-side `pipe` lock 축소는 `PAIR`에 직접
  반응한다.
- 하지만 `DEALER_DEALER`는 여전히 큰 gap이 남는다.
- 따라서 현재 남은 상위 원인은
  - send-side `pipe`/lifecycle 고정비
  - public send surface
  - public recv의 남은 routed/aggregate 경로
  로 보는 것이 맞다.

### 2.6 current send-side `write+flush` 병합 후 3s quick 수치

같은 날 send-side final part 경로에서 `pipe::write()` 와 `pipe::flush()`의
별도 lock을 하나의 `write_and_flush()`로 묶은 뒤 다시 `3s quick run`으로
찍은 결과:

- `PAIR tcp 64B`
  - `libzmq 3,646.58 Kmsg/s`
  - `zlink 3,010.85 Kmsg/s`
  - `-17.43%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3,783.27 Kmsg/s`
  - `zlink 2,957.39 Kmsg/s`
  - `-21.83%`

해석:

- `DEALER_DEALER`는 send-side lock 축소에 뚜렷하게 반응했다.
- `PAIR`도 여전히 `3.0M msg/s` 수준을 유지하며, 남은 gap은
  `socket_base_t::send()` lifecycle/public lock과 current benchmark surface
  mismatch 쪽으로 더 좁혀진다.

### 2.7 current TLS-view release 최적화 후 3s quick 수치

같은 날 `zlink_multipart_close()`가 TLS recv view를 바로 release하도록 정리한 뒤
다시 `3s quick run`으로 찍은 결과:

- `PAIR tcp 64B`
  - `libzmq 3,908.78 Kmsg/s`
  - `zlink 3,046.03 Kmsg/s`
  - `-22.07%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3,765.72 Kmsg/s`
  - `zlink 3,180.02 Kmsg/s`
  - `-15.55%`

해석:

- `zlink_recv()` single-part direct path와 TLS-view release 최적화는
  `DEALER_DEALER` 쪽에서도 의미 있는 회복을 만든다.
- `PAIR`는 여전히 `3.0M msg/s` 수준이지만, `libzmq`와의 남은 격차는
  recv 한 축만으로 설명하기 어려운 수준이다.
- 따라서 현재 남은 상위 원인은
  - send-side public lifecycle/backpressure 고정비
  - current benchmark surface mismatch
  - send-side 남은 `pipe` serialization cost
  로 보는 것이 맞다.

### 2.8 current `PAIR` public send sync 축소 후 5s quick 수치

`PAIR`는 `xsend()`가 `pipe::write_and_flush()`만 타고
`lb_t`/`dist_t` 같은 추가 per-socket send 상태를 건드리지 않는다.
그래서 현재는 `PAIR`에 한정해 `socket_base_t::send()`의
public sync를 우회하는 실험을 유지하고 다시 `5s quick run`으로 확인했다.

- `PAIR tcp 64B`
  - `libzmq 3,643.16 Kmsg/s`
  - `zlink 3,058.49 Kmsg/s`
  - `-16.05%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 4,506.82 Kmsg/s`
  - `zlink 3,133.27 Kmsg/s`
  - `-30.48%`

해석:

- `PAIR`에서는 public send sync가 실제 hot-path 비용 축이다.
- 다만 개선 폭은 중간 수준이고, 이 한 축만으로 전체 gap이 설명되진 않는다.
- `DEALER`는 `lb_t`의 mutable send state 때문에 같은 우회를 바로 적용하면
  안 된다.

### 2.9 최근 배제 실험과 그 의미

현재 환경에서는 `perf`가 없어 원인 후보를 직접 A/B로 배제하는 방식이
중요했다. 최근 quick run 기준으로 아래 후보는 모두 배제했다.

- `pipe.cpp` 전체 `_out_sync` no-op
  - `PAIR`: 약 `3.046M -> 2.158M`
  - `DEALER_DEALER`: 약 `3.180M -> 2.468M`
- `pipe::write_and_flush()` / `flush()`의 peer activation을 lock 밖으로 이동
  - `PAIR`: 약 `3.122M -> 2.970M`
- `mailbox.cpp` recv/check_read 측 read lock 제거
  - `PAIR`: 약 `3.058M -> 2.860M`
  - `DEALER_DEALER`: 약 `3.133M -> 2.809M`

이 절은 현재 문서에서 가장 중요한 발견 중 하나다.

의미는 분명하다.

- 현재 남은 gap은 `lock이 있으니 없애면 빨라진다` 식의 단순 문제가 아니다.
- 적어도 지금 구현에서는 `pipe`, `activation`, `mailbox`가 순수 고정비만
  제공하는 구조가 아니며, ordering, activation, progress 보장과 얽혀 있다.
- 따라서 다음 개선 방향은
  - 같은 의미를 더 저렴하게 제공하거나
  - hot path가 그 의미를 덜 자주 필요하게 만들거나
  - 불필요한 work 자체를 줄이는 쪽
  이어야 한다.
- 즉 `lock 제거` 자체는 목표가 아니라, 현재 구조에서 어떤 ordering이 정말
  필요한지 먼저 분리하는 것이 목표다.

### 2.10 특정 시점의 raw/public 분리 결과

`PAIR`/`DEALER_DEALER`에서 `zlink_send/zlink_recv` 기준과
`zlink_msg_send/zlink_msg_recv` 기준을 같은 zlink bench surface에서 다시 찍었다.

직접 측정:

- `PAIR tcp 64B`
  - public: `3,207,629.67 msg/s`
  - raw: `3,104,833.33 msg/s`
  - raw가 약 `-3.20%`
- `PAIR inproc 64B`
  - public: `3,459,143.00 msg/s`
  - raw: `3,511,405.00 msg/s`
  - raw가 약 `+1.51%`
- `DEALER_DEALER tcp 64B`
  - public: `3,208,134.67 msg/s`
  - raw: `3,159,694.67 msg/s`
  - raw가 약 `-1.51%`
- `DEALER_DEALER inproc 64B`
  - public: `3,161,425.00 msg/s`
  - raw: `3,345,499.33 msg/s`
  - raw가 약 `+5.82%`

의미:

- 이 시점의 HEAD 기준으로는 `PAIR`/`DEALER_DEALER`의 zlink 내부 public
  wrapper penalty가 low single-digit 수준이었다.
- 따라서 현재 남은 `libzmq` 대비 큰 상대 gap의 상위 축은
  `zlink_send()/pipe/lifecycle` 쪽 steady-state work로 봐야 한다.
- `public API를 더 얇게 만드는 것`은 여전히 의미가 있지만, 이제는 상위 본체가
  아니라 보조 축이다.

즉 현재 기준으로 gap을 나누면 대략 이렇게 읽는 것이 맞다.

- `core engine gap`
  - `libzmq raw` 대 `zlink raw`
- `surface penalty`
  - `zlink public` 대 `zlink raw`

즉 이 측정은 "public penalty가 항상 작다"는 영구 결론이 아니라,
그 시점의 HEAD에서는 send-side core differential이 더 상위였다는 뜻으로 읽어야
한다.

### 2.10b 1~5 적용 후 public/raw 재측정

2026-03-27에 `1~5` 개선 축을 실제 코드에 반영한 뒤 다시 측정했다.

public 결과:

- [`perf_linux_20260327_210022_items1to5_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_210022_items1to5_public.txt)
- `PAIR tcp 64B`
  - `libzmq 3644.61 Kmsg/s`
  - `zlink 2773.65 Kmsg/s`
  - `-23.90%`
- `PAIR inproc 64B`
  - `libzmq 4205.86 Kmsg/s`
  - `zlink 2825.57 Kmsg/s`
  - `-32.82%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3672.56 Kmsg/s`
  - `zlink 3138.98 Kmsg/s`
  - `-14.53%`
- `DEALER_DEALER inproc 64B`
  - `libzmq 4168.25 Kmsg/s`
  - `zlink 2807.03 Kmsg/s`
  - `-32.66%`
- `DEALER_ROUTER tcp 64B`
  - `libzmq 4551.35 Kmsg/s`
  - `zlink 2782.01 Kmsg/s`
  - `-38.88%`
- `DEALER_ROUTER inproc 64B`
  - `libzmq 4521.56 Kmsg/s`
  - `zlink 2753.86 Kmsg/s`
  - `-39.09%`
- `ROUTER_ROUTER tcp 64B`
  - `libzmq 2986.98 Kmsg/s`
  - `zlink 1241.39 Kmsg/s`
  - `-58.44%`
- `ROUTER_ROUTER inproc 64B`
  - `libzmq 3473.86 Kmsg/s`
  - `zlink 2459.15 Kmsg/s`
  - `-29.21%`
- `PUBSUB tcp 64B`
  - `libzmq 3908.25 Kmsg/s`
  - `zlink 2254.26 Kmsg/s`
  - `-42.32%`
- `PUBSUB inproc 64B`
  - `libzmq 4032.53 Kmsg/s`
  - `zlink 2033.78 Kmsg/s`
  - `-49.57%`

raw 결과:

- [`perf_linux_20260327_210223_items1to5_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_210223_items1to5_raw.txt)
- `PAIR tcp 64B`
  - public `2773.65 Kmsg/s`
  - raw `3354.39 Kmsg/s`
  - raw가 약 `+20.94%`
- `PAIR inproc 64B`
  - public `2825.57 Kmsg/s`
  - raw `3299.18 Kmsg/s`
  - raw가 약 `+16.76%`
- `DEALER_DEALER tcp 64B`
  - public `3138.98 Kmsg/s`
  - raw `2810.42 Kmsg/s`
  - raw가 약 `-10.47%`
- `DEALER_DEALER inproc 64B`
  - public `2807.03 Kmsg/s`
  - raw `3366.84 Kmsg/s`
  - raw가 약 `+19.94%`

의미:

- `1~5` 적용은 `DEALER_DEALER tcp`에는 실제 회복을 만들었다.
  - 직전 public 기준 `2973.05 -> 3138.98 Kmsg/s`
- 반면 `PAIR`, `DEALER_ROUTER`, `PUBSUB`, `ROUTER_ROUTER`는 이번 라운드에서
  뚜렷한 회복을 만들지 못했다.
- 가장 중요한 건 raw/public 분리가 다시 벌어졌다는 점이다.
  - 특히 `PAIR tcp/inproc`는 이번 상태에서 public penalty가 다시 두드러진다.
- 따라서 `2.10`의 low single-digit 결과를 현재 HEAD의 고정 결론으로
  들고 가면 안 된다.
- 현재 해석은 이렇게 고정한다.
  - send-side core differential이 상위 본체인 것은 맞다.
  - 하지만 send-side lifecycle 변경은 public penalty도 다시 키울 수 있으므로
    raw/public 분리를 매 라운드 guardrail로 다시 찍어야 한다.

### 2.11 zlink 절대 throughput 추이

quick run의 libzmq 절대값은 실행 시점마다 흔들리므로,
최근 개선 작업은 zlink 절대 throughput 추이도 같이 봐야 한다.

| 단계 | `PAIR tcp 64B` | `DEALER_DEALER tcp 64B` | 의미 |
|------|----------------|-------------------------|------|
| 2.3 baseline quick | `2.616M` | `2.824M` | 현재 문서 작성 시작점 |
| 2.4 recv public fast path | `3.012M` | `2.760M` | `PAIR` 즉시 회복 |
| 2.5 recv-side `pipe` lock 축소 | `3.025M` | `2.661M` | recv 축 영향 확인, 노이즈 큼 |
| 2.6 send-side `write_and_flush` | `3.011M` | `2.957M` | `DEALER`에 의미 있는 회복 |
| 2.7 TLS-view release | `3.046M` | `3.180M` | recv export steady-state 비용 축소 |
| 2.8 `PAIR` public send sync 축소 | `3.058M` | `3.133M` | `PAIR` send public sync 영향 확인 |
| 2.10b `1~5` 적용 후 public 재측정 | `2.774M` | `3.139M` | `DEALER` 유지, `PAIR`/pattern-specific는 미해결 |
| 2.10c retry-side sync 유지 | `3.238M` | `3.088M` | `PAIR` 회복, send retry 축 재확인 |

이 표가 뜻하는 바는 명확하다.

- `PAIR`가 약 `3.0M` 부근에서 오래 정체한 것은 send-side 병목이 남아 있다는
  강한 신호다.
- 반대로 `DEALER_DEALER`는 recv/TLS와 send `write+flush` 개선에 실제로 더
  많이 반응했다.
- 따라서 이후 우선순위는 `public recv wrapper`를 계속 미세조정하는 것보다
  send lifecycle/backpressure 경로를 더 직접적으로 보는 쪽이 맞다.

### 2.10c retry-side sync 유지 + lifecycle shadow atomic 제거 후 재측정

2026-03-27 현재 HEAD에서 아래 두 변경을 추가했다.

- [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  / [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  에서 `public_api_sync` shadow atomic 제거
- [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  에서 send-ready handler가 없는 blocking send는 retry 동안
  `public_api_sync`를 유지

public 순차 재측정:

- [`perf_linux_20260327_212608_retry_hold_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_212608_retry_hold_public_seq.txt)
- `PAIR tcp 64B`
  - `libzmq 3628.97 Kmsg/s`
  - `zlink 3237.93 Kmsg/s`
  - `-10.78%`
- `PAIR inproc 64B`
  - `libzmq 4175.11 Kmsg/s`
  - `zlink 2766.34 Kmsg/s`
  - `-33.74%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 3833.30 Kmsg/s`
  - `zlink 3088.44 Kmsg/s`
  - `-19.43%`
- `DEALER_DEALER inproc 64B`
  - `libzmq 4108.89 Kmsg/s`
  - `zlink 3217.18 Kmsg/s`
  - `-21.70%`
- `DEALER_ROUTER tcp 64B`
  - `libzmq 4039.53 Kmsg/s`
  - `zlink 3026.06 Kmsg/s`
  - `-25.09%`
- `DEALER_ROUTER inproc 64B`
  - `libzmq 4506.29 Kmsg/s`
  - `zlink 2956.17 Kmsg/s`
  - `-34.40%`
- `PUBSUB tcp 64B`
  - `libzmq 3795.19 Kmsg/s`
  - `zlink 2369.20 Kmsg/s`
  - `-37.57%`
- `PUBSUB inproc 64B`
  - `libzmq 3751.86 Kmsg/s`
  - `zlink 2200.01 Kmsg/s`
  - `-41.36%`
- `ROUTER_ROUTER tcp 64B`
  - `libzmq 2861.16 Kmsg/s`
  - `zlink 1240.04 Kmsg/s`
  - `-56.66%`
- `ROUTER_ROUTER inproc 64B`
  - `libzmq 3279.51 Kmsg/s`
  - `zlink 2449.13 Kmsg/s`
  - `-25.32%`

raw 재측정:

- [`perf_linux_20260327_213142_retry_hold_raw_seq2.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_213142_retry_hold_raw_seq2.txt)
- `PAIR tcp 64B`
  - `libzmq 3660.66 Kmsg/s`
  - `zlink 3153.50 Kmsg/s`
  - `-13.85%`
- `PAIR inproc 64B`
  - `libzmq 4000.62 Kmsg/s`
  - `zlink 3343.75 Kmsg/s`
  - `-16.42%`
- `DEALER_DEALER tcp 64B`
  - `libzmq 4460.78 Kmsg/s`
  - `zlink 3177.72 Kmsg/s`
  - `-28.76%`
- `DEALER_DEALER inproc 64B`
  - `libzmq 4176.41 Kmsg/s`
  - `zlink 2821.68 Kmsg/s`
  - `-32.44%`

의미:

- `PAIR tcp`는 이번 라운드의 가장 확실한 회복이다.
  public 기준 `2.774M -> 3.238M`로 올랐다.
- `PAIR tcp` raw/public은 현재 `3.154M` vs `3.238M`로 거의 붙는다.
  이 케이스에서는 public wrapper가 본체가 아니라
  send retry/lifecycle 쪽이 본체였다고 보는 게 맞다.
- `DEALER_DEALER`와 `DEALER_ROUTER`는 일부 개선됐지만,
  `PUBSUB`와 `ROUTER_ROUTER`는 여전히 pattern-specific 잔여 비용이 크다.
- 따라서 다음 우선순위는 "wrapper를 더 걷어내자"보다
  `pipe` publication/ordering과 routed/publish path를 더 깊게 보는 쪽이다.

## 3. 현재 `with_zmq single`이 실제로 비교하는 surface

### 3.1 공통 패턴

현재 `PAIR`/`DEALER_DEALER` 기준으로 실제 호출은 다음과 같다.

| Pattern | zlink | libzmq |
|------|------|------|
| `PAIR` | `zlink_send(parts,1)` + `zlink_recv(&parts,&count)` | `send_exact(buffer)` + `zlink_msg_recv(msg)` |
| `DEALER_DEALER` | `zlink_send(parts,1)` + `zlink_recv(&parts,&count)` | `send_exact(buffer)` + `zlink_msg_recv(msg)` |
| `DEALER_ROUTER` | `zlink_send(parts,1)` + `zlink_recv(source_rid,&parts,&count)` | `send_exact(buffer)` + `zlink_msg_recv()` 2회 |
| `PUBSUB` | `zlink_publish(NULL, &part,1)` + `zlink_recv(&parts,&count)` | `send_exact(buffer)` + `zlink_msg_recv(msg)` |
| `ROUTER_ROUTER` | `zlink_send(parts,2)` + `zlink_recv(source_rid,&parts,&count)` | `zlink_send(..., "RID", SNDMORE)` + `zlink_send(..., payload)` + `zlink_msg_recv()` 2회 |

의미는 명확하다.

- 현재 `zlink` 측은 public aggregate API를 직접 측정한다.
- 현재 `libzmq` 측은 raw message recv path를 직접 측정한다.
- 이건 `3/05 baseline`과 달리 현재 benchmark 자체가 더 이상 대칭이 아니라는
  뜻이다.
- 2026-03-28 single `PUBSUB` zlink bench를
  `zlink_publish(NULL, &part, 1)` + `zlink_recv()` no-topic payload-only
  경로로 정렬했다.
- aligned first run/rerun은 `tcp -24.51% / -23.17%`,
  `inproc -41.79% / -44.68%`였다.
- 따라서 이전 empty-topic wire-shape mismatch는 제거됐지만,
  남은 `PUBSUB` gap은 여전히 publication/lifecycle/distribution differential을
  강하게 반영한다.
- 다만 `2.10`의 raw/public 분리 결과를 보면, 현재 HEAD에서 이 surface mismatch는
  해석에는 중요하지만 남은 gap의 본체는 아니다.
- 즉 현재 가장 큰 격차는 `public wrapper`보다 send-side core engine differential을
  더 강하게 반영한다.

### 3.2 왜 이게 중요한가

`PAIR`는 원래 경로가 가장 얇다.

그래서 현재처럼:

- zlink는 `zlink_recv()` aggregate export
- libzmq는 `zmq_msg_recv()` raw one-frame recv

를 비교하면, `PAIR`에서 gap이 가장 크게 드러나는 것이 자연스럽다.

즉 지금 `PAIR`의 큰 gap은 오히려
"core transport가 가장 많이 무너졌다"기보다
"public aggregate recv 비용이 가장 잘 드러난다"는 해석이 더 맞다.

## 4. 현재 코드 기준의 실제 hot path 차이

여기서 hot path는 steady-state 동안 메시지 1건마다 반복해서 타는 코드다.

### 4.1 `pipe_t` 잠금은 현재 상위 core-side 후보다

현재 [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp) 의
steady-state 경로는 아래 메서드마다 `scoped_fast_lock_t lock(_out_sync)`를 잡는다.

- `check_read()`
- `read()`
- `check_write()`
- `write()`
- `flush()`

반면 libzmq [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp) 의 같은
경로에는 해당 잠금이 없다.

그리고 이 차이는 단순히 "현재 코드가 libzmq와 다르다" 수준이 아니라,
`3/05` baseline 근처 커밋 `7bea9e3f`의 zlink [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
에도 없던 항목이다. 즉 이 잠금은 남아 있는 상대 gap을 설명하는 후보일 뿐 아니라,
`3/05 -> current` 성능저하를 설명하는 실제 delta 후보이기도 하다.

의미:

- `PAIR` single-part 메시지 1건당 `write + flush + read`로
  최소 3번의 lock/unlock이 steady-state에 들어간다
- 이 비용은 current public API mismatch와 별개로 존재하는
  core-side 후보다
- single-thread benchmark라고 해도 uncontended mutex 비용 자체는 남는다.
  즉 contention이 없다는 사실이 이 후보를 무효화하지는 않는다.

다만 용어는 정확히 써야 한다.

- 현재 `fast_mutex_t`는 순수 CAS spinlock이 아니라
  [`fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
  기준 `pthread_mutex_t` + owner tracking이다
- 즉 "spinlock 3회"라고 단정하는 것보다
  "uncontended fast mutex lock/unlock 3회"라고 보는 편이 맞다

그래도 영향이 작다는 뜻은 아니다. 오히려 현재 기준에선
가장 강한 core-side 단일 후보 중 하나다.

### 4.2 zlink `recv`는 public aggregate export를 탄다

현재 `zlink_recv()`는 [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
에서 두 갈래로 나뉜다.

- `PAIR`/`DEALER` direct single-part fast path
  1. `recv_tls_view::begin_with_first_slot()`
  2. `socket_base_t::recv()`
  3. single-part면 `commit_reserved_single()`
  4. multipart면 follow-up frame 수집 후 `push()/commit()`
- 그 외 generic/public aggregate path
  1. `recv_tls_view::begin()`
  2. `recv_msg_socket()` 또는 `recv_msg_routed_socket()`
  3. single-part면 `recv_tls_view::export_single()`
  4. multipart면 follow-up frame 수집 후 `push()/commit()`

관련 코드:

- [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
- [`recv_tls_view.hpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_tls_view.hpp)

특징:

- direct single-part fast path도 최종 반환은 TLS slot을 통해 이뤄진다.
- generic path의 `begin()`은 이전 결과를 정리하기 위해 `reset()`을 수행한다.
- generic path의 `export_single()`도 최종적으로 TLS slot으로 move한다.
- 현재 HEAD에서는 `zlink_multipart_close()`가 TLS view 결과를 즉시 release하여,
  다음 recv begin이 이미 닫힌 slot을 다시 close/init 하는 낭비를 줄인다.

반면 `libzmq` bench의 `PAIR`/`DEALER_DEALER`는
[`bench_zmq_pair.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zmq/bench_zmq_pair.cpp),
[`bench_zmq_dealer_dealer.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zmq/bench_zmq_dealer_dealer.cpp)
에서 `zlink_msg_recv()`를 바로 사용한다.

즉 현재 격차의 가장 강한 recv-side 원인은:

- `zlink_recv()` aggregate/TLS export
- 대
- `zmq_msg_recv()` raw recv

차이다.

다만 현재 HEAD에는 `PAIR`/`DEALER` public single-part direct recv fast path가
이미 일부 복구돼 있다. 그래서 지금 남아 있는 recv-side 원인은 더 정확히는:

- routed/strip/multipart 경로의 aggregate export
- direct fast path가 닿지 않는 public recv overhead
- `recv_msg_socket()` / mode guard 계층

으로 보는 편이 맞다.

실험 메모:

- `PAIR` send에서 `socket_base_t::send()`의 public sync를 우회하는
  pair-only fast path를 현재 코드에 제한적으로 유지하고 있다.
- same-handle concurrent send regression은 계속 통과한다.
- `5s quick run` 기준 `PAIR tcp 64B`는
  `zlink 3.058M`, `libzmq 3.643M`로 약 `-16.1%` 차이까지 줄었다.
- 개선 폭이 압도적이진 않지만, `PAIR`에서는 public send sync가 실제 비용 축이라는
  실측 근거로는 의미가 있다.
- 다만 `DEALER` 이상으로 일반화할 만큼 단순한 구조는 아니므로,
  이것을 곧바로 전체 해법으로 과대해석하면 안 된다.

### 4.3 zlink `send`도 public path를 탄다

현재 `zlink` bench는 `PAIR`/`DEALER_DEALER`에서도 `zlink_send(parts,1)`를 쓴다.

관련 코드:

- [`bench_zlink_pair.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pair.cpp)
- [`bench_zlink_dealer_dealer.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_dealer_dealer.cpp)

이 경로는 [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
→ [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
로 들어간다.

여기서 중요한 보정이 하나 있다.

- `PAIR`/`DEALER` single-part send는 현재 `zlink_send()`에서
  완전히 일반 multipart helper를 타는 것은 아니다
- [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
  기준 `send_socket_singlepart_fast()`가 실제로 사용된다

즉 send wrapper 전체를 "매우 두꺼운 일반 경로"라고 과장하면 안 된다.
하지만 그 fast path 안쪽에서 결국:

- validation
- public lifecycle coordinator
- process_commands
- backpressure 복귀 loop

를 포함한다.

반면 `libzmq` bench는 `send_exact()`로 raw send를 사용한다.

즉 현재 `single` 상대 비교는 recv뿐 아니라 send 쪽도
`public zlink_send` 대 `raw zmq_send` 차이를 함께 포함하며,
send-side에서는 wrapper depth 자체보다 `socket_base_t::send()` 내부 비용이
더 중요하다.

### 4.4 recv mode guard는 현재도 zlink-only 비용이다

`recv_msg_socket()`은 [`recv_internal.cpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_internal.cpp)
에서 매 recv마다:

- `socket_msg_dispatch_active()`
- `sub_dispatch_active()`
- `xpub_dispatch_active()`
- `stream_dispatch_active()`

를 확인한다.

이건 correctness guard로는 맞지만, libzmq의 raw `msg_recv` 쪽에는 같은 층의
분리가 없다.

즉 이것도 현재 상대 gap에 기여하는 zlink-side 고정비다.

### 4.5 `PAIR/DEALER`의 `last_recv_source_rid`는 현재 P0가 아니다

이건 중요한 수정 사항이다.

`3/05 -> 3/23` 사이 diff에는 `PAIR`/`DEALER`의
`store_last_recv_source_rid()`가 실제로 들어왔었다.
하지만 현재 HEAD의 [`pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp),
[`dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
기준으로는 그 갱신이 빠져 있다.

즉:

- 과거 시점 분석에는 의미가 있었지만
- 현재 HEAD 원인분석의 상위 후보로 계속 두면 안 된다

현재 문서에서는 이 항목을 historical note로만 취급한다.

### 4.6 callback-dispatch mode awareness는 현재도 남아 있다

`PAIR`/`DEALER`의 attach/read activation 쪽은 여전히 dispatch mode를 의식한다.

관련 코드:

- [`pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
- [`dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)

다만 이건 현재 `PAIR`/`DEALER` recv loop의 최상위 직접 원인이라기보다,
mode specialization이 덜 된 구조라는 보조 신호로 보는 것이 맞다.

## 5. 현재 원인 우선순위

현재 코드와 현재 benchmark surface를 함께 보면 우선순위는 이렇게 정리된다.

1. 현재 `with_zmq single` surface mismatch
   - zlink: `zlink_send` + `zlink_recv`
   - libzmq: `send_exact` + `zlink_msg_recv`
2. `zlink_send()`의 public lifecycle/backpressure 고정비
3. send-side `pipe_t`의 남아 있는 steady-state 잠금/serialization 비용
4. `zlink_recv()`의 남아 있는 public aggregate/export 고정비
5. recv-side `pipe_t`의 남아 있는 경로와 `recv_internal.cpp`의 dispatch/mode guard
6. `PUBSUB`/`ROUTER`의 패턴 전용 surface 차이
7. callback-dispatch aware socket 구조

반대로 현재 우선순위가 낮은 것은:

- `fq/lb` 재작성
- `PAIR/DEALER` algorithm 자체 변경
- `pipe` lock 제거 같은 thread-safe 위험 최적화
- 이미 현재 HEAD에서 빠진 `last_recv_source_rid`

여기서 중요한 점은 우선순위 1과 2를 구분해서 읽는 것이다.

- 우선순위 1은 "현재 비교가 완전히 대칭이 아니다"는 해석 문제다.
- 우선순위 2 이하부터가 "그 비대칭을 인정하더라도 zlink public path 안에서
  실제로 줄일 수 있는 steady-state 비용"이다.

즉 실제 개선 작업은 2번부터 시작하지만,
그 결과를 해석할 때는 항상 1번을 같이 붙여서 봐야 한다.

## 6. 해석상 중요한 보정

### 6.1 지금 gap을 전부 `core` 회귀로 읽으면 안 된다

현재 측정은 `core engine vs core engine` 비교가 아니라,
상당 부분:

- `zlink`의 더 높은 public surface 비용
- 대
- `libzmq`의 더 낮은 raw message 비용

을 비교하고 있다.

즉 현재 수치는 "public API까지 포함한 사용자 체감 비용"으로는 의미가 있지만,
"transport core가 libzmq보다 얼마나 느린가"의 순수한 답으로 쓰기엔 과한 면이 있다.

### 6.2 `3/05` baseline과 현재를 직접 1:1로 붙이면 안 된다

`3/05`에는 양쪽이 사실상 `msg_send/msg_recv` 기준으로 붙었다.
현재는 zlink만 `zlink_send/zlink_recv` public path를 쓰고 있다.

따라서 `3/05` 대비 부호가 뒤집힌 사실은 중요하지만,
그 안에는:

- thread-safe/callback 도입 시기의 구조 변화
- 현재 public API surface로 바뀐 benchmark 측정 방식

이 함께 들어 있다.

### 6.3 `perf` 없이 한 직접 A/B 실험의 의미

현재 환경에서는 `perf`를 사용할 수 없어 `perf stat`으로
store-buffer stall이나 cache miss를 직접 찍지 못했다.

대신 `_out_sync`를 `pipe.cpp` 전체에서 no-op으로 만드는 직접 A/B 실험을
짧게 해봤다. 결과는 오히려 악화였다.

- `PAIR tcp 64B`
  - 안정 상태: 약 `3.046M msg/s`
  - `_out_sync` no-op 실험: 약 `2.158M msg/s`
- `DEALER_DEALER tcp 64B`
  - 안정 상태: 약 `3.180M msg/s`
  - `_out_sync` no-op 실험: 약 `2.468M msg/s`

이 관찰이 뜻하는 바는 명확하다.

- `pipe` lock은 여전히 상위 원인 후보로 남지만,
- 현재 구현에서 이 lock은 단순한 순수 고정비가 아니라
  활성화/직렬화 순서와 얽혀 있다.
- 따라서 "lock만 없애면 libzmq와 비슷해진다"는 식의 단순 해석은 틀리다.

즉 `pipe`를 봐야 한다는 결론은 유지하되,
해결 방식은 전체 no-op 제거가 아니라 더 좁은 근거 기반 축소여야 한다.

이 절의 의미를 한 줄로 요약하면:

- `pipe`/`mailbox`의 현재 lock은 원인 후보이지만
- naive 제거 실험이 모두 실패했으므로
- 다음 단계는 `lock 감축`이 아니라 `ordering 보존 + 불필요한 work 제거`다.

### 2.12 send-side differential 재정리

현재 코드 기준으로 libzmq와 직접 다르게 보이는 send-side 항목은 아래 셋이다.

1. [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
   의 lifecycle coordinator
   - zlink는 `send()`마다 `enter_public_api()` / `leave_public_api()`를 탄다.
   - non-`PAIR`는 여기에 `socket_public_api_lock_scope_t`도 추가로 탄다.
   - libzmq [`socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
     는 non-thread-safe socket에서 이 계층이 없다.
2. [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
   의 `_out_sync`
   - `check_write()` / `write()` / `write_and_flush()` / `flush()`가
     steady-state send마다 lock을 잡는다.
   - libzmq [`pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)에는
     같은 steady-state lock이 없다.
   - 다만 최근 A/B에서 naive 제거는 모두 악화됐으므로
     "없애자"가 아니라 "같은 ordering을 더 싸게 제공할 수 있나"로 봐야 한다.
3. `oneway`에서 더 자주 노출되는 activation / progress 경로
   - `write_and_flush()` 후 `send_activate_read()`
   - HWM 해제 후 `process_activate_write()`
   - `socket_base_t::send()`의 blocking retry loop

반대로 `process_commands(0, true)` 자체는 현재 zlink와 libzmq가 거의 같은
구조다. 둘 다 `rdtsc` 기반 throttled command poll을 쓰므로, 이 부분은
현재 상대 gap의 1차 differential로 보기 어렵다.

## 7. 다음 작업 우선순위

현재 기준으로는 두 갈래를 분리해서 보는 게 맞다.

### 7.1 원인 분리용

1. `PAIR`/`DEALER_DEALER` raw/public 분리는 완료됐고,
   현재는 send-path 변경 뒤 raw/public 분리를 다시 찍는 guardrail로 유지한다.
2. 이후 비교는 `zlink raw - libzmq raw`보다
   `zlink public/current core path - libzmq raw`를 실제 사용자 체감 gap으로 본다.
3. routed/topic 패턴은 공통 hot path와 pattern-specific 비용을 따로 본다.

### 7.2 실제 개선용

현재 public API 성능을 끌어올리는 쪽은 아래 순서가 맞다.

1. `socket_base_t::send()`의 public lifecycle fast path를 설계한다.
   - 가장 유력한 구조 후보는 `enter_public_api`와 `public_api_sync`를
     steady-state에서 더 적은 atomic으로 합치는 방향이다.
   - 다만 이는 `begin_close_or_fail_busy`, callback handoff, close semantics를
     같이 다시 증명해야 하므로 작은 tweak가 아니라 설계 작업으로 본다.
2. blocking retry 경로에서 반복되는 `public_api_sync` 재획득 비용을 줄이는
   방향을 별도 후보로 본다.
   - 현재 retry loop는 `enter_public_api()`를 다시 하는 구조는 아니다.
   - admission은 `send()` 호출당 한 번만 일어나고,
     반복되는 것은 `socket_public_api_lock_scope_t`다.
   - 따라서 개선 포인트는 "re-admission 제거"가 아니라
     backpressure 복귀 시 `public_api_sync`와 `xsend()` 경로를
     어떻게 더 싸게 재시도하느냐에 가깝다.
   - 현재 HEAD는 send-ready handler가 없는 blocking send에 한해
     retry 동안 sync를 유지하도록 조정했다.
   - `PAIR tcp`가 실제로 크게 올라왔으므로 이 축은 여전히 상위다.
   - 다만 send-ready handler가 걸린 경로까지 같은 방식으로 넓히는 건
     close/callback handoff 검토가 더 필요하다.
3. send-side `pipe_t`는 전체 lock 제거가 아니라
   final-part, activation, wakeup, flush ordering을 유지하면서
   lock 안의 work를 줄이는 방향으로 본다.
   - `write_and_flush()`는 이미 반영됐고 유지한다.
   - `_out_sync` no-op, activation lock 밖 이동, mailbox read lock 제거는
     모두 배제된 실험으로 유지한다.
4. `fast_mutex_t` owner tracking 제거 같은 "pipe 전용 non-reentrant mutex" 제안은
   현재 그대로는 채택하지 않는다.
   - `pipe_t::terminate()`, `set_nodelay()`, `process_delimiter()`,
     `send_disconnect_msg()`는 `_out_sync`를 잡은 상태에서
     `rollback()` / `flush()`를 다시 호출한다.
   - 즉 현재 `pipe_t`는 실제로 reentrant lock 성질을 의존하므로,
     owner/depth 제거는 low-risk가 아니라 correctness risk다.
5. `socket_base_t::send()`의 send-side throttle은 보조 후보로 본다.
   - 현재 `process_commands(0, true)` 구조 자체는 libzmq와 거의 같다.
   - 따라서 이 항목은 "문제의 본체"라기보다, send-side differential을
     더 줄일 수 있는 보조 후보다.
   - `counter-only` 단순 치환은 현재 문서 기준으로 보류다.
6. `zlink_recv()`는 direct single-part fast path가 닿지 않는 경로부터 줄인다.
   - routed
   - strip
   - multipart export
   - 다만 `PAIR`/`DEALER` single-part의 `begin_with_first_slot()` fast path는
     이미 들어가 있으므로, 여기서의 추가 이득은 현재 secondary다.
7. `recv_internal.cpp`의 mode guard는
   dispatch mode가 고정인 steady-state 경로에서 더 얕은 분기로 내릴 수 있는지 본다.
8. `PUBSUB`/`ROUTER`는 공통 개선 후에도 남는 gap을 별도로 본다.
   - topic/routing prefix
   - pattern-specific public surface
9. `perf` 없이도 가능한 A/B 토글과 quick-run 기록을 계속 남겨
   원인 후보를 하나씩 배제한다
10. send-path 변경 후에는 `PAIR`/`DEALER_DEALER` raw/public 분리를 다시 찍어
    공통 core differential과 public penalty를 다시 분리한다

### 7.3 현재 보류하거나 기각한 제안

아래 제안은 현재 문서 기준으로는 바로 진행하지 않는다.

1. `pipe` lock 전체 제거
   - 최근 배제 실험에서 모두 악화됐다.
2. `pipe` activation을 lock 밖으로 이동
   - 최근 배제 실험에서 악화됐다.
3. mailbox read lock 제거
   - 최근 배제 실험에서 악화됐다.
4. send-side throttle의 counter-only 치환
   - recv와 모양은 비슷하지만, 현재 send throttle은 시간 기반 visibility를
     보장하므로 의미가 다르다.
5. pipe 전용 non-reentrant mutex
   - 현재 코드는 reentrant 성질을 실제로 의존한다.
6. same-thread `send_activate_read()` direct delivery
   - [`object.cpp`](/home/hep7/project/kairos/zlink/core/src/core/object.cpp)
     에서 peer `activate_read`를 mailbox 대신 inline `process_command()`로
     보내는 generic 실험은 `PAIR inproc`은 일부 회복했지만
     `DEALER_DEALER tcp 64B`를 `-25.06%`까지 악화시켰다.
   - `PAIR` no-handler 전용 gate로 더 좁혀도
     `PAIR tcp 64B` `-26.32%`, `PAIR inproc 64B` `-32.96%`로 악화됐다.
   - 따라서 현재 `activate_read publication`은
     "mailbox 왕복만 줄이면 바로 이득이 나는 순수 고정비"가 아니다.
     현재 구현에서는 progress ordering / callback / engine wakeup과 얽혀 있어
     direct delivery를 기본 후보로 올리지 않는다.
7. `send_socket_singlepart_fast()`의 중복 `msg->check()` 제거
   - [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
     에서 public single-part fast path의 wrapper-side `msg->check()`를
     걷는 실험은 `PAIR inproc`은 일부 회복했지만
     `DEALER_DEALER inproc 64B`를 `-31.51%`로 악화시켰다.
   - 즉 이 중복 검사는 현재 single bench의 상위 본체가 아니며,
     제거 자체로는 유지 가능한 broad win을 만들지 못했다.

thread-safe 계약을 깨뜨리는 최적화는 이 단계의 후보가 아니다.

## 8. 요약

현재 `single` 격차는 실재하지만, 그 의미를 정확히 읽어야 한다.

- POSD 직접 회귀는 대부분 회복됐다.
- 현재 benchmark surface가 완전히 대칭은 아니지만,
  `2.10` 기준으로 public wrapper penalty는 이미 secondary다.
- 즉 현재 남은 큰 gap의 본체는 send-side core engine differential 쪽이다.
- 현재 HEAD 기준 상위 원인은:
  - `zlink_send()` public lifecycle/backpressure
  - blocking retry 시의 `public_api_sync` 재획득
  - `pipe_t` steady-state 잠금과 activation/progress ordering
  - `zlink_recv()` aggregate/TLS export의 남은 routed/topic 경로
  - recv mode guard
  - pattern-specific public surface 차이
다.

- 추가로 현재는 `PAIR` 한정 public send sync 축소가 실제 개선을 보여서,
  send-side lifecycle/public sync가 여전히 상위 축임을 재확인했다.

따라서 지금 결과는
"public wrapper 때문에 느리다"보다,
"현재 zlink core send-side engine이 libzmq raw path 대비 얼마나 비싼가"
에 더 가깝다.

## 9. 2026-03-27 late-night iteration 로그

- 작업한 가설
  - `DEALER_DEALER` single bench의 steady-state는 active outbound pipe가
    하나뿐이므로, `lb_t::sendpipe()/has_out()`에서 일반 load-balancing loop를
    매 메시지 반복하지 않아도 된다.
- 수정한 파일 경로
  - [`lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
  - [`test_public_inproc_multipart_send.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_public_inproc_multipart_send.cpp)
- 실행한 명령
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^test_public_inproc_multipart_send$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_single_pipe_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_single_pipe_guardrail`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_single_pipe_guardrail_raw`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_single_pipe_fastpath_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_router_single_pipe_fastpath`
  - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py dealer_dealer --build-dir core/build --runs 1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260327_233556_dealer_single_pipe_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233556_dealer_single_pipe_fastpath.txt)
  - [`perf_linux_20260327_233634_pair_single_pipe_guardrail.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233634_pair_single_pipe_guardrail.txt)
  - [`perf_linux_20260327_233703_pair_single_pipe_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233703_pair_single_pipe_guardrail_raw.txt)
  - [`perf_linux_20260327_233733_dealer_single_pipe_fastpath_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233733_dealer_single_pipe_fastpath_raw.txt)
  - [`perf_linux_20260327_233804_dealer_router_single_pipe_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_233804_dealer_router_single_pipe_fastpath.txt)
- 핵심 수치
  - 유지한 `lb` one-pipe fast path
    - `DEALER_DEALER tcp 64B`: `3693.91 Kmsg/s` vs `3265.99 Kmsg/s`, `-11.58%`
    - `DEALER_DEALER inproc 64B`: `4356.51 Kmsg/s` vs `3177.03 Kmsg/s`, `-27.07%`
    - `DEALER_DEALER tcp raw`: `3822.41 Kmsg/s` vs `3352.16 Kmsg/s`, `-12.30%`
    - `DEALER_DEALER inproc raw`: `4164.13 Kmsg/s` vs `2917.13 Kmsg/s`, `-29.95%`
    - `DEALER` public penalty는 이번 상태에서 작다.
      - tcp: `3265.99` vs raw `3352.16`, public이 약 `-2.57%`
      - inproc: `3177.03` vs raw `2917.13`, public이 약 `+8.91%`
    - `PAIR` guardrail
      - public tcp `-10.00%`, inproc `-31.39%`
      - raw tcp `-16.73%`, inproc `-15.13%`
      - 즉 `PAIR inproc` public penalty는 다시 커져 있다.
    - `DEALER_ROUTER` smoke
      - tcp `-33.98%`, inproc `-24.43%`
    - multi `dealer_dealer tcp 64B`
      - `2299.48 Kmsg/s` vs `1619.89 Kmsg/s`, `-29.55%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `lb.cpp` one-active-pipe `DEALER` send fast path
    - `DEALER` public inproc single/multipart/concurrent send regression 추가
  - 원복
    - `fq.cpp` one-active-pipe recv fast path
      - [`perf_linux_20260327_234037_dealer_single_pipe_fastpath_lb_fq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_234037_dealer_single_pipe_fastpath_lb_fq.txt)
      - `DEALER_DEALER inproc 64B`가 `-34.71%`로 악화돼 유지하지 않음
- 해석
  - 이번 라운드의 유지 후보는 "send-side `pipe` publication/order lock 안 work 축소"에
    해당한다.
  - `DEALER_DEALER tcp`는 stop condition 바로 아래까지 회복됐고,
    raw/public 분리상 이번 라운드의 이득은 public wrapper가 아니라
    core send-side work 축 축소로 읽는 편이 맞다.
  - 반대로 `inproc`, `DEALER_ROUTER`, multi guardrail은 아직 미달이다.
  - 따라서 이 변경은 유지할 가치가 있지만, broader acceptance를 통과한
    안정 지점은 아직 아니다.
- 다음 iteration 우선순위
  - `send-side lifecycle/backpressure`에서 더 안전한 공통 atomic 축소 후보가
    실제로 있는지 다시 좁힌다.
  - 동시에 `DEALER` 계열은 이번 one-pipe send fast path를 기준선으로 두고,
    `inproc` core differential과 routed/pattern-specific 잔여 비용을 분리한다.
  - `PUBSUB` / `ROUTER_ROUTER`는 아직 이번 라운드로는 직접 닿지 않았으므로
    우선순위는 그대로 유지한다.

## 10. 2026-03-28 midnight iteration 로그

- 작업한 가설
  - `pipe::write_and_flush()` 뒤 `send_activate_read()` publication을
    same-thread direct delivery로 바꾸면 inproc send publication cost를
    줄일 수 있다.
- 수정한 파일 경로
  - [`object.cpp`](/home/hep7/project/kairos/zlink/core/src/core/object.cpp)
  - [`test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
    임시 회귀 시도 후 원복
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R 'test_monitor_perf_contract|test_monitor_socket_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_activate_read_direct`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_activate_read_direct`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_inline_activate_read_pair_only`
- 생성된 결과 파일 경로
  - [`perf_linux_20260327_235547_pair_activate_read_direct.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_235547_pair_activate_read_direct.txt)
  - [`perf_linux_20260327_235621_dealer_activate_read_direct.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260327_235621_dealer_activate_read_direct.txt)
  - [`perf_linux_20260328_000053_pair_inline_activate_read_pair_only.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_000053_pair_inline_activate_read_pair_only.txt)
- 핵심 수치
  - generic direct `activate_read`
    - `PAIR tcp 64B`: `3703.59 Kmsg/s` vs `3235.59 Kmsg/s`, `-12.64%`
    - `PAIR inproc 64B`: `4170.15 Kmsg/s` vs `3176.67 Kmsg/s`, `-23.82%`
    - `DEALER_DEALER tcp 64B`: `3737.68 Kmsg/s` vs `2801.02 Kmsg/s`, `-25.06%`
    - `DEALER_DEALER inproc 64B`: `4104.88 Kmsg/s` vs `3294.05 Kmsg/s`, `-19.75%`
  - `PAIR` no-handler 전용 gate
    - `PAIR tcp 64B`: `3722.28 Kmsg/s` vs `2742.57 Kmsg/s`, `-26.32%`
    - `PAIR inproc 64B`: `4165.32 Kmsg/s` vs `2792.45 Kmsg/s`, `-32.96%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음. latest accepted delta는 여전히 `lb.cpp` one-active-pipe `DEALER`
      send fast path다.
  - 원복
    - `object.cpp` generic same-thread `send_activate_read()` direct delivery
    - `PAIR` no-handler 전용 inline `activate_read` gate
    - `test_socket_with_handler.cpp` 임시 recv-handler 회귀 시도
- 해석
  - `activate_read publication`은 현재 코드에서 mailbox 왕복만 줄이면 되는
    순수 오버헤드가 아니다.
  - generic direct path는 `DEALER_DEALER tcp`를 치명적으로 악화시켜
    guardrail을 즉시 벗어났다.
  - `PAIR` 전용으로 좁혀도 `PAIR tcp/inproc` 둘 다 기준선보다 나빠졌다.
  - 따라서 현재 우선순위는 여전히 `send-side lifecycle/backpressure`와
    `pipe` lock 안 work 축소이며, `activate_read` direct publication은
    rejected candidate로 유지한다.
- 다음 iteration 우선순위
  - `socket_base_t::send()` lifecycle/admission 쪽에서 현재 문서에 남은
    공통 atomic 축소 후보를 다시 좁힌다.
  - `DEALER`는 유지 중인 `lb` one-pipe fast path 기준선에서
    `inproc` differential을 계속 분리한다.
  - `PUBSUB` / `ROUTER_ROUTER`는 공통 send-side 후보가 더 이상 유지되지 않으면
    다음 순서로 올린다.

## 11. 2026-03-28 public send fast-path check elision 로그

- 작업한 가설
  - `send_socket_singlepart_fast()`가 `socket->send()` 안에서 다시 수행하는
    `msg->check()`를 wrapper에서 한 번 더 호출하고 있으므로,
    이 중복 검사를 제거하면 `PAIR`/`DEALER` public send penalty를 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R 'test_monitor_perf_contract|test_monitor_socket_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_singlepart_public_check_elision`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag dealer_singlepart_public_check_elision`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_000714_pair_singlepart_public_check_elision.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_000714_pair_singlepart_public_check_elision.txt)
  - [`perf_linux_20260328_000746_dealer_singlepart_public_check_elision.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_000746_dealer_singlepart_public_check_elision.txt)
- 핵심 수치
  - `PAIR tcp 64B`: `3727.31 Kmsg/s` vs `3180.10 Kmsg/s`, `-14.68%`
  - `PAIR inproc 64B`: `4150.14 Kmsg/s` vs `3261.31 Kmsg/s`, `-21.42%`
  - `DEALER_DEALER tcp 64B`: `3593.58 Kmsg/s` vs `3110.56 Kmsg/s`, `-13.44%`
  - `DEALER_DEALER inproc 64B`: `4169.00 Kmsg/s` vs `2855.55 Kmsg/s`, `-31.51%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_message_send_api.cpp` single-part public fast path의
      wrapper-side `msg->check()` 제거
- 해석
  - `PAIR inproc` 하나만 보면 좋아 보일 수 있지만,
    `DEALER_DEALER inproc`이 기준선보다 더 나빠져 broad win이 아니다.
  - 따라서 이 중복 검사는 현재 최상위 병목이 아니며,
    유지 후보가 아니다.
- 다음 iteration 우선순위
  - 여전히 `socket_base_t::send()` lifecycle/backpressure 쪽의
    더 안전한 공통 축소 후보를 먼저 찾는다.
  - 그 다음은 유지 중인 `DEALER` one-pipe send fast path 기준선에서
    `inproc` differential을 더 분리한다.

## 12. 2026-03-28 PUBSUB dist fast path 로그

- 작업한 가설
  - `PUBSUB` steady-state에서 matching pipe가 하나뿐이면
    generic `dist_t` distributor loop와 pipe index lookup을 건너뛰는 것만으로도
    zlink-only send differential을 줄일 수 있다.
- 수정한 파일 경로
  - [`dist.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.hpp)
  - [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
  - [`test_multi_socket_contract_regressions.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_multi_socket_contract_regressions.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_multi_socket_contract_regressions)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^test_pubsub_filter_xpub$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_dist_single_pipe_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_dist_single_pipe_fastpath_rerun`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_dealer_guardrail_public_after_pubsub_dist_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pair_dealer_guardrail_raw_after_pubsub_dist_seq`
  - `./core/bench/with_zmq/run_benchmarks_multi.sh --reuse-build --pattern pubsub --msg-sizes 64 --transports tcp --runs 1 --warmup 1 --duration 3 --results-tag pubsub_dist_single_pipe_fastpath_multi`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_002158_pubsub_dist_single_pipe_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_002158_pubsub_dist_single_pipe_fastpath.txt)
  - [`perf_linux_20260328_002301_pubsub_dist_single_pipe_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_002301_pubsub_dist_single_pipe_fastpath_rerun.txt)
  - [`perf_linux_20260328_002427_pair_dealer_guardrail_public_after_pubsub_dist_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_002427_pair_dealer_guardrail_public_after_pubsub_dist_seq.txt)
  - [`perf_linux_20260328_002516_pair_dealer_guardrail_raw_after_pubsub_dist_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_002516_pair_dealer_guardrail_raw_after_pubsub_dist_seq.txt)
  - [`perf_linux_20260328_002837_pubsub_dist_single_pipe_fastpath_multi.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/multi/report/perf_linux_20260328_002837_pubsub_dist_single_pipe_fastpath_multi.txt)
- 단계 commit / push
  - commit: `af020ce4`
  - push: `origin/main`
- 핵심 수치
  - `PUBSUB` first run
    - `tcp 64B`: `3180.04 Kmsg/s` vs `2409.49 Kmsg/s`, `-24.23%`
    - `inproc 64B`: `3796.72 Kmsg/s` vs `2050.13 Kmsg/s`, `-46.00%`
  - `PUBSUB` rerun
    - `tcp 64B`: `3309.57 Kmsg/s` vs `2448.99 Kmsg/s`, `-26.00%`
    - `inproc 64B`: `3874.20 Kmsg/s` vs `2227.44 Kmsg/s`, `-42.51%`
  - raw/public guardrail
    - `PAIR` public `tcp/inproc`: `-15.78%` / `-17.62%`
    - `PAIR` raw `tcp/inproc`: `-11.84%` / `-35.47%`
    - `DEALER_DEALER` public `tcp/inproc`: `-13.19%` / `-18.46%`
    - `DEALER_DEALER` raw `tcp/inproc`: `-24.32%` / `-34.91%`
  - multi `pubsub tcp 64B`
    - `6094.02 Kmsg/s` vs `4450.16 Kmsg/s`, `-26.97%`
- 유지한 변경 / 원복한 변경
  - 유지
    - `dist_t` one-matching-pipe `PUBSUB` send fast path
    - index-stable matching-pipe deactivate helper
    - same-handle concurrent `PUB` publish regression
  - 원복
    - 없음
- 해석
  - `PUBSUB tcp`가 즉시 반응했으므로, distributor loop/index/deactivate work는
    실제 hot-path 비용 축이다.
  - raw/public guardrail을 다시 찍어도 이번 라운드가
    `PAIR`/`DEALER` public penalty 재도입으로 설명되진 않는다.
  - 반면 `PUBSUB inproc`와 multi `pubsub`는 여전히 크게 미달이라,
    이 변경은 keep-worthy지만 broader acceptance를 통과한 안정 지점은 아니다.
- 다음 iteration 우선순위
  - 첫 번째는 여전히 `socket_base_t::send()` lifecycle/backpressure 쪽의
    더 안전한 공통 atomic 축소 후보다.
  - 두 번째는 `PUBSUB` single-subscriber win을 `inproc`/multi까지
    확장하는 publication/order 후보 재탐색이다.
  - 세 번째는 `ROUTER_ROUTER` routed path의 pattern-specific differential 정리다.

## 13. 2026-03-28 XPUB single-pipe nodrop fusion 로그

- 작업한 가설
  - `XPUB`에서 matching pipe가 하나뿐인 `nodrop` steady-state는
    `check_hwm()` precheck와 실제 `write`를 같은 pipe lock 안으로 합치면
    `zlink_publish(topic, payload)`의 prefix/payload send differential을
    더 줄일 수 있다.
- 수정한 파일 경로
  - [`dist.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.hpp)
  - [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - 위 세 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub)$' -j1`
  - `./core/build/bin/test_xpub_nodrop`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_single_pipe_nodrop_write_fusion`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_single_pipe_nodrop_write_fusion_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_004716_pubsub_single_pipe_nodrop_write_fusion.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_004716_pubsub_single_pipe_nodrop_write_fusion.txt)
  - [`perf_linux_20260328_004750_pubsub_single_pipe_nodrop_write_fusion_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_004750_pubsub_single_pipe_nodrop_write_fusion_rerun.txt)
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3674.19 Kmsg/s` vs `2414.00 Kmsg/s`, `-34.30%`
    - `PUBSUB inproc 64B`: `3806.91 Kmsg/s` vs `2405.61 Kmsg/s`, `-36.81%`
  - rerun
    - `PUBSUB tcp 64B`: `3766.63 Kmsg/s` vs `2405.39 Kmsg/s`, `-36.14%`
    - `PUBSUB inproc 64B`: `3971.62 Kmsg/s` vs `2176.55 Kmsg/s`, `-45.20%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `XPUB` single matching `nodrop` HWM+write fusion helper 전부
- 해석
  - first run에서 `inproc`만 좋아 보였지만 rerun에서 그 이득도 사라졌다.
  - 반대로 `tcp`는 두 번 모두 accepted baseline보다 명확히 나빠졌다.
  - 따라서 이 후보는 keep-worthy broad win이 아니며 rejected candidate로 둔다.
  - 위 해석에서 "failed write가 pipe inactive 전이까지 함께 일으켜
    tcp publication/wakeup 쪽을 더 비싸게 만들었을 가능성"은 현재 코드 기반
    추론이지만, 원인 규명 전까지는 채택하지 않는다.
- 다음 iteration 우선순위
  - 첫 번째는 그대로 `socket_base_t::send()` lifecycle/backpressure 쪽의
    공통 atomic 축소 후보 재검토다.
  - 두 번째는 `PUBSUB`에서 distribution 자체보다 남은 publication/wakeup
    differential을 더 좁게 분리하는 것이다.

## 14. 2026-03-28 `check_hwm_locked()` self-reentry A/B 로그

- 작업한 가설
  - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp) 의
    `write()` / `write_and_flush()` / `check_write_status()`가 `_out_sync`
    아래에서 `check_hwm()`로 같은 recursive fast mutex를 다시 잡고 있으므로,
    locked helper로 이 self-reentry를 제거하면 send-side `pipe` cost를
    더 줄일 수 있다.
- 수정한 파일 경로
  - [`pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - 위 두 파일은 bench A/B 후 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_socket_with_handler|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pipe_hwm_locked_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pipe_hwm_locked_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pipe_hwm_recursive_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pipe_hwm_recursive_raw`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_010113_codex_pipe_hwm_locked_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_010113_codex_pipe_hwm_locked_public.txt)
  - [`perf_linux_20260328_010154_codex_pipe_hwm_locked_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_010154_codex_pipe_hwm_locked_raw.txt)
  - [`perf_linux_20260328_010424_codex_pipe_hwm_recursive_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_010424_codex_pipe_hwm_recursive_public.txt)
  - [`perf_linux_20260328_010507_codex_pipe_hwm_recursive_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_010507_codex_pipe_hwm_recursive_raw.txt)
- 핵심 수치
  - patched public zlink throughput
    - `PAIR tcp 64B`: `3237.13 Kmsg/s`
    - `PAIR inproc 64B`: `3214.84 Kmsg/s`
    - `DEALER_DEALER tcp 64B`: `3333.61 Kmsg/s`
    - `DEALER_DEALER inproc 64B`: `3191.00 Kmsg/s`
  - reverted public zlink throughput
    - `PAIR tcp 64B`: `3264.20 Kmsg/s`
    - `PAIR inproc 64B`: `2787.27 Kmsg/s`
    - `DEALER_DEALER tcp 64B`: `3062.35 Kmsg/s`
    - `DEALER_DEALER inproc 64B`: `2759.72 Kmsg/s`
  - patched raw zlink throughput
    - `PAIR tcp 64B`: `2958.21 Kmsg/s`
    - `PAIR inproc 64B`: `2807.57 Kmsg/s`
    - `DEALER_DEALER tcp 64B`: `3256.14 Kmsg/s`
    - `DEALER_DEALER inproc 64B`: `3280.86 Kmsg/s`
  - reverted raw zlink throughput
    - `PAIR tcp 64B`: `2867.24 Kmsg/s`
    - `PAIR inproc 64B`: `3193.91 Kmsg/s`
    - `DEALER_DEALER tcp 64B`: `3187.07 Kmsg/s`
    - `DEALER_DEALER inproc 64B`: `3290.80 Kmsg/s`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `pipe.cpp` / `pipe.hpp` locked `check_hwm()` helper
- 해석
  - `DEALER_DEALER` public은 눈에 띄게 좋아졌지만,
    `PAIR`와 raw guardrail은 같은 방향으로 움직이지 않았다.
  - 특히 `PAIR inproc raw`가 `3193.91 -> 2807.57 Kmsg/s`로 내려가
    broad win으로 보긴 어렵다.
  - 따라서 "`_out_sync` self-reentry 제거"는 plausible cost axis인 것은
    맞지만, 현재 구현 형태 그대로는 keep-worthy accepted delta가 아니다.
  - 이 결론은 결과 파일 기반 해석이며, libzmq absolute throughput 자체도 run마다
    흔들려 직접 diff만으로 과대해석하지 않았다.
- 다음 iteration 우선순위
  - 첫 번째는 다시 `socket_base_t::send()` lifecycle/backpressure 쪽의
    공통 atomic/admission 후보를 좁히는 것이다.
  - 두 번째는 send-side `pipe` 경로에서 self-reentry 제거보다 더 직접적인
    ordering/work 축소 후보를 찾는 것이다.
  - 세 번째는 keep-worthy 공통 후보가 더 없으면 `PUBSUB` publication 잔여축과
    `ROUTER_ROUTER` routed path를 순서대로 올린다.

## 15. 2026-03-28 ROUTER single-out-pipe lookup cache 로그

- 작업한 가설
  - `ROUTER`/`STREAM` routed steady-state에서 outbound pipe가 하나뿐이면
    `routing_socket_base_t`의 routed lookup을 cache로 우회하는 것만으로도
    `ROUTER_ROUTER` public send differential을 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
  - [`socket_base_routing.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_routing.cpp)
  - 위 두 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_connect_rid|test_probe_router|test_router_multiple_dealers|test_router_auto_id_format|test_transport_matrix)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag router_single_out_pipe_cache`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag router_single_out_pipe_cache_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_011839_router_single_out_pipe_cache.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_011839_router_single_out_pipe_cache.txt)
  - [`perf_linux_20260328_011918_router_single_out_pipe_cache_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_011918_router_single_out_pipe_cache_rerun.txt)
- 핵심 수치
  - first run
    - `ROUTER_ROUTER tcp 64B`: `2998.27 Kmsg/s` vs `1237.50 Kmsg/s`, `-58.73%`
    - `ROUTER_ROUTER inproc 64B`: `3142.30 Kmsg/s` vs `2453.71 Kmsg/s`, `-21.91%`
  - rerun
    - `ROUTER_ROUTER tcp 64B`: `2844.23 Kmsg/s` vs `1230.30 Kmsg/s`, `-56.74%`
    - `ROUTER_ROUTER inproc 64B`: `3257.12 Kmsg/s` vs `2407.00 Kmsg/s`, `-26.10%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `routing_socket_base` single-out-pipe routed lookup cache 전부
- 해석
  - `inproc` first run만 보면 회복처럼 보였지만 rerun에서 그 이득이 사라졌고,
    `tcp`는 기준선보다 좋아졌다고 보기 어렵다.
  - 따라서 현재 `ROUTER_ROUTER` 잔여 gap을 routed `std::map` lookup 하나로
    설명할 수는 없다.
  - 이 후보는 keep-worthy broad win이 아니며 rejected candidate로 둔다.
- 다음 iteration 우선순위
  - 첫 번째는 여전히 `socket_base_t::send()` lifecycle/backpressure 쪽의
    공통 atomic/admission 후보를 다시 좁히는 것이다.
  - 두 번째는 send-side `pipe` 경로에서 ordering/work 축소 후보다.
  - 세 번째는 keep-worthy 공통 후보가 더 없으면 `PUBSUB` publication 잔여축과
    `ROUTER_ROUTER` public routed send/recv differential을 순서대로 다시 좁힌다.

## 16. 2026-03-28 `public_api_state` full CAS fast path 로그

- 작업한 가설
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    의 `enter_public_api()` / `leave_public_api()` /
    `unlock_public_api_sync_and_leave()` 에 uncontended CAS fast path를 넣으면
    send-side lifecycle atomic cost를 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 두 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_fast_exit_public_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_fast_exit_raw_seq`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_fast_exit_reverted_public_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_fast_exit_reverted_raw_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_012937_codex_lifecycle_fast_exit_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_012937_codex_lifecycle_fast_exit_public_seq.txt)
  - [`perf_linux_20260328_013023_codex_lifecycle_fast_exit_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013023_codex_lifecycle_fast_exit_raw_seq.txt)
  - [`perf_linux_20260328_013301_codex_lifecycle_fast_exit_reverted_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013301_codex_lifecycle_fast_exit_reverted_public_seq.txt)
  - [`perf_linux_20260328_013344_codex_lifecycle_fast_exit_reverted_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013344_codex_lifecycle_fast_exit_reverted_raw_seq.txt)
- 핵심 수치
  - patched public
    - `PAIR tcp 64B`: `4542.78 Kmsg/s` vs `2800.92 Kmsg/s`, `-38.34%`
    - `PAIR inproc 64B`: `4181.74 Kmsg/s` vs `2795.49 Kmsg/s`, `-33.15%`
    - `DEALER_DEALER tcp 64B`: `3858.44 Kmsg/s` vs `3125.45 Kmsg/s`, `-19.00%`
    - `DEALER_DEALER inproc 64B`: `4080.17 Kmsg/s` vs `3356.60 Kmsg/s`, `-17.73%`
  - patched raw
    - `PAIR tcp 64B`: `4387.06 Kmsg/s` vs `3233.82 Kmsg/s`, `-26.29%`
    - `PAIR inproc 64B`: `4172.40 Kmsg/s` vs `2665.57 Kmsg/s`, `-36.11%`
    - `DEALER_DEALER tcp 64B`: `3714.39 Kmsg/s` vs `3187.49 Kmsg/s`, `-14.19%`
    - `DEALER_DEALER inproc 64B`: `4037.43 Kmsg/s` vs `2833.70 Kmsg/s`, `-29.81%`
  - reverted public
    - `PAIR tcp 64B`: `3625.10 Kmsg/s` vs `3158.47 Kmsg/s`, `-12.87%`
    - `PAIR inproc 64B`: `4188.57 Kmsg/s` vs `3171.66 Kmsg/s`, `-24.28%`
    - `DEALER_DEALER tcp 64B`: `3550.24 Kmsg/s` vs `2751.97 Kmsg/s`, `-22.49%`
    - `DEALER_DEALER inproc 64B`: `4174.18 Kmsg/s` vs `3186.47 Kmsg/s`, `-23.66%`
  - reverted raw
    - `PAIR tcp 64B`: `4567.20 Kmsg/s` vs `3280.92 Kmsg/s`, `-28.16%`
    - `PAIR inproc 64B`: `4192.83 Kmsg/s` vs `3143.78 Kmsg/s`, `-25.02%`
    - `DEALER_DEALER tcp 64B`: `3719.76 Kmsg/s` vs `2797.58 Kmsg/s`, `-24.79%`
    - `DEALER_DEALER inproc 64B`: `4347.30 Kmsg/s` vs `2853.17 Kmsg/s`, `-34.37%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp` full lifecycle CAS fast path
    - `unittest_socket_runtime.cpp` 임시 close-preservation 회귀 추가
- 해석
  - `DEALER` raw/public은 일부 회복했지만 `PAIR` public과 `PAIR inproc raw`가
    clean reverted baseline 대비 너무 크게 흔들렸다.
  - 즉 lifecycle atomic cost axis 자체는 살아 있지만, `enter/leave` 전부를
    CAS fast path로 치환하는 현재 형태는 broad win이 아니다.
  - 따라서 이 후보는 rejected candidate로 두고, 다음 라운드는 `PAIR`를
    건드리지 않는 더 좁은 non-`PAIR` 종료 경로로 다시 본다.
- 다음 iteration 우선순위
  - `unlock_public_api_sync_and_leave()` 단독 fast path로
    `DEALER` 계열 raw 회복이 `PAIR` public 흔들림 없이 유지되는지 확인한다.

## 17. 2026-03-28 `unlock_public_api_sync_and_leave()` CAS fast path 로그

- 작업한 가설
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    의 `unlock_public_api_sync_and_leave()` 하나만 좁게 fast path로 바꾸면
    non-`PAIR` send 종료 비용을 줄이면서도 `PAIR` regression은 피할 수 있다.
- 수정한 파일 경로
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 두 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_sync_leave_public_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_sync_leave_raw_seq`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_lifecycle_sync_leave_public_rerun_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_013518_codex_lifecycle_sync_leave_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013518_codex_lifecycle_sync_leave_public_seq.txt)
  - [`perf_linux_20260328_013557_codex_lifecycle_sync_leave_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013557_codex_lifecycle_sync_leave_raw_seq.txt)
  - [`perf_linux_20260328_013659_codex_lifecycle_sync_leave_public_rerun_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_013659_codex_lifecycle_sync_leave_public_rerun_seq.txt)
- 핵심 수치
  - public first run
    - `PAIR tcp 64B`: `4558.41 Kmsg/s` vs `3090.47 Kmsg/s`, `-32.20%`
    - `PAIR inproc 64B`: `4086.96 Kmsg/s` vs `2810.77 Kmsg/s`, `-31.23%`
    - `DEALER_DEALER tcp 64B`: `4283.26 Kmsg/s` vs `3061.73 Kmsg/s`, `-28.52%`
    - `DEALER_DEALER inproc 64B`: `4250.57 Kmsg/s` vs `3171.47 Kmsg/s`, `-25.39%`
  - raw
    - `PAIR tcp 64B`: `3605.96 Kmsg/s` vs `3319.62 Kmsg/s`, `-7.94%`
    - `PAIR inproc 64B`: `4201.83 Kmsg/s` vs `3348.56 Kmsg/s`, `-20.31%`
    - `DEALER_DEALER tcp 64B`: `3610.30 Kmsg/s` vs `2854.94 Kmsg/s`, `-20.92%`
    - `DEALER_DEALER inproc 64B`: `4096.07 Kmsg/s` vs `3305.53 Kmsg/s`, `-19.30%`
  - public rerun
    - `PAIR tcp 64B`: `3655.07 Kmsg/s` vs `3127.22 Kmsg/s`, `-14.44%`
    - `PAIR inproc 64B`: `4104.06 Kmsg/s` vs `3317.05 Kmsg/s`, `-19.18%`
    - `DEALER_DEALER tcp 64B`: `4262.76 Kmsg/s` vs `3101.84 Kmsg/s`, `-27.23%`
    - `DEALER_DEALER inproc 64B`: `4155.75 Kmsg/s` vs `2873.87 Kmsg/s`, `-30.85%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp` `unlock_public_api_sync_and_leave()` CAS fast path
    - `unittest_socket_runtime.cpp` 임시 close-preservation 회귀 추가
- 해석
  - raw는 `PAIR tcp`, `DEALER_DEALER inproc`까지 꽤 회복해
    lifecycle 종료 비용이 실제 cost axis라는 점은 더 강해졌다.
  - 하지만 public rerun에서는 `DEALER_DEALER tcp/inproc`가
    reverted baseline보다 더 흔들렸고, `PAIR`도 stop condition에는 멀다.
  - 즉 이 후보는 raw/public 분리상 "core differential을 줄이는 방향" 자체는
    맞지만, current public surface에서 keep-worthy broad win으로 보긴 어렵다.
  - 따라서 이 후보도 rejected candidate로 두고 원복한다.
- 다음 iteration 우선순위
  - 첫 번째는 여전히 `socket_base_t::send()` lifecycle/backpressure 쪽의
    다음 공통 후보를 찾는 것이다.
  - 다만 현재 문서 기준으로 단순 uncontended CAS 치환 계열은
    유지 후보가 아니므로 우선순위를 낮춘다.

## 18. 2026-03-28 `PAIR` no-sync send scope enter+leave fast path 로그

- 작업한 가설
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    와
    [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
    에서 `PAIR` no-sync public send scope의 admission enter/leave 둘 다
    uncontended fast path를 넣으면 `PAIR` public send lifecycle cost를
    더 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 세 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pair_admission_fast_public_seq`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pair_admission_fast_raw_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_015536_codex_pair_admission_fast_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_015536_codex_pair_admission_fast_public_seq.txt)
  - [`perf_linux_20260328_015622_codex_pair_admission_fast_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_015622_codex_pair_admission_fast_raw_seq.txt)
- 핵심 수치
  - public
    - `PAIR tcp 64B`: `4494.47 Kmsg/s` vs `2788.09 Kmsg/s`, `-37.97%`
    - `PAIR inproc 64B`: `4236.85 Kmsg/s` vs `2850.84 Kmsg/s`, `-32.71%`
    - `DEALER_DEALER tcp 64B`: `3857.80 Kmsg/s` vs `3196.09 Kmsg/s`, `-17.15%`
    - `DEALER_DEALER inproc 64B`: `4205.13 Kmsg/s` vs `2858.53 Kmsg/s`, `-32.02%`
  - raw
    - `PAIR tcp 64B`: `3692.77 Kmsg/s` vs `2954.82 Kmsg/s`, `-19.98%`
    - `PAIR inproc 64B`: `4172.63 Kmsg/s` vs `3347.67 Kmsg/s`, `-19.77%`
    - `DEALER_DEALER tcp 64B`: `3579.95 Kmsg/s` vs `3137.19 Kmsg/s`, `-12.37%`
    - `DEALER_DEALER inproc 64B`: `4119.86 Kmsg/s` vs `3353.52 Kmsg/s`, `-18.60%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp/.hpp` `PAIR` no-sync send scope enter+leave fast path
    - `unittest_socket_runtime.cpp` 임시 fast-path 회귀 추가
- 해석
  - raw만 보면 `PAIR`/`DEALER` 일부 회복이 보이지만,
    public seq에서는 `PAIR tcp/inproc`이 `-37.97% / -32.71%`로 다시 크게
    흔들렸다.
  - 즉 `PAIR` no-sync scope의 enter/leave 둘 다를 CAS fast path로 묶는 현재
    형태는 raw/public guardrail을 깨며 public penalty를 다시 키운다.
  - 이 후보는 lifecycle atomic 축의 방향성만 확인하고 rejected candidate로 둔다.
- 다음 iteration 우선순위
  - `PAIR` no-sync send scope의 enter/leave 둘 다를 한 번에 바꾸는 방향은
    다시 시도하지 않는다.
  - 다음 라운드는 더 좁은 종료 경로만 따로 보거나, keep-worthy 공통 후보가
    없으면 send-side `pipe` ordering/work 축소 쪽으로 옮긴다.

## 19. 2026-03-28 `PAIR` no-sync send scope leave-only fast path 로그

- 작업한 가설
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
    의 `PAIR` no-sync public send scope에서 leave 쪽만 좁게 fast path로 바꾸면
    위의 enter+leave 동시 치환보다 public penalty 재도입을 피할 수 있다.
- 수정한 파일 경로
  - [`socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
  - [`socket_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.hpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 세 파일은 bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_pair_leave_fast_public_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_015918_codex_pair_leave_fast_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_015918_codex_pair_leave_fast_public_seq.txt)
- 핵심 수치
  - public
    - `PAIR tcp 64B`: `3764.37 Kmsg/s` vs `3123.97 Kmsg/s`, `-17.01%`
    - `PAIR inproc 64B`: `4108.01 Kmsg/s` vs `3291.33 Kmsg/s`, `-19.88%`
    - `DEALER_DEALER tcp 64B`: `4462.23 Kmsg/s` vs `2791.81 Kmsg/s`, `-37.43%`
    - `DEALER_DEALER inproc 64B`: `4269.97 Kmsg/s` vs `2809.13 Kmsg/s`, `-34.21%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_runtime.cpp/.hpp` `PAIR` no-sync send scope leave-only fast path
    - `unittest_socket_runtime.cpp` 임시 fast-exit 회귀 추가
- 해석
  - `PAIR`만 보면 enter+leave 동시 치환보다 덜 흔들렸지만,
    같은 seq run에서 `DEALER_DEALER tcp/inproc`가 `-37.43% / -34.21%`까지
    내려가 broad win 근거를 만들지 못했다.
  - 즉 leave-only까지 줄여도 지금 형태의 `PAIR` 전용 no-sync lifecycle fast path는
    유지 후보가 아니다.
  - raw/public guardrail 단계까지 갈 가치가 없는 disqualifying public 결과로 보고
    즉시 원복했다.
- 다음 iteration 우선순위
  - `PAIR` no-sync send scope에 대한 단순 uncontended CAS 치환 계열은
    현재 문서 기준으로 보류/기각 영역으로 내린다.
  - 다음 후보는 send-side lifecycle/backpressure의 다른 공통 구조나,
    keep-worthy 공통 후보가 더 없으면 `pipe` ordering/work 축소 쪽을 본다.

## 20. 2026-03-28 idle send-ready handler retry-sync gate 로그

- 작업한 가설
  - [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
    의 blocking retry loop가 "send-ready handler가 설치돼 있다"는 이유만으로
    `public_api_sync`를 매번 풀었다 다시 잡는다.
  - 실제 callback 개입 조건은 "notification이 이미 armed 됐을 때"로 더 좁으므로,
    idle handler만 있는 retry는 sync를 계속 유지하면 backpressure 복귀 비용을
    줄일 수 있다.
- 수정한 파일 경로
  - [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - [`unittest_socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_socket_runtime.cpp)
  - 위 두 파일은 serial bench 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_monitor_perf_contract|test_socket_with_handler|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_idle_send_ready_retry_public_seq_serial`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_idle_send_ready_retry_raw_seq_serial`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_021040_codex_idle_send_ready_retry_public_seq_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_021040_codex_idle_send_ready_retry_public_seq_serial.txt)
  - [`perf_linux_20260328_021124_codex_idle_send_ready_retry_raw_seq_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_021124_codex_idle_send_ready_retry_raw_seq_serial.txt)
  - 아래 두 파일은 public/raw를 동시에 띄운 잘못된 측정이라 해석에서 제외했다.
    - [`perf_linux_20260328_020957_codex_idle_send_ready_retry_public_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_020957_codex_idle_send_ready_retry_public_seq.txt)
    - [`perf_linux_20260328_020957_codex_idle_send_ready_retry_raw_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_020957_codex_idle_send_ready_retry_raw_seq.txt)
- 핵심 수치
  - public serial
    - `PAIR tcp 64B`: `3806.57 Kmsg/s` vs `3049.30 Kmsg/s`, `-19.89%`
    - `PAIR inproc 64B`: `4138.24 Kmsg/s` vs `3131.20 Kmsg/s`, `-24.33%`
    - `DEALER_DEALER tcp 64B`: `3501.78 Kmsg/s` vs `3158.24 Kmsg/s`, `-9.81%`
    - `DEALER_DEALER inproc 64B`: `4241.06 Kmsg/s` vs `2847.56 Kmsg/s`, `-32.86%`
  - raw serial
    - `PAIR tcp 64B`: `3741.88 Kmsg/s` vs `3244.87 Kmsg/s`, `-13.28%`
    - `PAIR inproc 64B`: `4200.18 Kmsg/s` vs `3251.37 Kmsg/s`, `-22.59%`
    - `DEALER_DEALER tcp 64B`: `3936.01 Kmsg/s` vs `2862.06 Kmsg/s`, `-27.29%`
    - `DEALER_DEALER inproc 64B`: `4357.16 Kmsg/s` vs `3285.83 Kmsg/s`, `-24.59%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_base_msg.cpp` retry loop의 `send_ready_handler_active() -> send_ready_armed` gate
    - `unittest_socket_runtime.cpp` armed-state 회귀 추가
- 해석
  - `DEALER_DEALER tcp` public은 stop condition 안쪽까지 회복됐지만,
    같은 serial run에서 `PAIR inproc` public이 `-24.33%`,
    `DEALER_DEALER inproc` public이 `-32.86%`로 크게 흔들렸다.
  - raw도 `DEALER_DEALER tcp/inproc`가 `-27.29% / -24.59%`여서 broad win이 아니다.
  - 즉 "installed-but-idle send-ready handler 때문에 retry sync를 너무 자주 푼다"
    는 해석은 현재 코드 기준 상위 공통 cost axis로 채택할 수 없다.
  - send-ready callback contract를 깨지 않고 retry gate를 이 조건으로 좁히는
    방향은 rejected candidate로 둔다.
- 다음 iteration 우선순위
  - blocking retry에서 send-ready handler presence를 armed-state로 좁히는
    방향은 다시 시도하지 않는다.
  - send-side lifecycle/backpressure 축은 다른 구조적 후보가 실제로 남는지
    더 좁게 대조한 뒤, keep-worthy 공통 후보가 더 없으면
    `pipe` ordering/work 축소와 `PUBSUB` 잔여 publication 축으로 넘긴다.

## 21. 2026-03-28 single `PUBSUB` no-topic surface alignment 로그

- 작업한 가설
  - current single `PUBSUB` zlink bench가
    `zlink_publish("", &part, 1)` + `zlink_subscribe()`를 써서
    empty-topic frame/topic-aware recv surface를 강제하고 있었다.
  - 저장소 계약/테스트에 이미 있는
    `zlink_publish(NULL, &part, 1)` + `zlink_recv()` no-topic payload-only
    경로로 정렬하면 libzmq single raw send/recv와 shape mismatch를 줄일 수 있다.
- 수정한 파일 경로
  - [`bench_zlink_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_multi_socket_contract_regressions|test_pubsub_filter_xpub)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^test_multi_socket_contract_regressions$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_no_topic_surface_align`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_no_topic_surface_align_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_024920_pubsub_no_topic_surface_align.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_024920_pubsub_no_topic_surface_align.txt)
  - [`perf_linux_20260328_024954_pubsub_no_topic_surface_align_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_024954_pubsub_no_topic_surface_align_rerun.txt)
- 단계 commit / push
  - commit: `d03f56dd`
  - push: `origin/main`
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3208.01 Kmsg/s` vs `2421.78 Kmsg/s`, `-24.51%`
    - `PUBSUB inproc 64B`: `3818.17 Kmsg/s` vs `2222.58 Kmsg/s`, `-41.79%`
  - rerun
    - `PUBSUB tcp 64B`: `3210.77 Kmsg/s` vs `2466.81 Kmsg/s`, `-23.17%`
    - `PUBSUB inproc 64B`: `3797.16 Kmsg/s` vs `2100.44 Kmsg/s`, `-44.68%`
- 유지한 변경 / 원복한 변경
  - 유지
    - single zlink `PUBSUB` bench의 no-topic payload-only surface alignment
  - 원복
    - 없음
- 해석
  - empty-topic frame/topic-aware recv surface mismatch는 실제로 있었고,
    tcp rerun 기준 diff는 `-26.00% -> -23.17%`로 일부 좁혀졌다.
  - 하지만 `inproc` rerun은 `-44.68%`로 여전히 크며,
    first run도 `-41.79%`여서 이 정렬만으로 잔여 gap이 설명되지는 않는다.
  - 즉 single `PUBSUB` current gap은 더 이상 empty-topic wire-shape 차이보다는
    publication/lifecycle/distribution differential을 본체로 읽는 것이 맞다.
  - 첫 `ctest` 묶음에서는 `test_multi_socket_contract_regressions`가
    `test_pubsub_publish_is_safe_from_multiple_threads`에서 timeout/fail로
    끊겼지만, 같은 바이너리 단독 rerun은 즉시 통과했다.
    이 라운드에서는 bench surface 변경과 직접 연결된 재현으로 보지 않았다.
- 다음 iteration 우선순위
  - `PUBSUB`는 이제 aligned no-topic single surface를 기준선으로 유지한다.
  - 다음 후보는 publication/lifecycle 잔여축과
    single-subscriber win의 `inproc`/multi 확장 여부를 직접 보는 것이다.
  - 공통 send-side 후보가 다시 살아나지 않으면 그 다음은
    `ROUTER_ROUTER` public routed differential 정리다.

## 22. 2026-03-28 no-topic single-part `PUBSUB` public fast path 로그

- 작업한 가설
  - aligned single `PUBSUB` surface의 `zlink_publish(NULL, &part, 1)`는
    여전히 generic multipart publish wrapper를 지난다.
  - no-topic single-part publish만 `socket->send()` direct fast path로 좁히면
    public wrapper penalty를 더 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
  - 위 파일은 isolated bench 확인 후 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^test_multi_socket_contract_regressions$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_no_topic_singlepart_fastpath_isolated`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_030104_pubsub_no_topic_singlepart_fastpath_isolated.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_030104_pubsub_no_topic_singlepart_fastpath_isolated.txt)
- 핵심 수치
  - `PUBSUB tcp 64B`: `3329.43 Kmsg/s` vs `2236.07 Kmsg/s`, `-32.84%`
  - `PUBSUB inproc 64B`: `3817.36 Kmsg/s` vs `2069.11 Kmsg/s`, `-45.80%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_message_send_api.cpp` no-topic single-part `PUBSUB` public fast path
- 해석
  - aligned no-topic single surface에서도 generic multipart wrapper를
    바로 우회하는 현재 형태는 tcp/inproc 둘 다 오히려 더 나빴다.
  - 즉 현재 `PUBSUB` 잔여 gap을 public publish wrapper 한 겹으로
    설명할 수는 없다.

## 23. 2026-03-28 `SUB/XSUB` raw multipart recv fast path 로그

- 작업한 가설
  - aligned single `PUBSUB` zlink bench의 receive side는
    `zlink_recv(..., &parts, &part_count)` payload-only raw multipart surface다.
  - `PAIR/DEALER`처럼 TLS first-slot direct export를 `SUB/XSUB` raw recv에도
    좁게 적용하면 single `PUBSUB` recv-side cost를 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
  - [`test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
  - 위 두 파일은 rerun 확인 후 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^test_socket_with_handler$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_sub_raw_multipart_recv_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag pubsub_sub_raw_multipart_recv_fastpath_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_030652_pubsub_sub_raw_multipart_recv_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_030652_pubsub_sub_raw_multipart_recv_fastpath.txt)
  - [`perf_linux_20260328_030723_pubsub_sub_raw_multipart_recv_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_030723_pubsub_sub_raw_multipart_recv_fastpath_rerun.txt)
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3308.89 Kmsg/s` vs `2294.20 Kmsg/s`, `-30.67%`
    - `PUBSUB inproc 64B`: `3896.50 Kmsg/s` vs `2280.61 Kmsg/s`, `-41.47%`
  - rerun
    - `PUBSUB tcp 64B`: `3318.78 Kmsg/s` vs `2431.17 Kmsg/s`, `-26.74%`
    - `PUBSUB inproc 64B`: `4255.17 Kmsg/s` vs `2098.59 Kmsg/s`, `-50.68%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `socket_message_recv_api.cpp` `SUB/XSUB` raw multipart single-part recv fast path
    - `test_socket_with_handler.cpp` raw multipart recv 회귀 추가
- 해석
  - first run은 `inproc`만 약간 좋아졌지만 tcp는 크게 악화됐고,
    rerun은 tcp가 일부 회복되는 대신 inproc이 `-50.68%`로 더 악화됐다.
  - 즉 raw recv single-part export를 `SUB/XSUB`에 더 직접 적용하는 현재 형태는
    broad win이 아니며, current `PUBSUB` 잔여 gap의 본체를 정확히 짚지 못했다.

## 24. 2026-03-28 `PAIR`/`DEALER_DEALER` serial raw/public 재기준 로그

- 작업한 가설
  - guide/hot-path가 여전히 "`public penalty는 이미 secondary`" 쪽으로
    읽히고 있어, 현재 HEAD의 raw/public guardrail을 직렬 baseline으로 다시
    고정해야 했다.
  - 병렬 perf run은 서로 간섭하므로 폐기하고, `PAIR`/`DEALER_DEALER`의
    public/raw를 모두 직렬로 다시 찍어 현재 해석을 갱신한다.
- 수정한 파일 경로
  - 없음
- 실행한 명령
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_public_serial`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_raw_serial`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_public_serial`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_raw_serial`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_031439_codex_20260328_pair_public_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_031439_codex_20260328_pair_public_serial.txt)
  - [`perf_linux_20260328_031501_codex_20260328_pair_raw_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_031501_codex_20260328_pair_raw_serial.txt)
  - [`perf_linux_20260328_031526_codex_20260328_dealer_public_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_031526_codex_20260328_dealer_public_serial.txt)
  - [`perf_linux_20260328_031551_codex_20260328_dealer_raw_serial.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_031551_codex_20260328_dealer_raw_serial.txt)
- 핵심 수치
  - `PAIR`
    - public `tcp/inproc`: `3200.10 / 2816.95 Kmsg/s`
    - raw `tcp/inproc`: `3367.91 / 3015.08 Kmsg/s`
    - raw-public delta: `+5.24% / +7.03%`
  - `DEALER_DEALER`
    - public `tcp/inproc`: `2830.18 / 3145.71 Kmsg/s`
    - raw `tcp/inproc`: `3263.08 / 2799.88 Kmsg/s`
    - raw-public delta: `+15.30% / -10.99%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - 없음
- 해석
  - raw/public 분리는 현재도 keep해야 할 guardrail이지만, 패턴/transport별로
    방향이 다시 엇갈린다.
  - 따라서 `PAIR`/`DEALER` 공통으로 "`public penalty는 이미 low
    single-digit`"라고 고정하는 해석은 더 이상 안전하지 않다.
  - 다음 code iteration에서는 raw/public을 고정 전제가 아니라
    serial rerun으로 다시 확인해야 하는 동적 guardrail로 취급한다.

## 25. 2026-03-28 logical multipart publish contract fix 로그

- 작업한 가설
  - baseline HEAD는 same-handle concurrent `PUB` publish에서
    topic frame과 payload frame이 interleave되어
    `test_pubsub_publish_is_safe_from_multiple_threads`가
    `part_count Expected 1 Was 2`로 반복 실패했다.
  - `socket_base_t::send()` 단위가 아니라 logical multipart publish/send
    전체를 하나의 public send scope로 묶어야 이 contract를 복구할 수 있다.
- 수정한 파일 경로
  - [`multipart_send_txn.cpp`](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp)
  - [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
  - [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_contract_scope_public`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_contract_scope_public_nosinglefast`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_public_contract_scope`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern PAIR --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pair_raw_contract_scope`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_public_contract_scope`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --pattern DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_dealer_raw_contract_scope`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_033544_codex_20260328_pubsub_contract_scope_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033544_codex_20260328_pubsub_contract_scope_public.txt)
  - [`perf_linux_20260328_033719_codex_20260328_pubsub_contract_scope_public_nosinglefast.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033719_codex_20260328_pubsub_contract_scope_public_nosinglefast.txt)
  - [`perf_linux_20260328_033847_codex_20260328_pair_public_contract_scope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033847_codex_20260328_pair_public_contract_scope.txt)
  - [`perf_linux_20260328_033914_codex_20260328_pair_raw_contract_scope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033914_codex_20260328_pair_raw_contract_scope.txt)
  - [`perf_linux_20260328_033936_codex_20260328_dealer_public_contract_scope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033936_codex_20260328_dealer_public_contract_scope.txt)
  - [`perf_linux_20260328_033957_codex_20260328_dealer_raw_contract_scope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_033957_codex_20260328_dealer_raw_contract_scope.txt)
  - pushed commit: `754ee6080badb92539cd266b29680ea37c1284d9`
- 핵심 수치
  - 회귀 테스트
    - `test_multi_socket_contract_regressions`
    - `test_public_inproc_multipart_send`
    - `unittest_socket_runtime`
    - 모두 재통과
  - `PUBSUB` public
    - contract fix만 유지한 current code:
      `tcp -30.71%`, `inproc -40.37%`
    - no-topic single-part direct-send fallback 잠깐 추가한 rerun:
      `tcp -31.67%`, `inproc -38.76%`
  - `PAIR` public/raw
    - public `tcp/inproc`: `2717.91 / 3326.33 Kmsg/s`
    - raw `tcp/inproc`: `3212.40 / 3136.33 Kmsg/s`
  - `DEALER_DEALER` public/raw
    - public `tcp/inproc`: `3126.42 / 3163.47 Kmsg/s`
    - raw `tcp/inproc`: `3135.91 / 3138.42 Kmsg/s`
- 유지한 변경 / 원복한 변경
  - 유지
    - `multipart_send_txn.cpp` logical multipart publish/send single public
      send scope contract fix
    - `socket_base.hpp` logical multipart wrapper friend 선언과 scoped send helper 연계
    - `socket_base_msg.cpp` scoped rollback assert
  - 원복
    - `multipart_send_txn.cpp` no-topic single-part direct-send fallback
- 해석
  - logical multipart publish/send 전체를 하나의 public send scope로 묶는
    수정 없이는 same-handle concurrent `PUB` publish contract가 깨진다.
    이 수정은 성능 후보가 아니라 correctness fix로 유지해야 한다.
  - 다만 latest `PUBSUB` public rerun은 broad win이 아니었고,
    no-topic single-part direct-send fallback도 추가 회복을 만들지 못했다.
  - post-fix raw/public serial rerun은 `PAIR`/`DEALER_DEALER` 모두 다시 mixed라서
    raw/public 분리는 여전히 동적 guardrail이다.
- 다음 iteration 우선순위
  - contract fix를 유지한 채 send-side lifecycle/backpressure retry cost를
    다시 줄인다.
  - 그 다음은 `PUBSUB` publication path에서 single-subscriber correctness를
    유지하면서 steady-state work를 더 걷는 후보를 본다.

## 26. 2026-03-28 `XPUB` all-attached empty-prefix fast path 로그

- 작업한 가설
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
    에서 모든 attached pipe가 empty-prefix subscription을 가진 steady-state면
    single-part publish마다 `_subscriptions.match()`를 다시 돌지 말고
    `send_to_all()`로 바로 내려보내면
    `PUBSUB`의 no-topic/topic-aware 공통 publication cost를 더 줄일 수 있다.
- 수정한 파일 경로
  - [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)
  - [`socket_base_monitor.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_monitor.cpp)
  - [`xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - [`test_multi_socket_contract_regressions.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_multi_socket_contract_regressions.cpp)
  - 위 다섯 파일은 bench A/B 확인 뒤 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_socket_with_handler|test_pubsub_filter_xpub|test_monitor_perf_contract)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_match_all_public`
  - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_match_all_guardrail_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_match_all_guardrail_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_match_all_broader_single`
  - 원복 후 `cmake --build core/build -j$(nproc) --target test_multi_socket_contract_regressions test_socket_with_handler test_pubsub_filter_xpub test_monitor_perf_contract`
  - 원복 후 `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_socket_with_handler|test_pubsub_filter_xpub|test_monitor_perf_contract)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_040146_codex_20260328_xpub_match_all_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_040146_codex_20260328_xpub_match_all_public.txt)
  - [`perf_linux_20260328_040232_codex_20260328_xpub_match_all_guardrail_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_040232_codex_20260328_xpub_match_all_guardrail_public.txt)
  - [`perf_linux_20260328_040313_codex_20260328_xpub_match_all_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_040313_codex_20260328_xpub_match_all_guardrail_raw.txt)
  - [`perf_linux_20260328_040359_codex_20260328_xpub_match_all_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_040359_codex_20260328_xpub_match_all_broader_single.txt)
  - multi smoke는 Python runner가 새 report 파일을 남기지 않았고 콘솔 결과만 확인했다.
- 핵심 수치
  - isolated single `PUBSUB` public
    - `tcp 64B`: `-22.28%`
    - `inproc 64B`: `-43.96%`
  - multi smoke `pubsub tcp 64B`
    - `5725.08 Kmsg/s` vs `4488.36 Kmsg/s`, `-21.60%`
  - raw/public guardrail
    - public `PAIR tcp/inproc`: `-18.71%` / `-22.18%`
    - public `DEALER_DEALER tcp/inproc`: `-15.66%` / `-23.31%`
    - raw `PAIR tcp/inproc`: `-35.42%` / `-15.84%`
    - raw `DEALER_DEALER tcp/inproc`: `-12.05%` / `-35.25%`
  - broader single acceptance
    - `PAIR tcp/inproc`: `-16.32%` / `-31.54%`
    - `PUBSUB tcp/inproc`: `-30.53%` / `-42.65%`
    - `DEALER_DEALER tcp/inproc`: `-17.46%` / `-27.16%`
    - `DEALER_ROUTER tcp/inproc`: `-19.95%` / `-36.34%`
    - `ROUTER_ROUTER tcp/inproc`: `-57.55%` / `-25.46%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `XPUB` all-attached empty-prefix `send_to_all()` fast path
    - 해당 회귀 테스트/attached-pipe helper 전부
- 해석
  - empty-prefix trie-match 제거는 isolated `PUBSUB tcp`와 multi `pubsub tcp`
    에서는 반응했지만, `PUBSUB inproc`은 `-43.96%` / broader single
    `-42.65%`로 여전히 큰 gap이 남았다.
  - broader single 기준 `PUBSUB tcp`도 `-30.53%`라서
    current code의 `-30.71%` 대비 keep-worthy broad win이라고 보기 어렵다.
  - 즉 현재 `PUBSUB` 잔여 gap의 본체를 empty-prefix match 비용 하나로
    설명할 수는 없고, publication/lifecycle differential이 더 직접적인 축이다.
- 다음 iteration 우선순위
  - `XPUB` empty-prefix all-attached fast path는 다시 시도하지 않는다.
  - `PUBSUB`은 trie match elimination이 아니라 publication/lifecycle 잔여축을
    더 직접 줄이는 후보를 본다.
  - 공통 축이 더 남지 않으면 guide 순서대로 `pipe` send/publication
    ordering/work 축소와 `ROUTER_ROUTER` 전용 differential 정리로 넘어간다.

## 27. 2026-03-28 `XPUB` single-subscriber ready-count fast path 로그

- 작업한 가설
  - `XPUB`의 single-subscriber wakeup path는 monitor가 없더라도
    delivery-ready count를 다시 계산한다.
  - single attached pipe에서는 이 ready-count bookkeeping을 더 싸게 만들면
    `PUBSUB` publication/wakeup cost를 조금 더 줄일 수 있다.
- 수정한 파일 경로
  - [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
  - [`xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - 위 세 파일은 bench A/B 확인 뒤 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_monitor_perf_contract|test_monitor_enhanced|test_monitor_socket_contract)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_socket_with_handler|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_single_subscriber_readycount_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_single_subscriber_readycount_fastpath_rerun_seq`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_042325_codex_20260328_pubsub_single_subscriber_readycount_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_042325_codex_20260328_pubsub_single_subscriber_readycount_fastpath.txt)
  - [`perf_linux_20260328_042357_codex_20260328_pubsub_single_subscriber_readycount_fastpath_rerun_seq.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_042357_codex_20260328_pubsub_single_subscriber_readycount_fastpath_rerun_seq.txt)
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3216.78 Kmsg/s` vs `2373.33 Kmsg/s`, `-26.22%`
    - `PUBSUB inproc 64B`: `3572.34 Kmsg/s` vs `2203.87 Kmsg/s`, `-38.31%`
  - rerun
    - `PUBSUB tcp 64B`: `3240.18 Kmsg/s` vs `2303.87 Kmsg/s`, `-28.90%`
    - `PUBSUB inproc 64B`: `3662.76 Kmsg/s` vs `2090.81 Kmsg/s`, `-42.92%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - single-subscriber ready-count bookkeeping fast path 전체
- 해석
  - clean first/rerun 모두 current accepted baseline보다 낫지 않았다.
  - 따라서 `PUBSUB` 잔여 gap의 본체를 delivery-ready ready-count
    bookkeeping으로 읽는 것은 맞지 않다.
  - monitor-ready bookkeeping은 current code 기준으로도 secondary candidate로
    유지한다.

## 28. 2026-03-28 `XPUB` single attached empty-prefix match fast path 로그

- 작업한 가설
  - current no-topic `PUBSUB` single bench는 `zlink_publish(NULL, ...)`라서
    `XPUB`가 payload를 기준으로 `_subscriptions.match()`를 다시 돈다.
  - attached pipe가 정확히 하나이고 그 pipe가 empty-prefix subscription을
    가진 steady-state면, generic trie `match()`를 건너뛰고
    기존 `dist.match()/send_to_matching()` 경로만 쓰면
    `PUBSUB` single-subscriber publication cost를 더 줄일 수 있다.
- 수정한 파일 경로
  - [`xpub.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.hpp)
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - 위 두 파일은 bench A/B 확인 뒤 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_monitor_perf_contract|test_monitor_enhanced|test_monitor_socket_contract)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_socket_with_handler|test_stream_send_blocking_wakeup)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_single_empty_prefix_pipe_fastpath`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_xpub_single_empty_prefix_pipe_fastpath_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_042724_codex_20260328_xpub_single_empty_prefix_pipe_fastpath.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_042724_codex_20260328_xpub_single_empty_prefix_pipe_fastpath.txt)
  - [`perf_linux_20260328_042751_codex_20260328_xpub_single_empty_prefix_pipe_fastpath_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_042751_codex_20260328_xpub_single_empty_prefix_pipe_fastpath_rerun.txt)
- 핵심 수치
  - first run
    - `PUBSUB tcp 64B`: `3202.69 Kmsg/s` vs `2442.46 Kmsg/s`, `-23.74%`
    - `PUBSUB inproc 64B`: `3611.05 Kmsg/s` vs `2286.97 Kmsg/s`, `-36.67%`
  - rerun
    - `PUBSUB tcp 64B`: `3702.91 Kmsg/s` vs `2548.99 Kmsg/s`, `-31.16%`
    - `PUBSUB inproc 64B`: `4094.81 Kmsg/s` vs `2142.69 Kmsg/s`, `-47.67%`
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - single attached empty-prefix match fast path 전체
- 해석
  - first run만 보면 `tcp`와 `inproc` 둘 다 current code보다 좋아 보였지만,
    clean rerun에서 둘 다 다시 무너졌다.
  - 따라서 current `PUBSUB` 잔여 gap을
    "single attached pipe + empty-prefix trie match" 하나로 설명할 수는 없다.
  - empty-prefix trie match elimination은 all-attached 변형뿐 아니라
    더 좁은 single-attached 변형도 current code 기준으로 keep-worthy broad win이
    아니었다.
- 다음 iteration 우선순위
  - `PUBSUB`은 delivery-ready bookkeeping이나 empty-prefix trie match
    elimination이 아니라 publication/wakeup differential 자체를 더 직접적으로
    줄이는 후보를 본다.
  - keep-worthy 공통 send-side delta가 더 나오지 않으면
    guide 순서대로 `pipe` send/publication ordering/work 축소와
    `ROUTER_ROUTER` 전용 differential 정리로 넘어간다.

## 29. 2026-03-28 `ROUTER` mandatory-HWM 회귀 보강과 rejected candidate 로그

- 작업한 가설
  - `ROUTER` public routed send는 prefix frame 해석 뒤
    `check_write_status()`와 `write()`가 사실상 같은 HWM/ready 확인을
    두 번 밟는다. `check_write_status()`가 성공한 pipe에 한해
    `write_no_hwm_check()+flush()`로 내리면 `ROUTER_ROUTER` public send gap을
    조금 더 줄일 수 있다고 봤다.
  - `ROUTER` public routed recv는 direct fast path에서도
    `zlink_routing_id_t` 전체를 중복 zero-fill한다.
    이 256B clear 하나를 줄이면 routed recv 쪽 고정비를 덜 수 있다고 봤다.
- 수정한 파일 경로
  - 유지
    - [`CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/tests/CMakeLists.txt)
    - [`test_router_mandatory_hwm.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_router_mandatory_hwm.cpp)
  - bench A/B 뒤 원복
    - [`pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
    - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
    - [`router.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/router.cpp)
    - [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_public_inproc_multipart_send|test_router_multiple_dealers|test_stream_send_blocking_wakeup)$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^test_router_mandatory_hwm$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_prefix_hwm_skip`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_prefix_hwm_skip_guardrail_public`
  - `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_prefix_hwm_skip_guardrail_raw`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_router_recv_rid_zero_elision`
  - 원복 후 `ctest --test-dir core/build --output-on-failure -R '^(test_router_mandatory_hwm|test_public_inproc_multipart_send|test_router_multiple_dealers|test_stream_send_blocking_wakeup)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_043954_codex_20260328_router_prefix_hwm_skip.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_043954_codex_20260328_router_prefix_hwm_skip.txt)
  - [`perf_linux_20260328_044023_codex_20260328_router_prefix_hwm_skip_guardrail_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_044023_codex_20260328_router_prefix_hwm_skip_guardrail_public.txt)
  - [`perf_linux_20260328_044103_codex_20260328_router_prefix_hwm_skip_guardrail_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_044103_codex_20260328_router_prefix_hwm_skip_guardrail_raw.txt)
  - [`perf_linux_20260328_044628_codex_20260328_router_recv_rid_zero_elision.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_044628_codex_20260328_router_recv_rid_zero_elision.txt)
- 핵심 수치
  - routed send prefix/HWM second-check elimination
    - `ROUTER_ROUTER tcp 64B`: `2740.96 Kmsg/s` vs `1228.10 Kmsg/s`, `-55.19%`
    - `ROUTER_ROUTER inproc 64B`: `3261.50 Kmsg/s` vs `2444.50 Kmsg/s`, `-25.05%`
  - public/raw guardrail
    - public `PAIR tcp/inproc`: `-27.93%` / `-22.52%`
    - public `DEALER_DEALER tcp/inproc`: `-26.16%` / `-24.15%`
    - raw `PAIR tcp/inproc`: `-16.56%` / `-31.94%`
    - raw `DEALER_DEALER tcp/inproc`: `-15.62%` / `-21.03%`
  - routed recv source-rid zero-elision
    - `ROUTER_ROUTER tcp 64B`: `2947.82 Kmsg/s` vs `1228.20 Kmsg/s`, `-58.34%`
    - `ROUTER_ROUTER inproc 64B`: `3660.93 Kmsg/s` vs `2435.64 Kmsg/s`, `-33.47%`
  - 원복 후 회귀
    - `test_public_inproc_multipart_send`
    - `test_router_multiple_dealers`
    - `test_router_mandatory_hwm`
    - `test_stream_send_blocking_wakeup`
    - 네 테스트 모두 다시 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - `test_router_mandatory_hwm` ctest 등록
    - `test_router_send_rid_mandatory_hwm()` 추가로
      `zlink_send_rid()` mandatory-HWM regression coverage 보강
  - 원복
    - `pipe_t::write_and_flush_no_hwm_check()` helper와 ROUTER send fast path
    - direct routed recv source-rid zero-elision helper와 API-side memset 제거
- 해석
  - routed send의 prefix/HWM second-check elimination은
    `ROUTER_ROUTER tcp`를 약간만 줄였고 `inproc`도 baseline 대비 미세 개선에
    그쳤다. broad single acceptance를 설명할 정도의 축은 아니다.
  - routed recv source-rid zero-fill 제거는 zlink 절대 throughput 기준으로도
    거의 움직이지 않았고 결과는 오히려 더 흔들렸다.
  - 따라서 current `ROUTER` 잔여 gap은 prefix/HWM recheck나
    source-rid zero-fill 같은 micro-elision보다 더 큰 routed/public
    differential에서 찾아야 한다.
- 다음 iteration 우선순위
  - 이번에 유지한 것은 ROUTER mandatory-HWM regression surface 강화뿐이다.
  - guide 순서대로 `pipe`/publication 축을 더 보고,
    그다음 `ROUTER_ROUTER` routed/public differential을 다시 좁힌다.

## 30. 2026-03-28 `xwrite_activated()` delivery-ready refresh 제거 로그

- 작업한 가설
  - `XPUB` / `XSUB`의 delivery-ready count는 attach/subscribe/terminate에서
    실제로 바뀌고, `xwrite_activated()`는 current ready-count 정의를
    직접 바꾸지 않는다.
  - 따라서 `xwrite_activated()`에서 반복하는
    `refresh_delivery_ready_state()`를 빼면
    `PUBSUB` publication/wakeup path의 불필요한 recompute를 조금 더 줄일 수
    있다고 봤다.
- 수정한 파일 경로
  - [`xpub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xpub.cpp)
  - [`xsub.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/xsub.cpp)
  - 위 두 파일은 bench A/B 확인 뒤 모두 원복했다.
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_skip_delivery_ready_refresh_on_write_activated`
  - 원복 후 `cmake --build core/build -j$(nproc)`
  - 원복 후 `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_045824_codex_20260328_pubsub_skip_delivery_ready_refresh_on_write_activated.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_045824_codex_20260328_pubsub_skip_delivery_ready_refresh_on_write_activated.txt)
- 핵심 수치
  - `PUBSUB tcp 64B`: `3297.58 Kmsg/s` vs `2397.06 Kmsg/s`, `-27.31%`
  - `PUBSUB inproc 64B`: `3915.21 Kmsg/s` vs `2156.23 Kmsg/s`, `-44.93%`
  - 회귀 테스트
    - `test_monitor_socket_contract`
    - `test_monitor_perf_contract`
    - `test_multi_socket_contract_regressions`
    - `test_public_inproc_multipart_send`
    - `test_pubsub_filter_xpub`
    - 후보 적용 상태와 원복 상태 모두 다시 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - 없음
  - 원복
    - `XPUB` / `XSUB` `xwrite_activated()` delivery-ready refresh 제거
- 해석
  - write re-activation에서 ready-count recompute를 빼는 것만으로는
    current `PUBSUB` 잔여 gap을 줄이지 못했고, single acceptance는
    baseline보다 오히려 악화됐다.
  - 즉 current `PUBSUB` 잔여 gap은
    `xwrite_activated()` monitor-ready refresh 하나를 지우는 수준보다
    더 큰 publication/wakeup differential에 있다.
- 다음 iteration 우선순위
  - 이 후보는 다시 시도하지 않는다.
  - guide 순서대로 `pipe`/publication 축의 다른 실제 cost 후보를 다시 본다.

## 31. 2026-03-28 `dist` 전용 non-recursive HWM check 로그

- 작업한 가설
  - generic `check_hwm_locked()` rollout은 `PAIR`/raw guardrail 때문에
    유지하지 못했지만, 실제 publication hot path는 `dist_t::write_at()` 쪽이다.
  - `PUBSUB` path에만 한정해 `pipe`의 recursive HWM self-reentry를 줄이면
    generic rollout의 부작용 없이 publication cost를 더 줄일 수 있다고 봤다.
- 수정한 파일 경로
  - [`pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  - [`pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  - [`dist.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dist.cpp)
- 실행한 명령
  - `cmake --build core/build -j$(nproc)`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_perf_contract|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_public_inproc_multipart_send)$' -j1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_no_recursive_hwm`
  - `python3 core/bench/with_zmq/single/run_comparison.py --pattern PUBSUB --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_no_recursive_hwm_rerun`
  - `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py pubsub --build-dir core/build --runs 1`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_no_recursive_hwm_broader_single`
  - `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_pubsub_dist_no_recursive_hwm_broader_single_rerun`
- 생성된 결과 파일 경로
  - [`perf_linux_20260328_050237_codex_20260328_pubsub_dist_no_recursive_hwm.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_050237_codex_20260328_pubsub_dist_no_recursive_hwm.txt)
  - [`perf_linux_20260328_050313_codex_20260328_pubsub_dist_no_recursive_hwm_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_050313_codex_20260328_pubsub_dist_no_recursive_hwm_rerun.txt)
  - [`perf_linux_20260328_050417_codex_20260328_pubsub_dist_no_recursive_hwm_broader_single.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_050417_codex_20260328_pubsub_dist_no_recursive_hwm_broader_single.txt)
  - [`perf_linux_20260328_050636_codex_20260328_pubsub_dist_no_recursive_hwm_broader_single_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_050636_codex_20260328_pubsub_dist_no_recursive_hwm_broader_single_rerun.txt)
  - multi smoke는 Python runner가 새 report 파일을 남기지 않았고 콘솔 결과만 확인했다.
- 핵심 수치
  - isolated first run
    - `PUBSUB tcp 64B`: `3222.16 Kmsg/s` vs `2392.07 Kmsg/s`, `-25.76%`
    - `PUBSUB inproc 64B`: `3862.32 Kmsg/s` vs `2321.95 Kmsg/s`, `-39.88%`
  - isolated rerun
    - `PUBSUB tcp 64B`: `3400.83 Kmsg/s` vs `2738.29 Kmsg/s`, `-19.48%`
    - `PUBSUB inproc 64B`: `3724.72 Kmsg/s` vs `2260.46 Kmsg/s`, `-39.31%`
  - multi smoke `pubsub tcp 64B`
    - `5211.95 Kmsg/s` vs `4344.38 Kmsg/s`, `-16.65%`
  - broader single rerun acceptance
    - `PAIR tcp/inproc`: `-18.89%` / `-17.22%`
    - `PUBSUB tcp/inproc`: `-23.63%` / `-39.84%`
    - `DEALER_DEALER tcp/inproc`: `-24.09%` / `-27.90%`
    - `DEALER_ROUTER tcp/inproc`: `-27.28%` / `-27.07%`
    - `ROUTER_ROUTER tcp/inproc`: `-54.97%` / `-30.77%`
  - 회귀 테스트
    - `test_monitor_socket_contract`
    - `test_monitor_perf_contract`
    - `test_multi_socket_contract_regressions`
    - `test_public_inproc_multipart_send`
    - `test_pubsub_filter_xpub`
    - 모두 통과
- 유지한 변경 / 원복한 변경
  - 유지
    - `pipe`의 non-recursive HWM helper를 `dist` publication path에만 적용
  - 원복
    - 없음
- 해석
  - generic helper rollout은 broad win이 아니었지만,
    `dist` 전용 좁은 적용은 isolated first/rerun, multi `pubsub`,
    broader single rerun까지 current accepted baseline을 넘겼다.
  - 따라서 current accepted 해석은
    "`pipe` self-reentry 제거를 전역으로 밀자"가 아니라
    "`PUBSUB` publication path에서만 실제로 반복되는 lock 안 work를 줄이자"다.
- 다음 iteration 우선순위
  - 이 delta는 유지한다.
  - 다음은 guide 순서대로 남은 send-side lifecycle/backpressure 공통 축을
    다시 보고, 그다음 `PUBSUB`의 publication/distribution 잔여 gap과
    `ROUTER_ROUTER` 전용 differential을 이어서 줄인다.
