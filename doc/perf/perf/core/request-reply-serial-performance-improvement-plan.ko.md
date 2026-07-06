# core request/reply serial 성능 개선 계획

> 이 문서는 `zlink` C API request/reply의 serial 패턴에서 보이는 낮은 처리량을
> 분석하고 개선하기 위한 계획이다.
>
> 이 문서는 공개 API 계약 문서가 아니다. 아직 확인되지 않은 동작을 spec으로
> 보장하지 않으며, 먼저 측정으로 병목 위치를 분리한 뒤 필요한 계층에서 수정한다.

## 1. 문제 요약

`bindings/c/bench/with_grpc`의 최신 local 비교 결과에서 `zlink` request/reply는
window와 saturation 패턴에서는 gRPC보다 높게 나오지만, serial 패턴에서만 약
`0.92 KOPS` 수준으로 낮다.

기준 리포트:

- 디렉터리: `bindings/c/bench/with_grpc/log/with_grpc_c_20260706_194816_c_with_grpc_patterns/`
- 파일: `with_grpc_c_20260706_194816_c_with_grpc_patterns.txt`

핵심 결과:

| 패턴 | 크기 | 처리량 | 평균 latency | blocked | max outstanding |
|------|------|--------|--------------|---------|-----------------|
| zlink request serial | 1024 | `0.923 KOPS` | `1.082 ms` | `0` | `1` |
| zlink request window | 1024 | `85.876 KOPS` | `1.144 ms` | `0` | `100` |
| zlink request saturation | 1024 | `176.825 KOPS` | `3.183 ms` | `76787` | `646` |
| zlink request serial | 4096 | `0.921 KOPS` | `1.085 ms` | `0` | `1` |
| zlink request window | 4096 | `84.675 KOPS` | `1.168 ms` | `0` | `100` |
| zlink request saturation | 4096 | `116.490 KOPS` | `5.841 ms` | `91167` | `754` |

이 결과에서 중요한 점은 아래와 같다.

- serial 처리량의 역수와 평균 latency가 거의 같다. 요청 하나가 완료될 때까지
  약 `1.08 ms`가 걸린다.
- 1024바이트와 4096바이트 serial 결과가 거의 같으므로 payload copy나 전송량
  자체가 주된 병목이라고 보기 어렵다.
- serial과 window에서는 `blocked`가 `0`이다. backpressure는 saturation에서
  자연스럽게 나타나는 동작이며, 이번 문제의 주 원인으로 보지 않는다.
- 1024바이트 serial의 `submit_wait_ms`는 전체 `24.763 ms`이고 submit 수는
  `2770`이다. submit 한 번당 약 `9 us` 수준이므로 `zlink_dealer_request_part()`
  호출 자체가 `1 ms`를 대부분 소비한다고 보기는 어렵다.
- 같은 실행에서 gRPC request serial은 약 `13 KOPS` 수준이다. 따라서 순수한 OS
  scheduler wakeup 한계만으로 zlink serial의 `0.92 KOPS`를 설명하기 어렵다.

따라서 우선 의심할 경로는 request submit 이후 reply completion을 기다리는 경로다.
특히 core socket command 처리에는 약 `1 ms` 수준의 throttle 정책이 있으므로,
reply가 completion queue에 들어가기 전 activate/read command가 이 정책에 묶이는지
먼저 확인한다.

이 문서는 한 가지 원인을 미리 확정하지 않는다. 확인 대상은 크게 네 구간이다.

1. server가 request를 받고 reply를 다시 보내기까지의 blocking wakeup 구간
2. client I/O thread가 reply frame을 받아 request/reply dispatch를 실행하는 구간
3. completion queue가 internal PAIR socket으로 다시 signal하는 구간
4. application thread의 `zlink_poller_wait()`가 signal을 보고 callback을 drain하는 구간

## 2. 현재 serial 벤치 의미

with_grpc 벤치의 serial request는 한 번에 request 하나만 outstanding 상태로 둔다.
request가 완료되기 전까지 다음 request를 보내지 않는다.

관련 코드:

```text
bindings/c/bench/with_grpc/zlink/bench_zlink_client.cpp
```

- `submit_request_once()`는 payload를 만들고 `zlink_dealer_request_part()`를 호출한다.
- `run_request_serial()`은 request 하나를 보낸 뒤 `outstanding == 0`이 될 때까지
  `poll_once(poller, 50)`을 반복한다.
- `poll_once()`는 `zlink_poller_wait()`를 호출한다.

`50 ms`는 최대 대기 시간이다. completion signal이 정상적으로 poller를 깨우면
즉시 return해야 한다. 그러므로 serial에서 약 `1 ms`가 반복된다면, 단순히 timeout
값이 `50 ms`라서 느린 것은 아니다. completion이 poller를 즉시 깨우지 못하거나,
poller가 깨우기 전에 reply completion이 만들어지는 경로가 지연되는지 확인해야 한다.

기존 `bindings/c/perf/single`의 reqrep 구현은 이 serial 의미와 다르다. single perf의
reqrep requester는 가능한 만큼 request를 계속 넣고, 중간중간 completion을 poll한다.
따라서 `1 ms` 수준의 completion 지연이 있어도 여러 request에 나뉘어 보이며,
with_grpc serial처럼 outstanding 1개에서 병목이 그대로 드러나지 않는다.

## 3. 우선 병목 후보

### 3.1 command polling throttle 때문에 activate_read 처리가 늦는 경로

core socket에는 command 처리 빈도를 제한하는 정책이 있다. 이 정책은 처리량을 위해
command polling을 매번 하지 않고, 일정 시간 안에는 command 확인을 건너뛸 수 있게 한다.

관련 코드:

```text
core/src/runtime/utils/config.hpp
core/src/runtime/sockets/common/socket_base_lifecycle.cpp
core/src/runtime/sockets/common/socket_base_msg.cpp
core/src/runtime/core/pipe.cpp
```

현재 코드에서 `max_command_delay`는 CPU tick 기준이며, 주석은 이 값이 현재 CPU에서
약 `1 - 2 ms`가 될 수 있다고 설명한다. public send hot path는
`send_direct_with_retry()`에서 `process_commands(0, true)`를 호출한다. 여기서 두 번째
인자가 `true`이므로 command poll이 throttle 대상이 된다.

request/reply serial에서는 request 하나를 보낸 뒤 reply completion 하나를 기다린다.
이때 pipe flush가 sleeping reader를 깨워야 하는 상황이면 `activate_read` command가
전달된다. 이 command 처리가 `max_command_delay`에 묶이면 outstanding 1개인 serial에서는
그 지연이 request마다 그대로 latency가 된다. window와 saturation에서는 여러 request가
같은 지연 구간 안에 겹치므로 throughput이 회복될 수 있다.

확인할 질문:

- serial request마다 `process_commands(0, true)`가 command poll을 건너뛰는가?
- reply를 만들거나 completion signal을 전달하는 `activate_read` command가 약 `1 ms`
  뒤에 처리되는가?
- `max_command_delay`를 진단 빌드에서 0으로 줄이면 serial 처리량이 즉시 회복되는가?

수정 계층 후보:

`core/src/runtime/sockets/common`, `core/src/runtime/core/pipe`

#### 3.1.1 코드 추적 결과 — 우선순위 하향

실제로 `activate_read`를 만들고 전달하는 경로를 끝까지 따라가면 이 throttle과는
분리된다는 근거가 나온다.

- `process_commands(0, true)`(`throttle_=true`)를 호출하는 지점은 코드 전체에서
  `core/src/runtime/sockets/common/socket_base_msg.cpp:137`
  (`send_direct_with_retry()` 내부) 단 한 곳뿐이다. 나머지 모든 `process_commands`
  호출(recv/poll 경로 포함, 같은 파일 183/239/315/330/346/385/400/416/455/471/487행)은
  전부 `throttle_=false`다. 즉 이 throttle은 "보내는 소켓이 자신의 관리용
  mailbox(bind/term 등)를 매번 체크할지"만 결정하며, poll/recv 쪽 readiness 확인에는
  적용되지 않는다.
- reply 도착을 상대에게 알리는 `send_activate_read()`는
  `core/src/runtime/core/pipe.cpp:1021-1030`(`pipe_t::flush_unlocked()`)에서
  **매 `write_and_flush()`마다 무조건, 동기적으로** 호출된다(1027-1029행:
  `_out_pipe`가 flush 결과 sleeping이면 즉시 `send_activate_read(_peer)`). 이 호출은
  `process_commands(0, true)` throttle과 무관한 별도 경로다.
- `send_activate_read` → `object_t::send_command`(`core/src/runtime/core/object.cpp:260-267`)
  → `ctx_t::send_command`(`core/src/runtime/core/ctx.cpp:762-767`)는
  `mailbox(tid)->send(command_)`로 상대 mailbox에 즉시 push+signal하는 구조이며,
  이 경로에서도 지연을 만드는 지점은 보이지 않는다.

따라서 "activate_read 처리가 `max_command_delay`에 묶인다"는 가설은 코드 추적만으로는
근거가 약하다. 4.1 실험은 여전히 빠르게 배제하기 위해 돌려볼 가치가 있지만, 결과가
"변화 없음"으로 나올 가능성을 염두에 두고, 3.2/3.3/3.5(특히 아래 3.7)를 먼저 검증하는
순서를 권장한다.

### 3.2 reply 수신 뒤 internal dispatch가 늦게 실행되는 경로

reply completion은 request/reply internal dispatch가 reply frame을 처리한 뒤
completion queue에 넣는다.

관련 코드:

```text
core/src/api/socket/socket_request_reply_dispatch.cpp
```

- pending request 조회
- timeout task cancel
- reply completion decode
- `queue_reply_completion()` 호출

이 경로가 즉시 실행되지 않으면 poller는 completion signal을 받을 수 없다. serial에서는
이 지연이 request마다 그대로 처리량 제한이 된다. window나 saturation에서는 여러 request가
같은 지연 구간 안에 겹치므로 throughput이 회복될 수 있다.

확인할 질문:

- server가 reply를 보낸 뒤 client socket의 internal dispatch가 언제 실행되는가?
- reply frame이 socket에 도착했는데 dispatch가 다음 runtime poll tick까지 밀리는가?
- dispatch 실행 간격에 `1 ms` 수준의 고정 granularity가 있는가?

이 후보는 3.1과 연결해서 본다. reply frame이 도착했는데 socket message dispatch가
늦게 실행된다면, 그 원인이 runtime I/O event 지연인지, socket command progress 지연인지
분리해야 한다.

수정 계층 후보:

`core/src/runtime/sockets`, `core/src/api/socket`

### 3.3 completion queue signal 또는 poller wakeup이 즉시 전달되지 않는 경로

completion queue는 pending queue가 비어 있던 상태에서 새 completion이 들어오면
signal socket에 byte를 쓴다. `ZLINK_POLLCOMPLETION`으로 등록한 poller는 이 hidden
completion signal을 감지하고 completion을 drain한다.

관련 코드:

```text
core/src/api/socket/request_completion_queue_internal.cpp
core/src/api/monitoring/poller_api.cpp
core/src/api/core/zlink.cpp
core/src/api/socket/internal_pair_queue_internal.cpp
```

현재 코드 의도는 completion enqueue 시 poller가 즉시 깨어나는 것이다. 다만 completion
signal은 OS eventfd에 직접 byte를 쓰는 구조가 아니라 internal PAIR socket을 통해 전달된다.
`request_completion::enqueue()`는 `internal_pair_queue::send_buffer_frame()`을 호출하고,
그 내부에서는 다시 `socket_->send()`가 실행된다. 따라서 completion signal 자체도 core
socket send, pipe flush, activate/read wakeup 경로의 영향을 받을 수 있다.

이 말은 completion 전달에 re-signal hop이 하나 더 있다는 뜻이다. 실제 reply readiness가
application poller를 직접 깨우는 것이 아니라, reply dispatch가 completion queue에 결과를
넣고 internal PAIR socket에 1바이트를 보낸 뒤 그 PAIR socket의 readiness를 poller가 본다.
serial에서는 이 추가 hop의 wakeup 비용이 request마다 그대로 latency가 될 수 있다.

반대로 poller는 blocking wait에 들어가기 전 socket readiness를 직접 확인한다. signal byte가
이미 internal PAIR pipe에 들어와 있다면 `has_in()`으로 즉시 감지할 수 있다. 그러므로
completion signal 후보는 "enqueue 이후 poller가 무조건 1 ms 늦는다"가 아니라,
"signal byte가 internal PAIR socket 경로를 지나 poller가 볼 수 있는 상태가 되기까지
지연되는가"로 좁혀서 확인한다.

`zlink_poller_wait()`에는 hidden completion을 drain한 뒤 public event가 없어도 caller에게
return해야 한다는 주석이 있다. 이 주석의 의도는 다음 timeout까지 다시 wait에 들어가
처리량이 `1 / timeout`으로 제한되는 상황을 막는 것이다.

따라서 여기서 확인할 것은 "의도한 wakeup이 실제로 즉시 일어나는가"이다.

확인할 질문:

- `queue_reply_completion()`에서 signal byte를 쓴 시각과 `zlink_poller_wait()`가
  return한 시각 사이가 얼마인가?
- hidden completion event가 관측되었는데 callback drain 뒤 caller로 바로 돌아오지
  않는 경로가 있는가?
- signal socket에 byte가 남거나 `signal_pending` 상태가 꼬여 다음 completion이
  signal을 생략하는 경우가 있는가?

수정 계층 후보:

`core/src/api/socket/request_completion_queue_internal.cpp`,
`core/src/api/socket/internal_pair_queue_internal.cpp`,
`core/src/api/monitoring/poller_api.cpp`

### 3.4 서버 blocking recv wakeup 비용

with_grpc zlink server의 request loop는 `zlink_router_recv_part()`를 blocking으로 호출하고,
request를 받으면 즉시 `zlink_router_reply_part()`로 응답한다. serial 패턴에서는 다음 request가
이전 completion 이후에야 들어오므로 server thread도 매 request마다 완전히 잠들었다가 깨어날
수 있다.

관련 코드:

```text
bindings/c/bench/with_grpc/zlink/bench_zlink_server.cpp
core/src/runtime/core/recv_internal.cpp
```

이 후보는 zlink 고유 completion path만의 문제라기보다 serial 패턴의 구조적 비용일 수 있다.
다만 같은 머신에서 gRPC serial이 훨씬 빠르므로, 서버 blocking wakeup만으로 전체 차이를
설명할 수 있는지는 측정으로 분리한다.

확인할 질문:

- server `zlink_router_recv_part()` return까지의 시간이 `1 ms` 대부분을 차지하는가?
- server reply 완료 뒤 client dispatch 진입까지가 짧은가, 긴가?
- server를 busy-poll 진단 모드로 바꾸면 serial latency가 크게 줄어드는가?

수정 계층 후보:

core recv/wakeup 경로 또는 bench 진단 조건. 단, busy polling은 최종 수정안으로 남기지 않는다.

### 3.5 I/O thread 배치와 inproc re-signal 경계

기본 context의 I/O thread 수는 `ZLINK_IO_THREADS_DFLT=4`다. TCP reply 수신과 socket message
dispatch는 I/O thread와 관련된다. 반면 completion signal에 쓰는 internal PAIR socket은
`inproc://`로 만들어지며, inproc bind/connect 자체는 일반 TCP transport처럼 `choose_io_thread()`
로 session을 만들지 않고 pipepair를 직접 붙인다.

따라서 "internal PAIR rx/tx가 기본 I/O thread 수 때문에 반드시 서로 다른 I/O thread에 배정된다"는
식으로 단정하지 않는다. 대신 아래 가능성을 나누어 확인한다.

- TCP reply를 받은 I/O thread에서 request/reply dispatch가 언제 실행되는가?
- internal PAIR signal send가 application poller가 볼 수 있는 pipe state까지 언제 반영되는가?
- `ZLINK_IO_THREADS=1`과 기본값 4에서 serial latency가 달라지는가?
- socket affinity를 고정했을 때 serial latency가 달라지는가?

수정 계층 후보:

core I/O scheduling, socket dispatch, internal inproc signal 경로

### 3.6 request timeout schedule/cancel 비용

request는 submit 시 timeout task를 등록하고, reply가 도착하면 dispatch 경로에서 cancel한다.
모든 request가 이 경로를 지나므로 비용은 확인해야 한다.

관련 코드:

```text
core/src/api/socket/socket_request_reply_pending_api.cpp
core/src/api/socket/socket_request_reply_dispatch.cpp
core/src/api/socket/request_timeout_scheduler_internal.cpp
```

다만 현재 리포트 기준으로는 이 후보의 우선순위가 낮다. submit 전체 시간이 요청당 약
`9 us` 수준이므로 timeout schedule이 serial의 `1 ms` 대부분을 설명한다고 보기는 어렵다.
그래도 cancel 경로가 scheduler thread와 경합하거나 특정 상황에서 blocking되는지
별도 측정으로 확인한다.

수정 계층 후보:

`core/src/api/socket`, request timeout scheduler

### 3.7 `send_activate_read`의 same-thread inline 최적화 누락

`object.cpp`에서 pipe 활성화 커맨드 두 종류의 처리 방식이 비대칭이다.

```text
core/src/runtime/core/object.cpp
```

```cpp
void zlink::object_t::send_activate_write (pipe_t *destination_, uint64_t msgs_read_)
{
    ...
    if (destination_->get_tid () == _tid)
        destination_->process_command (cmd);   // 같은 스레드면 mailbox 안 거치고 즉시 처리
    else
        send_command (cmd);
}

void zlink::object_t::send_activate_read (pipe_t *destination_)
{
    ...
    send_command (cmd);   // 같은 스레드여도 항상 mailbox 경유
}
```

`send_activate_write`(269-280행)는 송신자·수신자가 같은 `tid`면 mailbox를 거치지
않고 `process_command()`를 바로 호출하는 fast path가 있는데, reply 전달에 직접
쓰이는 `send_activate_read`(260-267행)에는 이 fast path가 없다. request/reply
serial 벤치 토폴로지에서 관련 pipe들이 같은 io_thread(tid)에 배정된다면, 매
reply마다 필요 없는 mailbox 왕복(큐 push + signal + 이후 dequeue)을 타고 있을
수 있다.

확인할 질문:

- 이 벤치 조건에서 reply를 전달하는 pipe의 `destination_->get_tid() == _tid`가
  실제로 참인가?
- 참이라면, `send_activate_write`와 동일한 same-thread inline 처리를
  `send_activate_read`에도 적용했을 때 serial latency가 줄어드는가?

수정 계층 후보:

`core/src/runtime/core/object.cpp`

## 4. 확인 실험

수정 전에 아래 순서로 병목 위치를 분리한다. 실험용 계측은 성능 개선 패치와 분리하고,
계측 결과를 확인한 뒤 제거한다.

### 4.1 command throttle 영향 제거

진단 빌드에서 `max_command_delay`를 0으로 낮추거나, request serial 경로에서
`process_commands(0, true)`가 command poll을 건너뛰지 않도록 임시로 바꾼 뒤
1024바이트 serial만 재측정한다.

판단 기준:

- serial 처리량이 크게 회복되면 `activate_read` 또는 completion signal progress가
  command polling throttle에 묶인 것이다.
- serial 처리량이 그대로 약 `0.92 KOPS`이면 command throttle은 1차 원인이 아니므로
  reply dispatch 이전 또는 poller wakeup 이후 경로를 계속 본다.

이 변경은 진단용이다. 처리량을 위해 command throttle 정책을 무조건 제거하는 방식은
최종 수정안으로 보지 않는다. 실제 수정은 request/reply completion progress에 필요한
command만 지연되지 않게 할 수 있는지, 또는 low-latency 경로를 좁게 둘 수 있는지 별도로
설계한다.

### 4.2 serial completion timeline 계측

with_grpc zlink client에 임시 timestamp를 넣어 아래 시각을 기록한다.

1. `zlink_dealer_request_part()` 호출 직전
2. `zlink_dealer_request_part()` return 직후
3. `zlink_poller_wait()` 진입 직전
4. `zlink_poller_wait()` return 직후
5. reply callback 진입 시각

판단 기준:

- 2번에서 5번까지가 약 `1 ms`이면 completion wait 경로가 병목이다.
- 1번에서 2번까지가 커지면 submit/send 경로를 다시 본다.
- 4번과 5번이 거의 같고 3번에서 4번이 약 `1 ms`이면 poller wakeup 전 단계가 병목이다.

### 4.3 core dispatch와 completion enqueue 계측

core에 임시 trace를 넣어 아래 시각을 비교한다.

1. client request submit 완료
2. server `zlink_router_recv_part()` return
3. server `zlink_router_reply_part()` 완료
4. client `socket_request_reply_dispatch()` reply path 진입
5. `queue_reply_completion()` 호출 직전
6. `request_completion::enqueue()` signal write 직후
7. `pipe_t::flush_unlocked()`에서 completion 내부 PAIR tx에 대해
   `send_activate_read()`를 호출한 시각(`core/src/runtime/core/pipe.cpp:1027-1029`)
8. 그 커맨드가 상대 pipe에서 `process_activate_read()`로 소비된 시각
   (`core/src/runtime/core/pipe.cpp:558-570`)
9. `zlink_poller_wait()` return

판단 기준:

- 1번에서 2번까지가 약 `1 ms`이면 server blocking recv wakeup 또는 request delivery
  경로를 본다.
- 3번에서 4번까지가 약 `1 ms`이면 runtime socket dispatch 또는 transport event
  progress 경로를 본다.
- 6번에서 9번까지가 약 `1 ms`이면 completion signal 또는 poller wakeup 경로를 본다.
  이때 7번-8번 구간이 크면 mailbox 크로스스레드 전달 자체의 비용(3.5, 3.7)이고,
  8번-9번 구간이 크면 completion queue drain 이후 poller 쪽 비용(3.3)이다.
- 4번에서 6번까지가 크면 pending lookup, timeout cancel, reply decode, enqueue 비용을 본다.

추가로 `process_commands(0, true)`가 command poll을 skip한 횟수와 시각을 함께 기록한다.
다만 3.1.1에서 확인했듯 이 throttle은 `send_activate_read` 자체를 게이팅하지 않으므로,
skip 횟수가 반복돼도 그것만으로 3.1을 우선 수정 대상으로 올리지 않는다 — 7번-8번
구간의 실측 지연과 함께 봐야 한다.

### 4.4 poll timeout 영향 제거

진단 빌드에서 serial loop의 `poll_once(poller, 50)`을 `poll_once(poller, 0)` busy poll
형태로 바꿔 같은 조건을 한 번 측정한다.

판단 기준:

- throughput이 크게 오르면 signal wakeup이나 wait blocking 경로가 문제다.
- throughput이 그대로 약 `0.92 KOPS`이면 poll timeout이 아니라 reply dispatch나
  completion 생성 이전 경로가 문제일 가능성이 높다.

이 변경은 진단용이다. hot path에 busy polling을 남기지 않는다.

### 4.5 timeout schedule/cancel 제거 실험

진단 빌드에서 timeout task schedule/cancel을 우회하거나, timeout이 필요 없는 local
실험 경로를 만들어 serial request만 재측정한다.

판단 기준:

- serial latency가 의미 있게 줄면 request timeout 관리 비용을 줄이는 설계를 검토한다.
- 변화가 없으면 timeout 후보를 제외한다.

이 실험은 public timeout 계약을 바꾸기 위한 것이 아니다. 병목 후보를 제거하기 위한
진단이다.

### 4.6 I/O thread 수와 affinity 비교

같은 진단 빌드와 같은 실행 조건에서 `ZLINK_IO_THREADS`를 1과 기본값 4로 바꿔 serial만
비교한다. 가능하면 socket affinity도 고정해 TCP I/O dispatch와 internal signal 경로의
배치 영향을 확인한다.

같은 실험에서 `object_t::send_activate_read()` 호출 시 `destination_->get_tid () == _tid`
여부도 함께 로깅한다(3.7). `IO_THREADS=1`과 4 모두에서 이 조건이 참으로 나오는지,
거짓이면 실제로 mailbox 크로스스레드 경로를 타는지 구분한다.

판단 기준:

- `ZLINK_IO_THREADS=1`에서 serial latency가 크게 줄면 thread 배치나 cross-thread wakeup
  비용을 더 본다.
- 값이 거의 같으면 internal PAIR가 다른 I/O thread에 배정된다는 가설은 약해지고,
  command throttle 또는 inproc pipe signal 자체의 비용을 우선 본다.
- `destination_->get_tid () == _tid`가 참인데도 `send_activate_read`가 매번 mailbox를
  거친다면, 3.7의 same-thread inline 누락이 두 설정 모두에서 공통 비용일 수 있다.

### 4.7 completion signal 직접화 진단

진단 빌드에서 completion queue enqueue 이후 internal PAIR socket 대신 가벼운 직접 wakeup
수단을 임시로 사용해 본다. 예를 들어 eventfd/pipe 기반 raw fd signal이나 condition
variable 기반 진단 경로를 좁게 만들어 serial만 비교한다.

판단 기준:

- 직접 wakeup에서 serial latency가 크게 줄면 internal PAIR re-signal hop이 주요 병목이다.
- 변화가 작으면 completion enqueue 이전 dispatch 또는 server wakeup 경로가 더 유력하다.

이 실험은 public API나 최종 구조를 바로 바꾸기 위한 것이 아니다. internal PAIR signal hop의
비용 상한을 재기 위한 진단이다.

## 5. 수정 원칙

- backpressure는 의도된 flow-control 동작이다. saturation에서 blocked가 많다는 이유로
  문제로 보지 않는다.
- request/reply completion을 빠르게 만들기 위해 public API 의미를 바꾸지 않는다.
- perf 전용 shortcut을 만들지 않는다. 수정은 실제 core runtime 사용자에게도 같은 의미를
  가져야 한다.
- `max_command_delay`나 command polling throttle을 전역으로 제거해 처리량 최적화를
  잃는 방식은 최종 수정안으로 보지 않는다. request/reply completion progress에 필요한
  wakeup만 지연되지 않게 하는 좁은 수정이 가능한지 먼저 검토한다.
- `ZLINK_POLLCOMPLETION`은 public completion progress 경로다. completion을 별도 timer,
  sleep, busy loop로 진행시키는 방식은 perf 정책에 맞지 않는다.
- request timeout 계약을 제거하거나 약하게 만들지 않는다. timeout 관리가 병목으로
  확인되면 같은 계약을 유지하면서 자료구조, lock 범위, cancel fast path를 개선한다.

## 6. 완료 기준

이 작업은 아래 조건을 만족해야 완료로 본다.

1. serial request/reply의 `1 ms` 지연이 어느 구간에서 생기는지 timestamp 근거로 확인한다.
2. command polling throttle, socket message dispatch, completion signal internal PAIR 경로,
   poller wakeup, server blocking recv wakeup 중 어느 구간이 병목인지 분리한다.
3. 원인이 core 계층이면 core 수정으로 개선하고, bench 계층이면 bench의 측정 의미 오류를
   수정한다.
4. `ZLINK_IO_THREADS=1`과 기본값 4에서 병목 구간이 달라지는지 확인한다.
5. gRPC serial 결과와 비교할 때 zlink 고유 completion path 비용인지, serial 구조의 일반
   blocking wakeup 비용인지 구분한다.
6. 1024바이트와 4096바이트 serial을 모두 다시 측정한다.
7. window와 saturation 수치가 의미 있게 나빠지지 않았는지 확인한다.
8. backpressure 동작은 기존 의미를 유지한다.
9. 계측 코드는 최종 패치에 남기지 않는다. 필요한 장기 진단 기능은 별도 debug option으로
   설계한다.

## 7. 다음 작업 순서

3.1.1의 코드 추적 결과에 따라 command throttle 우회 실험의 우선순위를 낮추고, 실제
completion 전달 경로(3.3, 3.5, 3.7)를 먼저 계측하는 순서로 조정한다.

1. with_grpc serial client/server와 core에 임시 timeline 계측을 넣고, submit부터
   callback까지 전 구간(4.3에 추가한 `send_activate_read`/`process_activate_read`
   지점 포함)을 한 trace로 비교한다.
2. `ZLINK_IO_THREADS=1`과 기본값 4를 비교해 thread 배치 영향을 확인하고,
   `send_activate_read` 호출 시 `destination_->get_tid() == _tid` 여부도 함께
   로깅한다(3.7).
3. 필요하면 internal PAIR completion signal을 직접 wakeup 진단 경로로 대체해 re-signal hop
   비용 상한을 잰다.
4. `max_command_delay`를 0으로 낮춘 진단 빌드로 1024바이트 serial만 재측정한다 — 3.1.1의
   코드 추적상 우선순위는 낮지만, 빠르게 배제하기 위한 확인 실험으로 남겨둔다.
5. 필요하면 `poll_once(poller, 0)` 진단으로 wait blocking 영향을 제거한다.
6. 병목 구간이 확정된 뒤에만 core 수정안을 작성한다.
