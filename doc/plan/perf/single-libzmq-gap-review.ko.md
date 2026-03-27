# `single` 성능 격차 코드 리뷰 메모

> 범위: [`core/bench/with_zmq/single/`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single)
> 기준의 `single` 상대 성능에서 `zlink`가 `libzmq`보다 뒤처지는 이유를
> 코드 기준으로 다시 검토한 메모다.
>
> 이 문서는 "지금 보이는 성능 차이를 설명할 만한 구현 차이가 실제로 있는가"
> 에 답하는 것이 목적이다. 구현 변경 계획 자체보다, 어떤 차이가 유력한 원인이고
> 어떤 차이는 원인으로 보기 어려운지를 `libzmq`와 대조해서 정리한다.
>
> 현재 1차 기준은 [`core/bench/with_zmq/single/`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single)
> 의 `recv` 방식 상대 비교다. [`core/perf/single/`](/home/hep7/project/kairos/zlink/core/perf/single)
> 의 callback 기반 `PAIR` 측정은 backpressure 관찰용 보조 신호로만 사용하고,
> root-cause 우선순위는 `zlink_send/zlink_recv` 대 `libzmq` recv 비교에 맞춘다.

## 1. 결론

현재 `single` small-message 성능 격차는 코드상으로 충분히 설명 가능하다.
다만 원인을 읽을 때는 `with_zmq single`이 실제로 타는 API surface를 패턴별로
구분해야 한다. 최근 회귀의 중심은 "thread-safe primitive 자체"보다, 최근 POSD
리팩토링에서 두꺼워진 public message API 계층과 zlink-specific recv/send
bookkeeping 쪽으로 보는 것이 더 정확하다.

핵심은 아래 다섯 가지다.

- 최근 POSD 리팩토링에서 분리된 public `send/recv` message API 계층의 추가 깊이
- `PAIR`/`DEALER` recv마다 수행되는 `last_recv_source_rid` 저장
- recv-mode와 callback/dispatch mode가 충분히 분리되지 않은 구조
- `PAIR`/`DEALER`에서 zlink만 타는 public aggregate recv 경로의 추가 고정비
- `socket_base_t::send()` 경로의 public lifecycle/lock 비용과 backpressure 복귀 비용

반대로 아래 항목은 지금 차이의 핵심 원인으로 보기 어렵다.

- `fq/lb` 알고리즘 자체
- `PAIR`/`DEALER`의 core `xsend/xrecv` 기본 구조
- `reset_metadata()` 자체
- `PAIR`/`DEALER` single steady-state에서의 대규모 heap alloc 자체

즉 현재 남은 격차는 transport core보다, 최근 public message API 분해 이후
`socket_base` public 진입과 zlink-specific bookkeeping이 small-message hot path에
더 많이 묻어 있는 구조로 해석하는 것이 맞다.

## 2. 검토 대상과 관찰 범위

이번 검토는 아래 경로를 중심으로 진행했다.

- zlink public hot path
  - [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
  - [`core/src/sockets/pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
  - [`core/src/sockets/dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
  - [`core/src/sockets/socket_base_dispatch.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_dispatch.cpp)
  - [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
- zlink public API wrapper
  - [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
  - [`core/src/api/socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
  - [`core/src/api/socket_message_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_api.cpp)
- libzmq 대응 기준
  - [`libzmq/src/socket_base.cpp`](/home/hep7/project/kairos/libzmq/src/socket_base.cpp)
  - [`libzmq/src/pair.cpp`](/home/hep7/project/kairos/libzmq/src/pair.cpp)
  - [`libzmq/src/dealer.cpp`](/home/hep7/project/kairos/libzmq/src/dealer.cpp)

## 2.1 현재 `with_zmq single`이 실제로 비교하는 surface

`with_zmq single`은 이름만 "recv 방식 비교"일 뿐, 실제로는 sender/receiver
양쪽 public API 비용이 모두 throughput에 들어간다. 수신 스레드가 카운트를
올리기 때문에 recv 기준 surface가 맞긴 하지만, sender가 HWM에 자주 닿으면
blocking `send`의 backpressure 복귀 비용도 그대로 측정값에 섞인다.

패턴별 실제 surface는 다음과 같다.

| Pattern | zlink | libzmq(std_compat) | 해석 |
|------|------|------|------|
| `PAIR` | `zlink_send(parts,1)` + `zlink_recv(&parts,&count)` | `zmq_send(buffer)` + `zmq_msg_recv(msg)` | 공통 원인 판단용 핵심 패턴 |
| `DEALER_DEALER` | `zlink_send(parts,1)` + `zlink_recv(&parts,&count)` | `zmq_send(buffer)` + `zmq_msg_recv(msg)` | 공통 원인 판단용 핵심 패턴 |
| `DEALER_ROUTER` | dealer send + router recv aggregate | dealer send + frame-by-frame recv | routed recv 추가 비용 포함 |
| `PUBSUB` | `zlink_publish("", part,1)` + `zlink_subscribe()` | `zmq_send(buffer)` + `zmq_msg_recv(msg)` | topic frame/API surface 차이가 있음 |
| `ROUTER_ROUTER` | `zlink_send(parts,2)` + `zlink_recv(source_rid,&parts,&count)` | `zmq_send` 2회 + `zmq_msg_recv` 2회 | routed frame strip/out-param 차이가 있음 |

중요한 의미는 다음과 같다.

- `PAIR`/`DEALER_DEALER`는 가장 공통적인 send/recv hot path 차이를 보기 좋은 패턴이다
- `PUBSUB`/`ROUTER_ROUTER`는 공통 원인 위에 패턴 전용 API surface 차이가 추가로 얹힌다
- 따라서 공통 회귀 원인 분석은 `PAIR`와 `DEALER_DEALER`를 1차 신호로 보고,
  `PUBSUB`/`ROUTER_ROUTER`는 증폭 패턴으로 읽는 것이 맞다

## 3. 성능 차이를 설명하는 유력 차이

### 3.1 최근 POSD 리팩토링에서 public message API hot path가 두꺼워졌다

최근 회귀와 직접 연결되는 변화는 아래 커밋들이다.

- `77bf9fbb refactor: continue posd core modularization`
- `d8dabbaf refactor: split socket message api entries`
- `a4cac5b0 refactor: finalize core posd remaining work`

이 구간에서 아래 파일이 새로 생기거나 크게 커졌다.

- [`core/src/api/socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)
- [`core/src/api/socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
- [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
- [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)

반면 최근 회귀 이전의 공개 send 경로는 훨씬 얇았다.

- [`01a5c663:core/src/api/zlink.cpp` `s_sendmsg()/zlink_send()`](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp)

최근 리팩토링 이후 public `send/recv`는:

- handle/tag 확인
- flag/part validation
- socket type 분기
- routed/unrouted/publish 별 helper 분리
- aggregate recv/TLS export

를 더 깊게 타게 됐다.

실제 관찰도 이 방향과 맞는다.

- 최근 `PAIR/DEALER`에 single-part public send fast path를 다시 얇게 만들자
  throughput이 바로 올라갔다
- 즉 최근 회귀를 설명하는 1차 축은 `thread-safe 기능 도입` 자체보다
  `public message API 분해 이후 hot path가 두꺼워진 것`으로 보는 편이 맞다

### 3.2 `send` public hot path에 libzmq보다 한 층 더 두꺼운 lifecycle/lock이 있다

zlink `send`는 아래 순서를 탄다.

- public API inflight/closing admission
- pending command 처리
- `more` 플래그 정리
- metadata reset
- public API sync lock
- `xsend()`

코드:
- [`socket_base_msg.cpp`#L9](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp#L9)
- [`socket_base_msg.cpp`#L35](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp#L35)

반면 libzmq는 대응 위치에서 optional `_sync`만 잡고 바로 같은 send 알고리즘으로
들어간다.

코드:
- [`socket_base.cpp`#L1204](/home/hep7/project/kairos/libzmq/src/socket_base.cpp#L1204)
- [`socket_base.cpp`#L1235](/home/hep7/project/kairos/libzmq/src/socket_base.cpp#L1235)

의미:

- `xsend()` 자체가 특별히 무거운 것이 아니라
- `xsend()`에 들어가기 전의 public lifecycle 관리 비용이 zlink에 더 있다
- small-message에서는 이 차이가 그대로 per-message 고정비가 된다

특히 zlink는 아래 요소를 추가로 가진다.

- `socket_public_api_scope_t`
- `socket_public_api_lock_scope_t`
- `socket_lifecycle_coordinator_t`의 inflight/closing 상태 관리

이 계층은 기능적으로 필요할 수 있고, 실제 비용도 맞다. 다만 중요한 보정이
하나 있다.

- 이 atomic/CAS 기반 lifecycle coordinator는 최근 POSD 리팩토링에서 새로 생긴
  것이 아니라, `01a5c663` 시점에도 이미 거의 같은 형태로 존재했다

즉 이 항목은 "현재도 비싼 비용"이긴 하지만, 최근 회귀의 1차 원인을 단독으로
설명하는 항목으로 보긴 어렵다. 더 정확히는:

- 원래부터 있던 비용 위에
- 최근 public message API 분해/일반화 비용이 더 얹힌 상태

로 보는 편이 맞다.

### 3.3 `PAIR`/`DEALER` recv마다 `last_recv_source_rid`를 저장한다

zlink는 `PAIR`와 `DEALER`에서 메시지 수신 성공 후 source RID를 저장한다.

코드:
- [`pair.cpp`#L110](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp#L110)
- [`pair.cpp`#L124](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp#L124)
- [`dealer.cpp`#L100](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp#L100)
- [`dealer.cpp`#L105](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp#L105)

이 저장은 결국 아래 경로를 탄다.

- `pipe -> peer -> routing_id resolve`
- `zlink_routing_id_t` 구조체 복사
- endpoint runtime에 last source RID 저장

코드:
- [`socket_base_dispatch.cpp`#L408](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_dispatch.cpp#L408)
- [`socket_runtime.cpp`#L194](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp#L194)

libzmq의 같은 경로에는 이런 작업이 없다.

코드:
- [`libzmq/src/pair.cpp`#L74](/home/hep7/project/kairos/libzmq/src/pair.cpp#L74)
- [`libzmq/src/dealer.cpp`#L79](/home/hep7/project/kairos/libzmq/src/dealer.cpp#L79)

의미:

- `PAIR`/`DEALER`는 원래 수신 경로가 매우 짧아야 하는데
- zlink는 성공한 recv마다 source bookkeeping이 하나 더 붙는다
- 작은 메시지, 특히 `inproc`/`ipc`에서 이런 추가 작업은 바로 throughput gap으로
  드러날 수 있다

### 3.4 callback mode가 사실상 고정인데도 steady-state 경로가 충분히 분리되지 않았다

zlink `PAIR`는 attach/read activation 시 dispatch mode를 계속 의식한다.

코드:
- [`pair.cpp`#L56](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp#L56)
- [`pair.cpp`#L79](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp#L79)

`DEALER`도 `xread_activated()`에서 callback dispatch 여부를 보고, 필요하면
즉시 drain loop로 진입한다.

코드:
- [`dealer.cpp`#L119](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp#L119)
- [`dealer.cpp`#L129](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp#L129)

libzmq의 같은 위치는 훨씬 얇다.

코드:
- [`libzmq/src/pair.cpp`#L45](/home/hep7/project/kairos/libzmq/src/pair.cpp#L45)
- [`libzmq/src/dealer.cpp`#L94](/home/hep7/project/kairos/libzmq/src/dealer.cpp#L94)

의미:

- handler attach/detach가 자주 일어나서 비싼 것이 핵심은 아니다
- 실제 사용 모델은 handler를 한 번 붙이면 callback mode가 사실상 고정된다
- 그런데도 일반 steady-state 경로가 callback-aware 상태를 계속 의식하면
  recv mode bench에서도 분기와 상태 유지 비용을 계속 낸다
- 즉 문제의 본질은 "handler 조회 비용"보다
  "mode가 고정인데 hot path가 mode-specialized되지 않았다"는 점이다

### 3.5 `recv`의 command runtime abstraction도 미세 비용을 추가한다

zlink `recv`는 `_ticks`를 직접 다루지 않고 `command_runtime()` 추상화를 한 번 더
거친다.

코드:
- [`socket_base_msg.cpp`#L206](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp#L206)
- [`socket_base_msg.cpp`#L237](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp#L237)

libzmq는 대응 경로에서 `_ticks` 직접 갱신만 한다.

코드:
- [`socket_base.cpp`#L1316](/home/hep7/project/kairos/libzmq/src/socket_base.cpp#L1316)
- [`socket_base.cpp`#L1361](/home/hep7/project/kairos/libzmq/src/socket_base.cpp#L1361)

이 차이 하나만으로 큰 gap이 생긴다고 보긴 어렵다. 다만 위 3개 차이와 합치면
small-message per-message 고정비를 설명하는 보조 요인으로는 충분하다.

### 3.6 `recv_msg_internal()`의 mode guard도 zlink-specific 비용이다

zlink의 direct/public recv 진입은 실제 `socket->recv()`에 들어가기 전에
아래 상태를 매번 확인한다.

- `socket_msg_dispatch_active()`
- `SUB/XSUB`의 `sub_dispatch_active()`
- `XPUB`의 `xpub_dispatch_active()`
- `STREAM`의 `stream_dispatch_active()`

코드:
- [`core/src/core/recv_internal.cpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_internal.cpp)

이건 기능적으로는 맞는 guard다. 다만 libzmq에는 같은 층의 개념이 없다.

즉 의미는 다음과 같다.

- zlink는 recv-mode와 callback/dispatch/service mode를 같은 public surface 안에서
  방어하고 있다
- 그래서 single steady-state recv에서도 mode guard 분기와 상태 조회가 매번 붙는다
- 이 역시 큰 단일 원인은 아니지만, small-message recv의 고정비를 설명하는
  보조 요인으로는 충분하다

## 4. 현재 단계에서 원인으로 보기 어려운 차이

### 4.1 `fq/lb`는 거의 동일하다

`fq.cpp`, `lb.cpp`는 libzmq와 구조적으로 거의 같다.

- [`core/src/sockets/fq.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/fq.cpp)
- [`core/src/sockets/lb.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/lb.cpp)
- [`libzmq/src/fq.cpp`](/home/hep7/project/kairos/libzmq/src/fq.cpp)
- [`libzmq/src/lb.cpp`](/home/hep7/project/kairos/libzmq/src/lb.cpp)

즉 현재 성능 차이를 queue policy 자체로 설명하는 것은 무리다.

### 4.2 `PAIR`/`DEALER`의 기본 `xsend/xrecv` 구조도 거의 같다

`PAIR`:
- [`pair.cpp`#L93](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp#L93)
- [`libzmq/src/pair.cpp`#L57](/home/hep7/project/kairos/libzmq/src/pair.cpp#L57)

`DEALER`:
- [`dealer.cpp`#L95](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp#L95)
- [`libzmq/src/dealer.cpp`#L74](/home/hep7/project/kairos/libzmq/src/dealer.cpp#L74)

핵심 send/recv loop는 유사하다. 따라서 차이는 소켓 타입 알고리즘보다 그
바깥의 bookkeeping에서 찾는 것이 맞다.

### 4.3 `reset_metadata()`는 libzmq도 한다

zlink:
- [`socket_base_msg.cpp`#L33](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp#L33)

libzmq:
- [`socket_base.cpp`#L1233](/home/hep7/project/kairos/libzmq/src/socket_base.cpp#L1233)

이 동작은 양쪽 모두 존재하므로, 현재 gap의 주요 설명 변수로 보긴 어렵다.

## 5. public API wrapper 차이 평가

이번 검토 시점 기준으로 `public recv/send wrapper`는 예전보다 많이 정리됐다.

### 5.1 예전 큰 병목은 이미 제거된 상태

예전에는 public recv가 single-part도 heap-backed multipart materialization을
강제했다. 현재는 TLS view로 바뀌어 그 큰 비용은 제거됐다.

코드:
- [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)

현재 남아 있는 recv wrapper 비용은:

- `zlink_msg_init/close/move`
- topic/routing id `memcpy`
- TLS view push/commit

이 정도이며, 예전의 `malloc/free(parts)`와는 차원이 다르다.

다만 `with_zmq single`의 `PAIR`/`DEALER_DEALER` 비교에서는 이 비용이
상대적으로 더 직접적인 의미를 가진다.

- zlink 측은 `zlink_recv(&parts,&count)`를 사용한다
- libzmq 측은 `zmq_msg_recv(msg)`를 사용한다

즉 `PAIR`/`DEALER` 패턴에서는 aggregate recv/TLS export 비용이 여전히
zlink 쪽에 더 특이적인 고정비다.

### 5.2 `single-part` send는 더 이상 큰 clone 경로를 타지 않는다

현재 `part_count == 1`이면 `s_sendmsg()`로 바로 간다.

코드:
- [`socket_message_send_api.cpp`#L174](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp#L174)
- [`socket_message_send_api.cpp`#L187](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp#L187)

즉 지금 `PAIR`/`DEALER` single gap을 `public API wrapper의 큰 heap/clone 비용`
하나로 설명하는 것은 맞지 않다.

### 5.3 현재 남은 gap을 "heap alloc 문제"로 보는 것은 부정확하다

예전과 달리 현재 `PAIR`/`DEALER` single steady-state 경로에는 아래와 같은
"메시지마다 큰 heap 할당"이 핵심으로 남아 있지 않다.

- public recv의 `malloc/free(parts)`
- single-part send의 multipart clone vector

즉 현재 gap의 중심은

- 큰 heap alloc
- 큰 payload copy

보다는

- public lifecycle/lock
- RID bookkeeping
- dispatch 관련 분기와 상태 관리
- command polling/runtime

같은 per-message 고정비로 해석하는 것이 맞다.

### 5.4 하지만 "최근 회귀"는 wrapper 깊이와 직접 연결된다

현재 wrapper는 예전처럼 큰 heap/clone 병목이 주인공은 아니지만, 최근 회귀와의
상관관계는 여전히 강하다.

이유는 다음과 같다.

- 최근 POSD 리팩토링에서 `socket_message_send_api.cpp`,
  `socket_message_recv_api.cpp`가 새로 분리됐다
- 이 분리 이후 single public send/recv는 검증/분기/helper 계층을 더 많이 타게
  됐다
- 실제로 최근 `PAIR/DEALER`에 single-part fast path를 복구하자 throughput이
  바로 올라갔다

즉 현재 상태는:

- "큰 heap alloc이 남아서 느리다"는 건 아님
- 하지만 "최근 API 분해로 public hot path가 두꺼워졌다"는 건 맞다

## 6. 현재 데이터와 코드 해석의 연결

최근 `single` quick 측정에서 관찰된 특징은 다음과 같다.

- `PAIR`, `DEALER_DEALER`, `DEALER_ROUTER` 전반에서 libzmq 대비 zlink가 작게는
  한 자릿수 %, 크게는 수십 % 뒤처짐
- `tcp`뿐 아니라 `ipc`, `inproc`에서도 gap이 유지됨
- public aggregate recv의 큰 병목을 걷은 뒤에도 gap이 남음

이 패턴은 아래 해석과 잘 맞는다.

- transport/network 자체 문제보다는
- socket public 진입 오버헤드와 zlink-specific bookkeeping이 small-message
  steady-state에 누적되고 있다

특히 `inproc`에서도 gap이 유지된다는 점은:

- OS 네트워크보다
- `socket_base`, `dispatch`, `RID bookkeeping`
차이가 더 유력하다는 근거다.

## 6.2 backpressure와 queue 지표는 별도로 읽어야 한다

최근 single perf 진단에서 확인한 사실은 다음과 같다.

- `PAIR tcp/ipc 64B`에서 `snd_pending_max`가 거의 바로 `999~1000`에 붙는다
- `inproc`에서는 합산 HWM 특성 때문에 `snd_pending_max`가 `1998~1999`까지 보인다
- `rcv_pending_max`는 `tcp/ipc`에서는 거의 `0`, `inproc`에서는 함께 올라갔다가
  끝나기 전 `0`으로 돌아온다

이건 sender가 실제로 HWM 근처에서 자주 막히고 있다는 뜻이다. 다만 여기서
주의할 점이 있다.

- `core/perf/single`의 backpressure 대기 함수
  [`single_wait_for_send_backpressure()`](/home/hep7/project/kairos/zlink/core/perf/single/common/bench_common.hpp#L1430)
  는 `EAGAIN` 뒤에 소켓 `POLLOUT`를 기다리지 않고 `poll(NULL, 0, 1)`로 그냥
  1ms 쉬었다가 다시 보낸다

즉 `core/perf/single`은:

- backpressure가 걸린다는 사실을 보여주는 진단엔 유용하지만
- 복귀 속도를 정밀하게 재는 측정치로는 거칠다

중요한 결론은 두 가지다.

- `perf`의 1ms sleep은 진단 surface 이슈다
- 하지만 `with_zmq single`은 이런 1ms sleep이 아니라 blocking send를 쓰는데도
  zlink가 libzmq보다 밀린다

즉 backpressure는 실제 핵심 현상 중 하나지만,
"현재 gap이 전부 perf sleep 때문이다"라고 보면 안 된다.

## 6.3 타임라인 기준 해석

현재 데이터를 날짜순으로 놓고 보면, 최근 POSD 리팩토링에서 생긴 추가 회귀와
그 이전에 이미 들어와 있던 구조 비용을 분리해서 읽을 수 있다.

### 6.3.1 `3/23 pre-POSD` 대비 `3/27 current perf`

`core/perf` 기준으로는 POSD 리팩토링 이후 생겼던 추가 병목 대부분이 이미
정리된 쪽에 가깝다.

예시:

- `single callback PAIR tcp 64B`
  - `2026-03-23`: `2919.29 Kmsg/s`
  - `2026-03-27`: `3090.08 Kmsg/s`
- `single callback DEALER_DEALER tcp 64B`
  - `2026-03-23`: `2989.90 Kmsg/s`
  - `2026-03-27`: `2908.41 Kmsg/s`
- `multi recv DEALER_DEALER tcp 64B`
  - `2026-03-23`: `1496.697 Kmsg/s`
  - `2026-03-27`: `1565.549 Kmsg/s`
- `multi recv ROUTER_ROUTER tcp 64B`
  - `2026-03-23`: `897.329 Kops/s`
  - `2026-03-27`: `902.288 Kops/s`

즉 `3/23 -> 3/27` 구간만 보면:

- 일부 패턴은 개선
- 일부는 소폭 변동
- 전반적으로 "대규모 추가 퇴행"은 보이지 않는다

이건 multipart materialization 문제와 POSD 리팩토링으로 생긴 직접 병목이
대부분 해소됐다는 팀장님 가설과 맞는다.

### 6.3.2 남은 큰 상대 gap은 `3/05 baseline`과 비교해야 한다

반면 `with_zmq single`의 오래된 baseline은 완전히 다르다.

코드/리포트:
- [`core/bench/with_zmq/results/single/report/perf_linux_20260305_204428.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260305_204428.txt)

예시:

- `PAIR tcp 64B`
  - `2026-03-05`: `libzmq 2610.75`, `zlink 3632.43`, `zlink +39.13%`
- `DEALER_DEALER tcp 64B`
  - `2026-03-05`: `libzmq 2566.54`, `zlink 3634.35`, `zlink +41.61%`

즉 `3/05` 시점엔 적어도 `PAIR/DEALER` small-message에서
`zlink ~= libzmq`가 아니라 오히려 더 빠른 구간이 있었다.

반면 현재는 최근 recv 기준 quick 비교에서:

- `PAIR tcp 64B`: `libzmq 4387.20`, `zlink 2703.01`, 약 `-38.39%`

즉 상대 비교의 부호 자체가 뒤집혔다.

이건 "최근 POSD 리팩토링 추가 회귀"와는 별개의 층이 남아 있다는 뜻이다.

### 6.3.3 따라서 남은 원인 시점은 `3/05 -> 3/23` 사이가 더 유력하다

위 타임라인을 합치면 다음 결론이 가장 자연스럽다.

- `3/23 -> 3/27` POSD 잔여 병목은 대부분 정리됐다
- 그런데 `3/05 baseline`으로 돌아가진 못했다
- 따라서 남은 상대 gap의 더 큰 축은 `3/05 -> 3/23` 사이에 들어온 구조 변화일
  가능성이 높다

이 구간의 대표 후보는:

- thread-safe socket 계약 도입/강화
  - `b7a7b0b0`, `01a5c663`
- direct callback recv / callback surface 재정렬
  - `f305d856`, `28fb0dd4`, `ba52d1ac`, `363b107f`, `77cdc3a7`
- recv-first / msg-only / pubsub public surface realignment
  - `98e7d324`, `9fa9ea57`, `f7daa6a5`, `2f38e8db`

즉 현재 남은 이슈를 읽는 기준은:

- "POSD 리팩토링이 아직 남긴 병목"보다는
- "thread-safe + callback/recv surface 재정렬 시기에 이미 들어온 구조 비용"을
  찾아야 한다

이 문서의 이후 우선순위도 그 기준을 따른다.

### 6.3.4 `3/05 -> 3/23` 사이 핵심 hot-path diff

핵심 hot path 파일만 놓고 보면 `3/05` 근처 기준 커밋 `7bea9e3f`와
`3/23 pre-POSD` 기준 커밋 `9ef080f7`의 차이는 생각보다 좁다.

실제 diff가 크게 잡히는 파일:

- [`core/src/sockets/pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
- [`core/src/sockets/dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)
- [`core/src/core/recv_internal.cpp`](/home/hep7/project/kairos/zlink/core/src/core/recv_internal.cpp)

즉 이 시점의 남은 상대 gap은 "모든 곳이 조금씩 느려졌다"기보다,
`PAIR/DEALER` recv hot path와 direct recv 진입 경계가 실제로 더 두꺼워진 쪽으로
보는 것이 맞다.

구체적으로 들어온 변화는 다음과 같다.

- `pair.cpp`
  - dispatch part 저장 vector 경로 추가
  - `xread_activated()`의 dispatch drain 추가
  - `xrecv()` 성공 시 `store_last_recv_source_rid(_pipe)` 추가
- `dealer.cpp`
  - dispatch part 저장 vector 경로 추가
  - `xread_activated()`의 dispatch drain loop 추가
  - `xrecv()` 성공 시 `store_last_recv_source_rid(pipe)` 추가
- `recv_internal.cpp`
  - direct/public recv 진입을 별도 모듈로 분리
  - `socket_msg_dispatch_active()`, `sub_dispatch_active()`,
    `xpub_dispatch_active()`, `stream_dispatch_active()` guard 추가

즉 `PAIR`/`DEALER` small-message 기준으로 보면 `3/05` 이후 새로 들어온
실질 비용은 다음 두 묶음으로 압축된다.

1. direct recv 진입의 mode guard
2. recv 성공 후 source RID bookkeeping 및 dispatch-aware state

이건 현재 `PAIR`/`DEALER`가 가장 좋은 공통 원인 판단 패턴이라는 해석과도
정확히 맞는다.

### 6.3.5 남은 원인은 한 축이 아니라 두 시기의 누적일 가능성이 높다

현재 기준으로는 아래 두 시기의 변화가 함께 누적됐다고 보는 것이 가장 자연스럽다.

#### A. `3/05 -> 3/16`: thread-safe socket 계약 도입

대표 커밋:

- `b7a7b0b0`
- `01a5c663`

의미:

- `socket_base_t::send()` 경로에 public lifecycle coordinator와 sync lock이
  들어갔다
- 이 비용은 single thread benchmark에서도 per-message atomic/CAS로 남는다
- 따라서 전 패턴 공통 send-side 고정비를 설명하는 축이다

#### B. `3/16 -> 3/23`: callback/recv-first surface 재정렬

대표 커밋:

- `f305d856`
- `28fb0dd4`
- `363b107f`
- `77cdc3a7`

의미:

- `PAIR/DEALER` recv hot path가 callback/dispatch-aware 구조를 의식하게 됐다
- `recv_internal()` 분리와 mode guard가 direct recv entry 비용을 늘렸다
- `store_last_recv_source_rid()`가 `PAIR/DEALER` recv 고정비로 들어왔다

즉 현재 남은 상대 gap을 "thread-safe만의 문제" 또는 "callback만의 문제"로
단정하는 것보다,

- send 쪽은 thread-safe 계약 도입 비용
- recv 쪽은 callback/recv-first 재정렬 비용

이 함께 누적된 결과로 보는 것이 더 정확하다.

## 6.4 현재 quick 수치

아래 수치는 현재 워크스페이스 기준으로 같은 조건에서 다시 찍은
`single / tcp / 64B / warmup=1s / duration=1s / latency=1s` quick 결과다.

측정 명령:

```bash
PERF_SINGLE_WARMUP_SECONDS=1 PERF_SINGLE_DURATION_SECONDS=1 PERF_SINGLE_LATENCY_SECONDS=1 ./core/build/bin/comp_std_zmq_pair libzmq tcp 64
PERF_SINGLE_WARMUP_SECONDS=1 PERF_SINGLE_DURATION_SECONDS=1 PERF_SINGLE_LATENCY_SECONDS=1 ./core/build/bin/comp_zlink_pair zlink tcp 64
PERF_SINGLE_WARMUP_SECONDS=1 PERF_SINGLE_DURATION_SECONDS=1 PERF_SINGLE_LATENCY_SECONDS=1 ./core/build/bin/comp_std_zmq_dealer_dealer libzmq tcp 64
PERF_SINGLE_WARMUP_SECONDS=1 PERF_SINGLE_DURATION_SECONDS=1 PERF_SINGLE_LATENCY_SECONDS=1 ./core/build/bin/comp_zlink_dealer_dealer zlink tcp 64
```

throughput 기준:

| Pattern | libzmq | zlink | Gap |
|------|------:|------:|------:|
| `PAIR` | `4,354,295 msg/s` | `2,615,843 msg/s` | `-39.93%` |
| `DEALER_DEALER` | `3,725,870 msg/s` | `2,823,829 msg/s` | `-24.21%` |

이 수치는 아래 사실을 보여준다.

- 현재 gap은 bench 노이즈 수준이 아니다.
- `PAIR`처럼 경로가 얇은 패턴일수록 격차가 더 크게 드러난다.
- `DEALER_DEALER`에서도 여전히 `20%+` 차이가 남는다.

latency 수치도 quick run에서 같이 나오긴 하지만, 이 1초 측정은 single
throughput 중심 진단용이라 절대 latency 비교 기준으로 쓰기엔 왜곡이 크다.
따라서 현재 문서에서는 throughput 차이를 중심 신호로 본다.

또한 이 수치는 quick snapshot이므로 절대값은 실행 시점마다 흔들릴 수 있다.
이 문서에서 의미 있게 보는 것은 개별 숫자 자체보다 다음 두 가지다.

- 같은 조건에서 zlink가 libzmq보다 일관되게 뒤처지는 방향성
- `PAIR`가 `DEALER_DEALER`보다 더 큰 gap을 보인다는 패턴

## 7. 우선순위 제안

현재 단계에서 다음 우선순위는 아래가 맞다.

1. 최근 POSD 리팩토링에서 두꺼워진 public message API
   [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp),
   [`socket_message_recv_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp)
   경로를 `PAIR`/`DEALER` single-part 기준으로 더 얇게 만들 수 있는지 검토
2. `PAIR`/`DEALER`에서 zlink만 수행하는 public aggregate recv/TLS export를
   single-part 기준으로 더 얇게 만들 수 있는지 검토
3. `PAIR`/`DEALER`에서 `store_last_recv_source_rid()`를 꼭 필요한 경우에만 하도록
   줄일 수 있는지 검토
4. recv mode와 callback/dispatch mode를 hot path에서 더 깊게 분리할 수 있는지
   검토
5. [`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
   의 lifecycle/lock 및 backpressure 복귀 fast path를 다시 다듬을지 검토
6. 마지막에 `PUBSUB`/`ROUTER_ROUTER`의 패턴 전용 surface 차이를 따로 최적화

반대로 당장 우선순위가 낮은 것은:

- `fq/lb` 재작성
- `PAIR`/`DEALER` algorithm 자체 변경
- metadata reset 제거 시도

## 8. 작업 시 주의

성능 이유로 아래 원칙을 깨면 안 된다.

- thread-safe 계약을 약화시키지 않는다
- callback/dispatch/monitor/service lifecycle correctness를 희생하지 않는다
- perf/bench surface를 우회해서 숫자만 올리지 않는다

특히 `pipe` steady-state lock을 걷는 방식은 수치상 효과가 있어도,
`pipe`가 command 대상 객체라는 점 때문에 thread-safety 증명이 부족하면
확정하면 안 된다. 이 문서는 그 점을 반영해, 현재 가장 설득력 있는 후보를
최근 POSD 리팩토링에서 두꺼워진 public message API 층과 RID bookkeeping으로
본다.

## 8.1 thread-safe를 유지하면서 수정하는 방향

이번 영역에서 가장 중요한 제약은 "빠르게 만드는 것"보다
"현재 thread-safe/lifecycle 계약을 깨지 않는 것"이다.

즉 아래 두 질문에 모두 답할 수 있어야만 수정 후보로 볼 수 있다.

- 이 변경이 steady-state hot path 비용을 줄이는가
- 이 변경이 public close/callback/async mailbox/command dispatch 계약을
  약화시키지 않는가

### 8.1.1 건드리면 안 되는 축

아래 축은 성능 이유만으로 약화시키면 안 된다.

- `socket_lifecycle_coordinator_t`의 public inflight/closing 계약
- callback 진입/종료 시점의 close handoff 계약
- async mailbox quiesce와 destroy handoff 계약
- `pipe`가 command 대상 객체라는 사실

특히 `pipe`는 아래 명령의 destination이 될 수 있다.

- `activate_read`
- `activate_write`
- `hiccup`
- `pipe_term`
- `pipe_term_ack`
- `pipe_hwm`

코드:
- [`core/src/core/object.cpp`](/home/hep7/project/kairos/zlink/core/src/core/object.cpp)
- [`core/src/core/object.hpp`](/home/hep7/project/kairos/zlink/core/src/core/object.hpp)

즉 `pipe`를 단순 SPSC 자료구조처럼 보고 steady-state lock을 무조건 제거하는 것은
thread-safe 증명이 부족하다.

### 8.1.2 우선적으로 손댈 수 있는 안전한 축

thread-safe를 유지하면서도 줄일 수 있는 후보는 아래 순서가 맞다.

1. 불필요한 steady-state bookkeeping 제거
2. hot path에서 필요 없는 feature 분기 분리
3. public entry 경로의 중복 확인/중복 상태 확인 제거
4. 그 다음에만 내부 동기화 축 재검토

이 순서가 중요한 이유는, 1~3은 대체로 기능 의미를 유지한 채
"안 해도 되는 일"을 줄이는 작업이기 때문이다.

### 8.1.3 `last_recv_source_rid`는 가장 안전한 상위 후보 중 하나다

현재 `PAIR`/`DEALER` recv 성공마다 source RID를 저장한다.

코드:
- [`core/src/sockets/pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp)
- [`core/src/sockets/dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp)

thread-safe를 유지하면서 이 비용을 줄이는 현실적인 방법은 아래 둘 중 하나다.

- 정말 필요한 surface에서만 `last_recv_source_rid`를 갱신
- 또는 lazy하게 조회 시점에 계산

권장 방향:

- `PAIR`/`DEALER` single steady-state recv에서는 기본적으로 갱신하지 않음
- 실제로 `last_recv_source_rid`를 읽는 API/모드에서만 갱신을 요구하거나
  lazy resolve

이 접근의 장점:

- pipe/command/lifecycle 계약을 건드리지 않는다
- recv 성공마다 발생하는 구조체 복사와 source resolve를 줄일 수 있다
- libzmq에는 없는 zlink-specific per-message 비용을 직접 줄인다

### 8.1.4 callback-dispatch는 "고정된 mode"로 더 깊게 분리하는 방향이 좋다

dispatch 지원 자체를 없애면 안 된다. 대신 일반 recv/send path에서
dispatch feature를 덜 의식하게 만들어야 한다.

가능한 방향:

- handler attach 시점을 사실상 mode 전환으로 간주
- attach/read activation 시 callback mode 전용 helper로 분리
- steady-state `xrecv/xsend`에서는 recv mode와 callback mode가 각자 더 직접적인
  경로를 타도록 정리
- callback mode와 recv mode의 진입 surface는 유지하되, 내부 책임을 더 깊게 분리

POSD 관점에서 보면:

- dispatch는 깊은 별도 모듈이어야지
- 일반 socket hot path가 매번 "dispatch도 있을 수 있음"을 의식하는 shallow
  구조가 되면 안 된다

### 8.1.5 `socket_base_msg.cpp` 최적화는 lifecycle 의미를 유지한 채 얇게 해야 한다

현재 `send`는 libzmq보다 한 층 더 두꺼운 public lifecycle/lock 경로를 탄다.
이걸 줄이려면 lifecycle 자체를 없애는 게 아니라, steady-state에서의 비용을
낮춰야 한다.

가능한 방향:

- public admission과 sync lock의 fast path 비용을 더 낮추기
- callback/close와 무관한 steady-state send에서 반복 확인되는 상태를 축소
- `send`/`send_routed` 공통 로직에서 중복되는 lifecycle work를 재검토

피해야 하는 방향:

- public inflight counting 제거
- close 중 concurrent API 진입 차단 약화
- callback와 public API 재진입 계약 완화

즉 여기서는 "계약 축소"가 아니라 "계약을 유지한 fast path화"가 맞다.

### 8.1.6 `pipe` 동기화는 마지막 단계에서만 다뤄야 한다

`pipe` lock은 수치상 효과가 있어 보여도, 그건 가장 마지막 단계 후보로 두는 게
맞다.

그 전에 먼저 닫아야 하는 질문:

- `read/write/check_* / flush`가 실제로 어느 thread에서만 호출되는가
- command 기반 상태 전이와 truly concurrent할 가능성이 없는가
- socket 종류별로 같은 전제가 유지되는가
- service/monitor/async mailbox와 결합해도 같은가

이 질문에 코드와 테스트로 답하기 전에는:

- `pipe` lock 제거
- `pipe` state field의 lock-free 재해석

같은 수정은 확정하면 안 된다.

### 8.1.7 권장 수정 순서

thread-safe 유지 기준으로는 아래 순서가 가장 안전하다.

1. `socket_message_send_api.cpp` / `socket_message_recv_api.cpp` single-part
   fast path 정리
2. `PAIR`/`DEALER` public aggregate recv single-part fast path 정리
3. `PAIR`/`DEALER`의 `last_recv_source_rid` 갱신 최소화
4. callback-dispatch 관련 steady-state 분기 분리
5. `socket_base_msg.cpp` lifecycle/backpressure fast path 재검토
6. 마지막으로만 `pipe` 동기화 재검토

이 순서의 장점:

- 앞 단계들은 상대적으로 지역적 변경이다
- correctness/lifecycle 영향 범위가 작다
- 성능 이득이 없더라도 되돌리기 쉽다
- 반대로 `pipe` 변경은 영향 범위가 커서 맨 뒤가 맞다

## 8.2 분석 신뢰도와 한계

이 문서는 "코드상으로 어떤 비용이 실제 격차를 설명할 수 있는가"에 대한
구조적 분석 문서다. 즉 아래는 코드에서 직접 확인한 사실이다.

- 어떤 경로에서 atomic/CAS가 per-message로 실행되는지
- 어떤 경로에서 source RID resolve/copy가 매 recv마다 붙는지
- 어떤 경로에서 mode guard와 aggregate recv/TLS reset이 붙는지
- 현재 bench가 실제로 어떤 public surface를 타는지

반면 아래는 아직 "정량 추정" 단계다.

- 각 항목이 몇 ns/msg를 차지하는지
- 특정 항목 하나를 제거했을 때 정확히 몇 % 회복되는지

즉 이 문서는 원인 후보의 우선순위를 세우는 데는 충분하지만,
세부 ns 단위 기여도까지 증명하는 문서는 아니다. 그런 단계는 이후
`perf`/프로파일링과 실제 수정 실험으로 검증해야 한다.

## 9. 요약

현재 `single` 성능 격차는 코드상 설명 가능하다.

- `libzmq`와 같은 socket 알고리즘인데도
- zlink는 최근 POSD 리팩토링 이후 public message API 층이 더 두꺼워졌고,
  그 위에 recv source bookkeeping, callback-dispatch support가 steady-state
  hot path에 더 많이 섞여 있다

즉 다음 분석/개선의 중심은 `transport core`보다
[`socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp),
[`pair.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp),
[`dealer.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp),
[`socket_base_dispatch.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_dispatch.cpp)
가 되어야 한다.

## 10. 추가 검토: 원인 분석 메모에 대한 평가

아래 평가는 "최근 전 패턴/전 transport에서 동시에 퇴행했다"는 관찰을
현재 코드와 다시 대조한 결과다.

요지는 다음과 같다.

- "공통 send/recv 경로에 원인이 있다"는 큰 방향은 맞다
- 다만 최근 회귀를 `lock 하나`로 설명하는 것은 부족하고, 최근 POSD 리팩토링에서
  두꺼워진 public message API 계층, mode guard, RID bookkeeping,
  public aggregate recv 비용이 같이 누적된다고 보는 것이 더 정확하다
- `P0` 후보 둘은 타당하지만, `recv_tls_view`는 예전 `malloc/free(parts)` 급
  병목으로 과장하면 안 된다

### 10.1 원인 1: lifecycle coordinator atomic 4회/send

이 평가는 대체로 맞다.

코드:
- [`socket_base_msg.cpp`#L9](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp#L9)
- [`socket_runtime.cpp`#L326](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp#L326)
- [`socket_runtime.cpp`#L399](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp#L399)
- [`libzmq/src/socket_base.cpp`#L1204](/home/hep7/project/kairos/libzmq/src/socket_base.cpp#L1204)

현재 zlink `send()`는 steady-state에서 아래 비용을 낸다.

- `enter_public_api()`의 `fetch_add/fetch_sub`
- `lock_public_api_sync()`의 CAS spin
- `unlock_public_api_sync()`의 atomic store

중요한 점은 이 비용이 "경합이 있을 때만 비싸다"가 아니라는 것이다.
지금 벤치처럼 single thread여도 RMW atomic과 CAS는 매 메시지마다 실행된다.

다만 최근 코드와 과거 코드를 다시 대조해보면 중요한 보정이 있다.

- `enter_public_api()/leave_public_api()/lock_public_api_sync()` 형태의
  atomic/CAS 기반 lifecycle coordinator는
  [`01a5c663:core/src/sockets/socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)
  시점에도 이미 존재했다
- 즉 이 항목은 "현재도 비싼 비용"이지만, 최근 POSD 리팩토링에서 새로 생긴
  회귀 원인으로 단정하면 안 된다

다만 표현은 조금 보정이 필요하다.

- `atomic 4회` 자체는 사실이지만
- 최근 회귀를 그 항목 하나로 설명하는 것은 과하다
- 실제로는 이 비용이 `recv` 쪽 고정비와 함께 누적된다고 보는 것이 맞다

판정:
- "현재 성능 차이를 키우는 비용"으로는 타당
- 단일 thread 환경에서도 충분히 의미 있는 비용
- 하지만 "최근 회귀의 1차 원인"으로 단정하긴 이르다

### 10.2 원인 2: `recv_tls_view` single-part 오버헤드

이 평가는 "public aggregate recv를 쓸 때"는 맞다.

코드:
- [`recv_tls_view.hpp`#L42](/home/hep7/project/kairos/zlink/core/src/core/recv_tls_view.hpp#L42)
- [`recv_tls_view.hpp`#L56](/home/hep7/project/kairos/zlink/core/src/core/recv_tls_view.hpp#L56)
- [`recv_tls_view.hpp`#L91](/home/hep7/project/kairos/zlink/core/src/core/recv_tls_view.hpp#L91)
- [`socket_message_recv_api.cpp`#L220](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp#L220)

현재 `zlink_recv()`는 single-part여도 아래를 탄다.

- `begin()` -> `reset()`
- `push()` -> `zlink_msg_move()`
- `commit()` -> TLS view pointer export

이 경로는 분명히 libzmq의 frame-by-frame recv보다 두껍다.

다만 보정해야 할 점도 있다.

- 현재 `reset()`은 cap 전체가 아니라 `tls.count`만큼만 순회한다
- 예전의 `malloc/free(parts)`와 달리 큰 heap alloc은 이미 제거됐다
- `zlink_msg_recv()` 경로에는 이 비용이 없다
- 현재 `std_compat`도 aggregate recv surface를 흉내 낼 때는
  [`std_compat/zlink.h`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/std_compat/zlink.h)
  안에서 `recv_tls_reset()`과 `recv_tls_push()`를 사용한다
- 하지만 `with_zmq single`의 핵심 공통 패턴인 `PAIR`/`DEALER_DEALER`에서는
  libzmq 측이 `zmq_msg_recv()`를 사용한다. 즉 이 패턴들에서는
  `recv_tls_view` 비용이 사실상 zlink 쪽에 더 특이적이다.

즉 의미는 다음과 같다.

- public aggregate recv를 쓰는 zlink bench에서는 실제 병목 후보가 맞다
- 특히 `PAIR`/`DEALER` 비교에선 상대 격차 설명력이 꽤 높다
- 다만 예전 회귀처럼 "압도적 단일 원인"으로 보는 것은 과장이다
- 지금은 `public recv 계층 + send lifecycle/backpressure 비용 + mode guard`가
  함께 누적된다고 보는 편이 정확하다

판정:
- `P0` 또는 `P1 상단` 후보로 타당
- 특히 public `zlink_recv()` 기반 bench라면 설명력이 높다
- 다만 `msg_recv` 경로에는 해당되지 않음을 분리해서 봐야 한다

### 10.3 원인 3: dispatch active 체크

이 평가는 맞다.

코드:
- [`recv_internal.cpp`#L15](/home/hep7/project/kairos/zlink/core/src/core/recv_internal.cpp#L15)

현재 direct/public recv 진입은 실제 `socket->recv()` 전에:

- `socket_msg_dispatch_active()`
- `sub_dispatch_active()`
- `xpub_dispatch_active()`
- `stream_dispatch_active()`

를 확인한다.

handler가 자주 attach/detach되기 때문에 비싼 것이 아니라,
"mode가 사실상 고정인데도 hot path가 mode-specialized되지 않았다"는 점이
문제다.

판정:
- 원인으로 타당
- 다만 단독 대원인보다는 steady-state recv 고정비를 설명하는 축

### 10.4 원인 4: send 경로 함수 호출 depth

이 평가는 방향은 맞지만 우선순위는 낮다.

현재 zlink public send는:

- handle/tag 확인
- flag/part validation
- socket type 분기
- `socket->send()`

를 거친다.

코드:
- [`socket_message_send_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_send_api.cpp)

하지만 현재 gap을 가장 크게 만드는 건
"함수 개수가 많다" 자체보다,

- lifecycle atomic 상태 전이
- recv-side aggregate/TLS 비용
- mode guard

같은 실질 작업이다.

판정:
- `P2` 수준으로 보는 것이 맞다
- inline/합치기만으로 큰 회복을 기대하긴 어렵다

### 10.5 원인 5: `PUBSUB` topic frame 분리 + multipart 경로

이 평가는 대체로 맞다.

코드:
- [`multipart_send_txn.cpp`#L71](/home/hep7/project/kairos/zlink/core/src/core/multipart_send_txn.cpp#L71)
- [`socket_message_recv_api.cpp`#L139](/home/hep7/project/kairos/zlink/core/src/api/socket_message_recv_api.cpp#L139)

현재 zlink publish/subscribe는:

- topic frame 생성
- topic send
- payload send

를 나눠서 수행한다.

subscribe recv도:

- topic frame recv
- payload follow-up recv
- payload export

를 별도로 수행한다.

그래서 `PUBSUB`가 다른 패턴보다 더 크게 벌어지는 설명력은 충분하다.

다만 수정 방향은 조금 보정이 필요하다.

- 현재 `with_zmq single`의 libzmq `PUBSUB` bench는
  [`bench_zmq_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zmq/bench_zmq_pubsub.cpp)
  에서 `send_exact()` + `zmq_msg_recv()`를 사용한다. 즉 현재 비교는
  zlink의 `publish/subscribe` surface와 libzmq의 raw `PUB/SUB` surface를
  비교하는 성격이 있다.
- 따라서 `PUBSUB`는 공통 원인 판단용 1차 신호보다, 패턴 전용 비용을 증폭해
  보여주는 패턴으로 읽는 편이 맞다.
- "topic+payload를 하나의 send로 합친다"는 표현은 wire 의미와 어긋날 수 있다
- 더 정확한 방향은
  - topic 처리의 internal fast path화
  - publish/subscribe 전용 경로에서 불필요한 public aggregate overhead 축소
  - 필요 시 socket 내부 prefix handling 깊게 숨기기

판정:
- `PUBSUB` 한정 고우선 후보로 타당
- 다만 wire/message shape는 유지한 채 최적화해야 한다

### 10.6 원인 6: `ROUTER_ROUTER`의 send+recv 누적 오버헤드

이 평가는 맞다.

`ROUTER_ROUTER`는:

- send 쪽 2-part plain `zlink_send()`가 결국 `send_frames_once()` 루프에서
  `socket->send()`를 두 번 호출하는 비용
- recv 쪽 routed recv + public aggregate export

가 같이 쌓인다.

여기서 `memset(source_rid_out_)` 자체는 미시 비용이라 단독 원인으로 볼 정도는
아니지만, routed surface가 일반 `PAIR`보다 두꺼운 건 맞다.

판정:
- 현상 설명으로 타당
- 특히 최악치가 `ROUTER_ROUTER`에서 나오는 이유를 설명하는 데 유효

### 10.7 원인 7: `command_runtime()` 함수 호출

이 평가는 맞지만 우선순위는 낮다.

코드:
- [`socket_base_msg.cpp`#L206](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp#L206)
- [`socket_runtime.cpp`#L224](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp#L224)

libzmq의 `_ticks` 직접 조작보다 함수 호출이 하나 더 있는 건 사실이다.
하지만 현재 gap 크기를 설명하는 주인공으로 보기엔 약하다.

판정:
- `P2` 이하
- 다른 P0/P1 정리 후에 보는 것이 맞다

### 10.8 추가로 빠져 있었던 축: `PAIR/DEALER`의 `last_recv_source_rid`

원인 메모에서 빠졌지만, 현재 코드 기준으로는 이 항목도 꽤 중요하다.

코드:
- [`pair.cpp`#L124](/home/hep7/project/kairos/zlink/core/src/sockets/pair.cpp#L124)
- [`dealer.cpp`#L105](/home/hep7/project/kairos/zlink/core/src/sockets/dealer.cpp#L105)
- [`socket_base_dispatch.cpp`#L408](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_dispatch.cpp#L408)
- [`socket_runtime.cpp`#L194](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp#L194)

이 작업은 libzmq에는 없는 zlink-specific recv bookkeeping이다.

의미:

- `PAIR/DEALER`처럼 원래 recv path가 짧은 패턴에서
- 메시지마다 source resolve와 구조체 복사가 더 붙는다
- 특히 `inproc/ipc` small-message에서 누적될 가능성이 높다

판정:
- `P1` 상단 후보
- thread-safe를 유지하면서 줄이기 쉬운 축이라 우선 검토 가치가 높다

### 10.9 이전 코드 리뷰에서 별도로 보고했던 이슈도 같이 유지한다

이 문서 앞부분과 본 절의 평가는, 이전 라운드에서 이미 별도로 레포팅했던
아래 이슈를 포함해 다시 정리한 것이다.

- `libzmq`와 비교했을 때 `socket_base_t::send()`의 public lifecycle/lock 계층이
  더 두껍다
- `PAIR`/`DEALER` recv마다 `last_recv_source_rid` bookkeeping이 붙어 있다
- callback handler는 보통 한 번 붙으면 mode가 고정되는데도, steady-state hot
  path가 충분히 mode-specialized되지 않았다
- `recv_msg_internal()`이 direct recv 진입마다 dispatch/mode guard를 수행한다
- `command_runtime()` 추상화는 작은 비용이지만 libzmq 대비 추가 층이다

반대로, 이전 라운드에서 수치상 효과가 있어 보여도 현재 문서에서는
"우선 원인"으로 채택하지 않은 항목도 있다.

- `pipe` steady-state lock 제거

이 항목은 성능 후보가 아니라 "thread-safe 증명이 끝난 뒤 마지막에 재검토할 축"
으로 유지한다. 즉 현재 문서는

- 실제로 gap을 설명하는 고정 per-message 비용
- thread-safe를 깨지 않고 줄일 수 있는 비용

을 우선순위로 둔 정리다.

### 10.10 현재 bench 호출 구조 기준으로 추가 보정이 필요하다

현재 `single zlink` 벤치는 public API 기준으로 측정하고 있다.

코드:
- [`bench_zlink_pair.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pair.cpp)
- [`bench_zlink_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp)
- [`bench_zlink_router_router.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_router_router.cpp)

즉 현재 비교에서 실제로 타는 surface는 다음과 같다.

- `PAIR`: `zlink_send()` + `zlink_recv()`
- `PUBSUB`: `zlink_publish()` + `zlink_subscribe()`
- `ROUTER_ROUTER`: `zlink_send()` + `zlink_recv()`

여기서 중요한 보정은 다음과 같다.

- `ROUTER_ROUTER`는 현재 bench에서 `zlink_send_rid()`가 아니라
  2-part plain `zlink_send()`를 사용한다
- `PAIR`/`DEALER_DEALER`의 libzmq bench는 `zmq_msg_recv()`를 사용하므로,
  해당 패턴에선 zlink의 aggregate recv/TLS export가 상대적으로 직접적인
  고정비다
- `ROUTER`/aggregate helper를 쓰는 일부 std_compat 경로는 비슷한 TLS
  aggregate recv/reset을 수행한다
- 반대로 `send lifecycle coordinator`와 `PAIR/DEALER`의
  `last_recv_source_rid`는 zlink 쪽에 더 특이적인 비용이다

즉 현재 상대 격차를 설명하는 중심은:

- 최근 POSD 리팩토링에서 두꺼워진 public message API 계층
- `PAIR`/`DEALER`의 zlink-only public aggregate recv/TLS export
- zlink recv 진입의 mode guard
- zlink `PAIR/DEALER`의 source RID bookkeeping

이고,

- `PUBSUB`의 topic/payload 분리
- `ROUTER`의 routed out-param 처리
- 일부 std_compat aggregate helper 비용

는 "패턴 전용 surface 차이" 또는 "양쪽에 모두 존재하지만 zlink 쪽 경로가 더
두껍게 누적될 수 있는 항목"으로 보는 편이 정확하다

### 10.11 backpressure 자체는 실제 현상이지만, `perf` 측정은 그 복귀 속도를 거칠게 본다

이 항목은 최근 single perf 진단에서 추가로 확인한 사실이다.

코드:
- [`core/perf/single/common/bench_common.hpp` `single_wait_for_send_backpressure()`](/home/hep7/project/kairos/zlink/core/perf/single/common/bench_common.hpp#L1430)

현재 `core/perf/single`은 send가 `EAGAIN`이면:

- socket `POLLOUT`를 기다리지 않고
- `perf_socket_poll(NULL, 0, 1)`로 1ms idle sleep 후 재시도한다

즉 이 surface는:

- "single에서 sender가 HWM에 자주 닿는다"는 사실을 보여주는 데는 유용하지만
- "queue가 비워진 뒤 얼마나 빨리 복귀하는가"를 정밀하게 재는 벤치로는 거칠다

다만 여기서도 중요한 보정이 있다.

- `with_zmq single`은 이런 1ms sleep이 아니라 blocking send를 사용한다
- 그런데도 zlink가 libzmq보다 밀린다

판정:

- backpressure는 실제 핵심 현상 중 하나다
- 하지만 `core/perf single`의 1ms sleep은 진단용 surface 이슈이지,
  현재 상대 성능 회귀의 단독 원인은 아니다

### 10.12 HWM/LWM 자체는 libzmq와 거의 같은 구조다

이 항목도 backpressure 해석에서 중요하다.

코드:
- [`core/src/core/pipe.cpp` `compute_lwm()`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp#L653)
- [`libzmq/src/pipe.cpp`](/home/hep7/project/kairos/libzmq/src/pipe.cpp)

현재 zlink `pipe`는:

- HWM에 닿으면 `_out_active=false`로 sender를 막고
- reader가 `LWM = HWM / 2`만큼 소비하면 `activate_write`로 다시 깨운다

이 구조는 libzmq와 본질적으로 같다.

판정:

- single에서 queue가 `1000`에 붙는 현상 자체는 새로 생긴 regression이라기보다
  기본적인 backpressure 동작이다
- 최근 회귀를 설명하려면 "막힌 뒤 다시 보내는 core/public 경로가 더 비싸졌는가"
  를 봐야 한다

## 11. 현재 평가 정리

현재 코드 기준으로 보면 우선순위는 아래처럼 보는 것이 가장 맞다.

1. 최근 POSD 리팩토링에서 두꺼워진 public `send/recv` message API fast path
2. `PAIR/DEALER`의 zlink-only public aggregate recv/TLS export 고정비
3. `PAIR/DEALER`의 `last_recv_source_rid` bookkeeping
4. recv mode에서도 남아 있는 dispatch/mode guard
5. `send` lifecycle coordinator와 blocking backpressure 복귀 경로의 비용
6. `PUBSUB`/`ROUTER_ROUTER`의 패턴 전용 surface 차이
7. 함수 호출 depth와 `command_runtime()` 같은 미시 비용

즉 팀장님이 정리하신 의견은 큰 방향에서 맞다.
다만 현재 코드 기준으로는 아래처럼 보정해서 읽는 것이 좋다.

- 최근 회귀의 중심은 POSD 리팩토링 이후 public message API 계층이 두꺼워진 쪽에
  더 가깝다
- `last_recv_source_rid`는 빠졌지만 실제로는 꽤 유력한 추가 축이다
- `recv mode`와 callback/dispatch mode가 충분히 분리되지 않은 것도 중요하다
- `recv_tls_view`도 중요한 후보지만, 예전 `malloc/free(parts)` 급으로
  과장하면 안 된다. 다만 `PAIR`/`DEALER` 비교에선 libzmq 측이 `msg_recv`를
  사용하므로, 이 패턴들에서는 zlink 쪽 상대 비용으로 보는 편이 더 정확하다
- `send atomic 4회`는 지금도 의미 있는 비용이지만, 최근 회귀의 1차 원인으로
  단독 지목하긴 어렵다
- `core/perf single`의 1ms backpressure sleep은 진단 보조 이슈이지,
  blocking send 기반 `with_zmq single`의 상대 격차를 전부 설명하진 못한다
- `함수 개수`보다 `실제 per-message 상태 작업`이 더 중요하다
