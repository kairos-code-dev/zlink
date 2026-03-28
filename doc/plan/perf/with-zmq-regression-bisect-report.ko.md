# `with_zmq single` 성능 회귀 원인 레포트

## 1. 요약

- first structural bad candidate:
  `9b91234c1cd1f8fc8b2766e584e61eec35fd3adb`
- first buildable bad commit:
  `77550a0aa1ce05d8f9f7f38c2b026338b3925e45`
- last good commit:
  `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- 핵심 결론:
  - `2026-03-05` good 상태에서는 `with_zmq single`의 zlink 쪽이
    `send_exact()` + `zlink_msg_recv()` 중심의 얇은 raw path를 탔다.
  - 이후 `3/12~3/27` 사이에는 direct-callback / recv-first 전환 과정에서
    fixed `with_zmq single` comparison surface 자체가 깨져 직접 측정이
    불가능한 skip 구간이 생겼다.
  - 다만 skip 구간 안에서도 hot path를 바꾼 실제 코드는 확인된다.
    - `ff0140e5`:
      `pipe_t::check_write()/write()/flush()`에 `_out_sync` lock 추가
    - `a819ea3a`:
      `socket_base_t::send()`에 `enter_public_api()` +
      `lock_public_api_sync()` 추가
    - `98e7d324`:
      public `zlink_send/zlink_recv`를 bytes API에서 multipart API로 교체
  - fixed surface가 다시 buildable해진 첫 commit이
    `77550a0a`이고, 이 시점 수치는
    `PAIR/DEALER_DEALER 64B tcp/inproc`에서 `-70%~-84%`로 이미 심각한 bad다.
  - first structural bad는 `9b91234c`다.
    이 commit에서 zlink `PAIR`/`DEALER_DEALER` bench가 raw
    `send_exact()` + `zlink_msg_recv()`에서
    `zlink_msg_init_size()+memcpy()+zlink_send()` +
    `zlink_recv()`로 실제 호출 경로를 바꿨다.
    동시에 `PERF_SINGLE_MAX_INFLIGHT` limiter도 제거되어
    one-way sender pacing regime 자체가 더 aggressive해졌다.
  - 이때 새로 붙은 per-message 비용은 검증된 코드 기준으로
    `send-side msg materialization + public send admission +
    pipe write lock`, `recv-side multipart export + malloc/free`다.
    first bad commit 코드에는 `second frame clone` 같은 추가 단계는 확인되지
    않았다.
  - 즉 historical first collapse는
    `9b91234c`의 surface switch 하나로 설명되는 것이 아니라,
    `ff0140e5` + `a819ea3a` + `98e7d324`로 이미 들어와 있던 core/public
    고정비를 `9b91234c`가 PAIR/DEALER 64B one-way hot loop에서
    실제로 타기 시작하고, sender pacing regime 변화까지 겹친 결과다.
  - `77550a0a` 자체는 위 전환을 새로 만든 commit이라기보다,
    `bench_common.hpp` alias와 `std_compat/zlink.h`를 추가해
    깨진 comparison surface를 다시 buildable하게 만든 복구 경계에 가깝다.
  - multipart 배열 export/free 이슈를 나중에 줄였더라도,
    그 변경과 겹치지 않는 send-side 원인은 남는다.
    `a819ea3a`의 `socket_base_t::send()` public admission/CAS와
    `ff0140e5`의 `pipe::_out_sync`는 그 수정과 별개다.

## 2. 측정 기준

- 기준 패턴:
  `PAIR`, `DEALER_DEALER`
- 메시지 크기:
  `64B`
- transport:
  `tcp`, `inproc`
- 실행 명령:

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag <tag>
```

- 결과 파일:
  - good:
    `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_141555_bisect_good_7bea9e3f_rerun.txt`
  - first buildable bad:
    `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_142205_bisect_77550a0a.txt`
  - current bad anchor:
    `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_141312_bisect_head_95d8a3b2.txt`
  - current bad public rerun:
    `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_144711_bisect_head_95d8a3b2_rerun_public.txt`
  - current bad raw-msg diagnostic:
    `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_144625_bisect_head_95d8a3b2_rawmsg.txt`
  - current bad raw-msg diagnostic rerun:
    `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_144757_bisect_head_95d8a3b2_rawmsg_rerun.txt`

## 3. 회귀 구간

- good commit:
  `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- first structural bad candidate:
  `9b91234c1cd1f8fc8b2766e584e61eec35fd3adb`
- first buildable bad commit:
  `77550a0aa1ce05d8f9f7f38c2b026338b3925e45`
- 좁혀진 culprit 구간:
  - `7bea9e3f` 이후 `3/12~3/27` 사이에 구조 변화가 누적됨
  - 다만 `376347fd`, `9b91234c`를 포함한 중간 구간은 fixed comparison
    surface가 unbuildable이라 direct good/bad 판정 불가
  - `9b91234c`는 zlink bench가 thin raw path를 버리고 public
    `zlink_send/zlink_recv`로 이동한 첫 commit이다
  - `b7a68af7`까지는 same comparison surface가 여전히 unbuildable이며,
    실제 실패 원인은 `send_exact` helper 누락뿐 아니라
    `zlink_set_option/zlink_get_option`,
    `ZLINK_OPT_TLS_*`, `ZLINK_OPT_LAST_ENDPOINT` alias 부재까지 포함한다
  - 따라서 현재 판정은
    `last good = 7bea9e3f`
    / `first structural bad = 9b91234c`
    / `first buildable bad = 77550a0a`
    로 분리해서 적는 것이 가장 정확하다
  - first measurable bad는 `77550a0a`
  - first surface-activation commit은 `9b91234c`
  - first core-side hot-path changes are:
    `ff0140e5` -> `a819ea3a` -> `98e7d324`

## 4. 실제 악화 시점 코드 변화 지도

- `ff0140e5` (`2026-03-12`)
  - `pipe.hpp`에 `fast_mutex_t _out_sync`가 추가됐다.
  - `pipe.cpp`의 `check_read/read/check_write/write/rollback/flush`가
    lock 아래로 이동했다.
  - 즉 message 1건당 내려가는 `pipe` write/read 경로가 owner-thread
    가정의 얇은 경로에서 serialized 경로로 바뀌었다.

- `a819ea3a` (`2026-03-15`)
  - `socket_base_t::send()`가
    `xsend(msg_)` 직호출에서
    `enter_public_api()` +
    `lock_public_api_sync(); xsend(msg_); unlock_public_api_sync();`
    구조로 바뀌었다.
  - 즉 send 1회마다 inflight state atomic과 public-api CAS lock이 추가됐다.

- `98e7d324` (`2026-03-18 12:57`)
  - public header의 `zlink_send/zlink_recv` 시그니처가
    bytes API에서 multipart API로 바뀌었다.
    - before:
      `zlink_send(void*, const void*, size_t, flags)`
      / `zlink_recv(void*, void*, size_t, flags)`
    - after:
      `zlink_send(void*, zlink_msg_t*, size_t, flags)`
      / `zlink_recv(void*, rid*, zlink_msg_t**, size_t*, flags)`
  - `send_socket_parts()` / `recv_socket_parts()`가 도입되고,
    `zlink_msg_send/zlink_msg_recv`는 compat 역할로 밀렸다.

- `9b91234c` (`2026-03-18 18:13`)
  - `bench_zlink_pair.cpp`, `bench_zlink_dealer_dealer.cpp`의 inner loop가
    실제로 새 public multipart API를 타도록 바뀌었다.
  - send:
    `send_exact(payload)` ->
    `zlink_msg_init_size(&part) + memcpy + zlink_send(&part, 1, 0)`
  - recv:
    `zlink_msg_recv(&msg)` ->
    `zlink_recv(&source_rid, &parts, &part_count, flags)` +
    `zlink_multipart_close(parts, part_count); free(parts);`
  - 동시에 `PERF_SINGLE_MAX_INFLIGHT` limiter가 제거됐다.
  - 이 commit이 first structural bad인 이유는,
    앞선 `ff0140e5`/`a819ea3a`/`98e7d324`의 새 비용을
    `PAIR`/`DEALER_DEALER` 64B one-way hot loop가 처음으로 직접 밟기
    시작했기 때문이다.

- `77550a0a` (`2026-03-27`)
  - `bench_common.hpp` alias와 `std_compat/zlink.h`가 추가되어
    fixed comparison surface가 다시 buildable해졌다.
  - 회귀를 만든 commit이라기보다 already-bad 상태를 다시 관측 가능하게 만든
    first buildable bad 경계다.

## 5. 성능 저하 원인 분석

### 5.1 실제 성능 저하를 만든 코드 변화

- `7bea9e3f` good 경로:
  - send:
    `send_exact(sender, payload->data(), payload_size, 0)`
  - recv:
    caller-owned `zlink_msg_t msg` +
    `zlink_msg_recv(&msg, socket, flags)` +
    `zlink_msg_close(&msg)`

- `9b91234c` structural bad 경로:
  - send:
    `zlink_msg_init_size(&part, payload_size)` +
    `memcpy(zlink_msg_data(&part), payload->data(), payload_size)` +
    `zlink_send(sender, &part, 1, 0)`
  - recv:
    `zlink_recv(socket, &source_rid, &parts, &part_count, flags)` +
    `zlink_multipart_close(parts, part_count)` +
    `free(parts)`

- 위 변경이 의미하는 실제 추가 비용:
  - send:
    payload 1건마다 `msg_init_size + memcpy`가 추가된다.
  - send:
    그 다음 `socket_base_t::send()`가 이미 `a819ea3a`에서 추가된
    `enter_public_api()`와 `lock_public_api_sync()`를 매 프레임마다 탄다.
  - send:
    그 아래 `pipe_t::write()/flush()`는 이미 `ff0140e5`에서 추가된
    `_out_sync` lock을 매 프레임마다 탄다.
  - recv:
    `recv_socket_parts()`가 `std::vector<zlink_msg_t>`에 프레임을 모은 뒤
    `malloc()`으로 caller-owned array를 export한다.
  - recv:
    bench는 매 메시지마다 `zlink_multipart_close()+free()`를 호출한다.

- 따라서 historical first collapse를 만든 것은 단일 원인이 아니다.
  - `ff0140e5`: core pipe serialization 추가
  - `a819ea3a`: send public admission/CAS 추가
  - `98e7d324`: public multipart contract 도입
  - `9b91234c`: bench가 그 경로를 실제 측정 loop에서 사용하기 시작하고,
    `PERF_SINGLE_MAX_INFLIGHT` 제거로 sender pacing regime도 함께 변경

### 5.2 multipart 배열 할당 수정과 겹치지 않는 원인

- 겹치는 축:
  - `98e7d324`/`9b91234c` recv path의 `parts_out` export,
    `malloc/free`, multipart marshalling
- 겹치지 않는 축:
  - `a819ea3a`의 `socket_base_t::send()` public admission/inflight atomic
  - `a819ea3a`의 `lock_public_api_sync()` CAS lock
  - `ff0140e5`의 `pipe::_out_sync` write/flush serialization
  - `9b91234c` send path의
    `send_exact(buffer alias)` -> `msg_init_size + memcpy + zlink_send`
    전환

- 즉 multipart 배열 할당 이슈를 줄였더라도,
  send-side에서는 여전히 별개의 고정비가 남는다.
  이 부분은 recv export 최적화와 독립적인 historical code change다.

### 5.3 현재 HEAD에서 무엇이 아직 남아 있는가

- 추가 진단:
  - current HEAD `95d8a3b2`에서
    `PERF_SINGLE_ZLINK_RAW_MSG_API=1` 보조 측정을 수행했다.
  - 이 토글은 recv를 `zlink_msg_recv`로, send를 `zlink_msg_send`로 강제한다.
  - 결과는 public default 대비 일관된 개선을 보이지 않았다.
    - public rerun:
      `PAIR tcp -8.05%`, `PAIR inproc -20.45%`,
      `DEALER_DEALER tcp -16.61%`, `DEALER_DEALER inproc -18.93%`
    - raw-msg diag #1:
      `PAIR tcp -21.72%`, `PAIR inproc -16.40%`,
      `DEALER_DEALER tcp -21.31%`, `DEALER_DEALER inproc -18.58%`
    - raw-msg diag #2:
      `PAIR tcp -25.14%`, `PAIR inproc -24.58%`,
      `DEALER_DEALER tcp -18.00%`, `DEALER_DEALER inproc -20.63%`

- 이 진단이 말해주는 것:
  - current HEAD의 `PAIR`/`DEALER_DEALER` one-way hot path에서는
    public recv가 이미 `recv_tls_view` fast path를 타고,
    public send도 single-part fast path면 `socket->send()`로 직접 내려간다.
  - 따라서 **예전 `parts_out` heap alloc/export 문제는
    현재 residual gap의 주원인이 아니다**.
  - 다만 이것은 current residual 해석일 뿐이고,
    historical first collapse를 만든 실제 코드 변화 지도는
    위 `ff0140e5` / `a819ea3a` / `98e7d324` / `9b91234c`가 더 직접적이다.

- 현재 residual direct cause:
  - `socket_base_t::send()`의 public admission/serialization
    - current `socket_base_t::send()`는
      `socket_public_api_scope_t`와
      `socket_public_api_lock_scope_t`를 매 호출마다 거친다
    - `7bea9e3f`의 `socket_base_t::send()`에는 이 admission/lock 계층이 없었다
  - `pipe_t` hot path의 `_out_sync`
    - `check_write`, `write`, `flush` 등이 lock 아래에 있다
    - libzmq는 같은 위치에 대응 lock이 없다
  - raw/public 토글과 무관하게 남는 비용이므로,
    current diagnostic에서 gap이 사라지지 않는 현상과 맞는다

### 5.4 libzmq 대응 구현과의 차이

- zlink:
  - `ff0140e5` 이후 `pipe_t` hot path에 `_out_sync` mutex가 존재한다.
  - `a819ea3a` 이후 `socket_base_t::send()`는
    `enter_public_api()`와 `lock_public_api_sync()`를 탄다.
  - `98e7d324` 이후 public send/recv는 multipart contract다.
  - `9b91234c` 이후 zlink bench는 single-part one-way를
    public `zlink_send` / `zlink_recv` 경로로 보낸다.
- libzmq:
  - `/home/hep7/project/kairos/libzmq/src/pipe.cpp`의
    `check_read/read/check_write/write/flush`에는 zlink의 `_out_sync`에
    대응하는 hot-path mutex가 없다.
  - `/home/hep7/project/kairos/libzmq/src/socket_base.cpp`는
    non-thread-safe socket에서 thin path를 유지한다.
  - `77550a0a`의 libzmq bench는 `send_exact` / `zlink_msg_recv` raw path를
    유지한다.
- 차이 해석:
  - zlink는 더 비싼 public/lifecycle/serialization 구조를 타고,
    libzmq는 여전히 얇은 raw path와 lock-free pipe path를 타기 때문에
    same one-way steady state에서 per-message 고정비 차이가 크게 벌어진다.

### 5.5 왜 `oneway`에서 더 크게 드러나는가

- 해석:
  - `oneway`는 sender가 receiver보다 계속 앞서가므로,
    send/publication/admission 비용이 message 1건마다 그대로 누적된다.
  - 여기서 드러나는 비용은
    `pipe` write/flush/hwm check,
    public API send/recv marshaling,
    lifecycle admission/close coordination이다.
  - 특히 send 쪽은 `socket_base_t::send()`가 public admission/lock을 잡는 반면,
    recv 쪽은 admission scope보다 `parts_out` marshalling/heap return 비용이
    중심이므로, steady-state one-way에서는 producer-side send 비용이 먼저 크게
    드러난다.
  - 반면 `echo`는 round-trip pacing 때문에 sender가 자연스럽게 제어되어
    같은 고정비가 덜 노출된다.
  - 그래서 current evidence는 recv-only 병목보다
    `producer-side one-way send/publication` 고정비 해석과 잘 맞는다.

## 6. 요소별 분류

### 6.1 thread-safe socket / lifecycle

- first historical change는 `a819ea3a`다.
- `socket_base_t::send()`에 `enter_public_api()`와
  `lock_public_api_sync()`가 들어가며,
  send 1회당 inflight atomic + CAS lock 비용이 붙었다.
- later POSD/refactor commit들은 current residual 구조를 정리한 것이지,
  first collapse의 최초 도입점은 아니다.

### 6.2 callback / dispatch

- direct-callback / recv-first 전환 자체가 `3/12~3/27`의 build-broken 구간을
  만들었다.
- 다만 `PAIR`/`DEALER_DEALER` 64B one-way의 공통 격차 본체를
  callback dispatch alone으로 보기는 어렵다.

### 6.3 recv-first / public surface

- 가장 직접적인 축이다.
- `98e7d324`에서 public API가 multipart contract로 바뀌고,
  `9b91234c`에서 zlink bench가 thin raw path에서 그 public path로 이동했다.
- 이 변경의 실체는
  `single-frame raw send/recv`를
  `multipart/public send/recv contract`로 바꾸면서
  `msg materialize + heap-return marshalling`을 매 메시지에 강제한 것이다.
- 이 변경이 fixed surface restored 시점의 severe bad를 가장 직접적으로
  설명한다.
- 다만 current HEAD 기준 residual gap의 본체를 여전히 설명하는 것은
  이 예전 recv export 비용보다
  `send-side public admission + pipe serialization` 쪽이다.

### 6.4 pipe / publication / serialization

- `ff0140e5`에서 `_out_sync`가 pipe hot path에 들어갔다.
- 이는 public-surface mismatch와 별개로 core-side steady-state send 비용을
  높인 증폭 축이다.

### 6.5 pattern-specific path

- `PAIR`와 `DEALER_DEALER`가 함께 무너진다.
- 따라서 현재 1차 원인은 pattern-specific helper가 아니라
  공통 send/public surface + core serialization 축으로 본다.

## 7. 최종 판정

- 실제 악화 시점의 코드 변화:
  - `ff0140e5`:
    `pipe::_out_sync` 추가
  - `a819ea3a`:
    `socket_base_t::send()` public admission/CAS lock 추가
  - `98e7d324`:
    public `zlink_send/zlink_recv` multipart contract 도입
  - `9b91234c`:
    `PAIR`/`DEALER_DEALER` zlink bench가 위 경로를 실제 hot loop에서 사용
- 따라서 first structural bad는 `9b91234c`다.
- 이 전환이 first buildable restored commit `77550a0a`에서 severe bad로
  관측됐다.
- first measurable bad commit:
  - `77550a0a`
  - 다만 이 commit의 `with_zmq` 관련 역할은
    `bench_common.hpp` alias와 `std_compat/zlink.h` 추가를 통한
    buildability restore가 핵심이다
- multipart 배열 할당 수정과 겹치지 않는 historical 원인:
  - `a819ea3a`의 send admission/lock
  - `ff0140e5`의 pipe serialization
  - `9b91234c`의 send-side `msg_init_size + memcpy + public send` 전환
- 제외된 가설:
  - `PAIR` 또는 `DEALER_DEALER`만의 pattern-local bug가 본체라는 해석
  - callback dispatch alone이 전부라는 해석
  - single micro-helper 하나로 설명되는 회귀라는 해석

## 8. 남은 불확실성

- `3/12~3/27`의 상당 구간은 fixed comparison surface가 unbuildable이어서
  first underlying bad commit을 purely perf-measured way로 확정할 수는 없다.
- 따라서 현재 가장 정확한 결론은
  `last good measurable = 7bea9e3f`,
  `first structural bad = 9b91234c`,
  `first buildable bad = 77550a0a`,
  `historical code-change map = ff0140e5 -> a819ea3a -> 98e7d324 -> 9b91234c`,
  `historical first direct cause = public multipart path activation`,
  `multipart alloc fix와 겹치지 않는 핵심 원인 = send admission + pipe serialization`
  이다.
- 이후 더 좁히려면 build-broken interval 내부를 “buildability boundary”와
  “core change boundary”로 따로 추적해야 한다.

## 9. 기존 gap-review와 겹치지 않는 원인 분리

- 기존
  [`single-libzmq-gap-review.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
  와 겹치는 결론:
  - current residual gap의 상위 축이
    `socket_base_t::send()` public lifecycle/admission과
    `pipe::_out_sync` serialization이라는 해석
  - `echo`보다 `oneway`에서 더 크게 드러나는 이유가
    producer-side steady-state send/publication 비용이라는 해석
  - raw/public wrapper 차이만으로 전체 gap을 설명할 수 없다는 해석

- bisect로 새로 분리된 원인:
  - `9b91234c`가 **historical first structural bad**라는 점
    - 기존 gap-review는 current HEAD 중심 재분석 문서라
      "언제 들어왔는가"를 commit boundary로 고정하진 않았다
    - bisect는 `3/05 good` 이후 성능 붕괴가 이 commit의
      hot-loop surface switch에서 구조적으로 드러났음을 보여준다
  - `77550a0a`가 **회귀 도입 commit이 아니라 first buildable bad**라는 점
    - 즉 `77550a0a`는 culprit 자체라기보다
      깨진 comparison surface를 다시 측정 가능하게 만든 경계다
  - initial collapse의 코드 변화 지도가 따로 있다는 점
    - `ff0140e5`:
      `pipe::_out_sync`
    - `a819ea3a`:
      `socket_base_t::send()` public admission/CAS
    - `98e7d324`:
      public multipart contract
    - `9b91234c`:
      bench hot-loop activation
  - multipart 배열 할당 수정과 겹치지 않는 원인을 따로 분리해야 한다는 점
    - overlap:
      `zlink_recv()`의 `parts_out` export/malloc/free
    - non-overlap:
      `socket_base_t::send()` public admission/CAS,
      `pipe::_out_sync`,
      `msg_init_size + memcpy + zlink_send`
  - 따라서 기존 gap-review에서 진행한 현재 코드 기준 최적화 작업과,
    bisect가 찾아낸 historical regression introduction은
    같은 축을 일부 공유하지만 동일 원인으로 합치면 안 된다

- 현재 문서 기준 최종 분리:
  - historical first cause:
    `9b91234c` public multipart path activation
  - first measurable bad:
    `77550a0a`
  - historical non-overlap cause after recv-array fix:
    `a819ea3a` send admission/CAS
    + `ff0140e5` pipe serialization
