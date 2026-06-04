# Monitoring 성능 개선 계획

> 범위: `core/` 내부의 socket monitor 및 service monitor 경로가 socket messaging 성능에 주는 영향을 분석하고 개선 우선순위를 정리한다.

## 1. 결론

### 1.1 핵심 판단

- 일반적인 `socket_base_t::send()` / `recv()` steady-state hot path에 monitor 수집 로직이 직접 섞여 있지는 않다.
- 그러나 monitoring이 완전히 공짜는 아니다.
- 특히 다음 세 경우에는 messaging 성능이나 I/O 진행성에 간접 영향을 줄 수 있다.

1. socket lifecycle event를 monitor로 내보낼 때, 이벤트 발생 thread가 동기식으로 monitor socket에 직접 전송한다.
2. `STREAM` raw dispatch처럼 실제 메시지 dispatch 직전 monitor event를 발생시키는 경로가 있다.
3. service monitor fanout이 watcher 수에 비례하는 비용을 lock 안에서 수행한다.

### 1.2 이번 문서에서 성능 문제로 보는 기준

- steady-state message throughput 저하 가능성
- latency spike 가능성
- topology churn / connect storm 시 CPU 사용량 급증
- monitoring consumer가 느릴 때 producer thread가 영향받는 구조

---

## 2. 현재 구조 요약

### 2.1 Socket monitor

- socket monitor는 [`core/src/sockets/socket_base.cpp`](../../../../zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp)에서 관리된다.
- event 발생 시 [`socket_base.cpp#L2358`](../../../../zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp#L2358) `socket_base_t::event()`가 `_monitor_sync`를 잡고 monitor 대상 여부를 확인한다.
- monitor 대상이면 [`socket_base.cpp#L2373`](../../../../zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp#L2373) `monitor_event()`가 multipart 메시지를 monitor socket으로 직접 전송한다.
- monitor socket은 [`socket_base.cpp#L2165`](../../../../zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp#L2165) `monitor()`에서 생성된다.

### 2.2 Service monitor

- service monitor fanout은 [`core/src/services/common/service_monitor.cpp`](../../../../zlink-direct-callback-rewrite/core/src/services/common/service_monitor.cpp)에서 수행된다.
- [`service_monitor.cpp#L178`](../../../../zlink-direct-callback-rewrite/core/src/services/common/service_monitor.cpp#L178) `service_monitor_hub_t::emit()`가 `_sync` lock 안에서 watcher를 순회하며 각 watcher socket으로 `send(DONTWAIT)`를 수행한다.

### 2.3 Monitor callback worker

- direct callback 방식은 [`core/src/api/zlink.cpp#L419`](../../../../zlink-direct-callback-rewrite/core/src/api/zlink.cpp#L419) `monitor_handler_worker()`가 별도 thread에서 monitor socket을 drain하면서 handler를 호출한다.
- 이 worker 자체는 message send/recv thread와 분리되어 있다는 점은 장점이다.
- 다만 producer 쪽 전송이 이미 동기식이면 consumer worker의 분리는 producer 보호를 완전히 보장하지 못한다.

---

## 3. 성능 영향 지점

## 3.1 P0: Socket monitor event 전송이 동기식이고 lock-held 구간이 길다

### 위치

- [`core/src/sockets/socket_base.cpp#L2358`](../../../../zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp#L2358)
- [`core/src/sockets/socket_base.cpp#L2373`](../../../../zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp#L2373)

### 현재 동작

- `socket_base_t::event()`는 `_monitor_sync`를 획득한다.
- monitor mask가 맞으면 같은 lock 구간 안에서 `monitor_event()`를 호출한다.
- `monitor_event()`는 monitor socket으로 다음 프레임들을 순차 전송한다.
  - event id
  - values count
  - values
  - routing id
  - local endpoint
  - remote endpoint

### 문제

- event 1건당 여러 번의 `zlink_msg_init_size()`, `memcpy()`, `zlink_msg_send()`가 실행된다.
- 이 비용이 `_monitor_sync` lock을 쥔 상태에서 수행된다.
- monitor consumer가 느리거나 queue가 차면 event emitter thread가 지연될 수 있다.
- socket monitor는 `DONTWAIT`가 아니라 일반 send 경로를 사용한다.

### 영향 범위

- steady-state payload send/recv 자체보다는 connect/disconnect/handshake/ready event가 많은 상황에서 영향이 크다.
- 특히 peer churn이 많은 환경에서 latency spike 원인이 될 수 있다.

### 판단

- monitoring이 messaging 성능에 영향을 주면 안 된다는 기준에서는 가장 먼저 손봐야 할 지점이다.

---

## 3.2 P0: Service monitor fanout이 watcher 수에 비례하고 lock 안에서 실행된다

### 위치

- [`core/src/services/common/service_monitor.cpp#L178`](../../../../zlink-direct-callback-rewrite/core/src/services/common/service_monitor.cpp#L178)

### 현재 동작

- `service_monitor_hub_t::emit()`가 `_sync` lock을 잡는다.
- watcher 전체를 순회한다.
- 각 watcher마다 메시지를 새로 할당하고 `send(DONTWAIT)`를 호출한다.
- send 실패 시 해당 watcher를 즉시 닫고 vector에서 제거한다.

### 문제

- event 1건의 비용이 O(number of watchers)다.
- watcher가 많을수록 fanout 비용이 선형 증가한다.
- fanout 전체가 lock 안에서 이뤄져 monitor open/close와 contention이 생길 수 있다.
- 메시지 할당도 watcher마다 반복된다.

### 영향 범위

- gateway/discovery/spot 계열 service monitor를 많이 붙여 쓰는 환경
- topology summary나 상태 변화가 잦은 환경

### 판단

- block 위험은 socket monitor보다 낮지만 watcher 수가 커질수록 비용이 예측 가능하게 증가한다.

---

## 3.3 P1: `STREAM` raw dispatch 경로에 monitor event가 직접 붙어 있다

### 위치

- [`core/src/sockets/stream.cpp#L578`](../../../../zlink-direct-callback-rewrite/core/src/sockets/stream.cpp#L578)
- [`core/src/sockets/stream.cpp#L636`](../../../../zlink-direct-callback-rewrite/core/src/sockets/stream.cpp#L636)
- [`core/src/sockets/stream.cpp#L694`](../../../../zlink-direct-callback-rewrite/core/src/sockets/stream.cpp#L694)

### 현재 동작

- `stream_t::xstream_dispatch_msg()`는 raw callback을 호출하기 전에 routing id를 정리한다.
- 그 과정에서 `maybe_emit_connect_event()`를 호출한다.
- `maybe_emit_connect_event()`는 최초 연결 ready 시 `event_connection_ready()`를 발생시킨다.
- 결국 실제 inbound message dispatch 직전에 socket monitor 전송 경로로 들어간다.

### 문제

- 이 경로는 단순 lifecycle thread가 아니라 message dispatch 경로에 가깝다.
- 최초 message 수신 지점에서 monitor 전송 비용이 직결된다.
- 현재는 `pipe_t::mark_stream_connect_event_emitted()`로 중복 emit은 막고 있지만, 첫 메시지 latency에는 여전히 영향이 있다.

### 영향 범위

- `STREAM` socket raw dispatch 사용 시
- 연결 수가 많고 첫 메시지 latency가 중요한 경우

### 판단

- steady-state 영향은 제한적이지만 경로의 성격상 "messageing에 영향 주지 않아야 한다" 기준에는 부적합하다.

---

## 3.4 P1: Spot pub/sub monitor queue는 producer를 완전히 분리하지 못한다

### 위치

- [`core/src/services/spot/spot_pub.cpp#L156`](../../../../zlink-direct-callback-rewrite/core/src/services/spot/spot_pub.cpp#L156)
- [`core/src/services/spot/spot_sub.cpp#L247`](../../../../zlink-direct-callback-rewrite/core/src/services/spot/spot_sub.cpp#L247)

### 현재 동작

- `emit_monitor_event()`는 event를 queue에 넣는다.
- 이어서 현재 thread가 drainer를 획득하면 같은 함수 안에서 queue를 끝까지 drain한다.
- drain 중에는 `_monitor.emit()`를 직접 호출한다.

### 문제

- 겉으로는 queue 기반이지만 producer가 실제 fanout 비용을 부담할 수 있다.
- 다수 event가 짧은 시간에 몰리면 enqueue만 하고 끝나는 것이 아니라 producer thread가 drain 작업까지 떠안을 수 있다.
- `_monitor_event_pending.fetch_sub()`가 drain된 event 수만큼 반복되어 atomic RMW도 많다.

### 영향 범위

- spot pub/sub 상태 변화가 burst로 발생할 때
- readiness 관련 event가 한꺼번에 몰릴 때

### 판단

- 구조적 문제는 있지만 socket monitor 동기 전송보다는 우선순위가 한 단계 낮다.

---

## 3.5 P1: Spot pub/sub는 raw monitor event 1건을 service event 2건으로 증폭한다

### 위치

- [`core/src/services/spot/spot_pub.cpp#L538`](../../../../zlink-direct-callback-rewrite/core/src/services/spot/spot_pub.cpp#L538)
- [`core/src/services/spot/spot_sub.cpp#L1356`](../../../../zlink-direct-callback-rewrite/core/src/services/spot/spot_sub.cpp#L1356)

### 현재 동작

- `CONNECTION_READY`를 `READY` + `PEER_UP` 두 event로 변환한다.
- `DISCONNECTED`를 `LOST` + `PEER_DOWN` 두 event로 변환한다.

### 문제

- raw event 1건이 service event 2건으로 늘어난다.
- 이후 queue enqueue, drain, watcher fanout도 2배로 따라간다.
- event 의미론이 꼭 두 개로 분리되어야 하는지 검토가 필요하다.

### 영향 범위

- spot 연결 churn이 많은 상황
- monitor watcher가 여러 개 붙은 상황

### 판단

- 의미론 유지가 중요해서 위험도는 약간 있지만 효과 대비 개선 여지가 크다.

---

## 3.6 P2: Gateway는 monitor event 처리와 service monitor emit을 상태 전이 경로에서 동기 수행한다

### 위치

- [`core/src/services/gateway/gateway.cpp#L1756`](../../../../zlink-direct-callback-rewrite/core/src/services/gateway/gateway.cpp#L1756)
- [`core/src/services/gateway/gateway.cpp#L2128`](../../../../zlink-direct-callback-rewrite/core/src/services/gateway/gateway.cpp#L2128)

### 현재 동작

- gateway가 socket monitor event를 해석한다.
- route up/down, send-ready changed 등을 만들고 즉시 `_monitor.emit(ev)`를 호출한다.

### 문제

- gateway의 route state 처리와 monitor fanout이 분리되어 있지 않다.
- route churn이 많을수록 service monitor emit 비용이 gateway 상태 처리 비용에 직접 더해진다.

### 영향 범위

- gateway peer 수가 많고 reconnect가 잦은 환경

### 판단

- 구조 자체는 이해 가능하지만 monitor fanout을 동기 수행하는 현재 방식은 확장성에 불리하다.

---

## 3.7 참고: monitor callback worker 자체는 주된 병목이 아니다

### 위치

- [`core/src/api/zlink.cpp#L419`](../../../../zlink-direct-callback-rewrite/core/src/api/zlink.cpp#L419)

### 현재 동작

- worker thread가 `DONTWAIT`로 monitor socket을 drain한다.
- 비어 있으면 10ms sleep 후 다시 돈다.

### 판단

- 이 구조는 consumer 측 polling 효율 문제는 있을 수 있어도 producer 측 messaging 성능의 1차 원인은 아니다.
- 현재 성능 개선 우선순위에서는 후순위다.

---

## 4. 개선 원칙

### 4.1 목표

- monitor 미사용 시 비용을 사실상 0으로 만든다.
- monitor 사용 시에도 producer thread가 fanout이나 serialization 비용을 직접 부담하지 않게 한다.
- event 의미론은 유지하되, 불필요한 증폭은 줄인다.

### 4.2 원칙

1. fast path에서 lock과 allocation을 피한다.
2. event producer와 monitor fanout을 분리한다.
3. 한 event당 메시지 할당 횟수와 send 횟수를 줄인다.
4. watcher 수 증가에 따른 비용 증가를 완만하게 만든다.

---

## 5. 개선안

## 5.1 P0: Socket monitor fast-path early exit 추가

### 제안

- `_monitor_events`를 그대로 두되, lock 없이 읽을 수 있는 atomic mirror를 추가한다.
- `socket_base_t::event()`에서 monitor 미등록 상태면 `_monitor_sync`를 잡지 않고 즉시 return 한다.

### 기대 효과

- monitor 미사용 시 비용을 거의 0으로 만든다.
- 현재 초안의 방향은 맞다. 이 항목은 유지해도 된다.

### 주의

- mask와 socket pointer visibility를 함께 맞춰야 하므로 memory ordering과 lock 경계를 명확히 해야 한다.

---

## 5.2 P0: Socket monitor 전송을 producer thread 밖으로 분리

### 제안

- 가장 바람직한 방향은 `socket_base_t::event()`에서 직접 multipart send를 하지 않는 것이다.
- producer는 event record를 lock-free 또는 bounded queue에 적재만 한다.
- 전용 monitor dispatch thread 또는 기존 monitor consumer와 분리된 sender가 실제 socket 송신을 담당한다.

### 기대 효과

- producer thread가 monitor consumer 속도에 영향받지 않는다.
- `_monitor_sync` lock-held 구간이 대폭 짧아진다.

### 주의

- shutdown 시 flush/drop 정책을 명확히 해야 한다.
- event ordering 보장 범위를 정의해야 한다.

### 비고

- 초안의 "6-frame -> 1-message"보다 우선순위가 높다.
- frame 수를 줄여도 동기 송신 자체가 남아 있으면 producer 보호는 충분하지 않다.

---

## 5.3 P1: Socket monitor serialization 비용 축소

### 제안

- 동기/비동기 여부와 별개로, event 1건당 여러 frame을 만드는 현재 구조는 비효율적이다.
- 내부 inproc 전용 포맷이므로 단일 buffer serialization이나 더 compact한 고정 포맷으로 바꾸는 방안을 검토한다.

### 기대 효과

- allocation 횟수와 send 호출 횟수 감소
- connect storm 시 CPU와 allocator pressure 감소

### 주의

- [`core/src/services/common/monitor_decode.hpp`](../../../../zlink-direct-callback-rewrite/core/src/services/common/monitor_decode.hpp) 및 API layer decode 경로를 같이 바꿔야 한다.
- wire format 변경 성격이 있으므로 별도 단계로 분리하는 것이 맞다.

---

## 5.4 P0: Service monitor watcher 0 fast-path 추가

### 제안

- `service_monitor_hub_t`에 atomic watcher count를 둔다.
- watcher가 0이면 `emit()`에서 lock 없이 즉시 return 한다.

### 기대 효과

- monitor consumer가 없을 때 service monitor 비용을 거의 제거한다.

### 주의

- open/close와 count update 순서를 보장해야 한다.

---

## 5.5 P1: Service monitor fanout에서 per-watcher allocation 제거

### 제안

- event payload를 1회만 준비한 뒤 watcher별로 공유 가능한 메시지 복제 전략을 검토한다.
- `msg_t::copy()` 또는 유사한 refcount copy가 안전하게 가능한지 먼저 확인한다.

### 기대 효과

- watcher 수가 많을 때 allocation pressure 감소

### 주의

- `msg_t` copy semantics 확인이 선행되어야 한다.
- send 실패 시 close/erase 흐름과 소유권 모델을 명확히 해야 한다.

---

## 5.6 P1: Spot pub/sub drain 경로를 진짜 비동기로 분리하거나 batch atomic으로 완화

### 제안

- 최선은 dedicated drainer thread 또는 기존 event loop에 drain 책임을 넘기는 것이다.
- 그 전 단계 quick win으로는 drain batch당 `fetch_sub()` 1회만 수행하도록 바꾼다.

### 기대 효과

- producer thread의 drain 부담 감소
- burst 시 atomic RMW 감소

### 주의

- 상태 플래그 `_monitor_event_draining`, `_monitor_event_pending`의 경쟁 상태를 테스트로 보강해야 한다.

---

## 5.7 P1: Spot event 증폭 정책 재검토

### 제안

- `READY + PEER_UP`, `LOST + PEER_DOWN`를 항상 둘 다 보내야 하는지 의미론을 다시 확인한다.
- 둘 다 유지해야 한다면 batch emit API를 도입해 drainer 획득과 fanout 준비를 1회로 줄인다.
- 둘 중 하나로 충분하다면 event 수 자체를 줄인다.

### 기대 효과

- monitor burst 시 총 event 수 감소
- spot monitor queue 및 service fanout 비용 완화

### 주의

- API 의미론을 바꾸는 경우 문서와 테스트를 함께 수정해야 한다.

---

## 5.8 P2: Gateway monitor 처리량 상한과 비동기화 검토

### 제안

- 한 번의 processing cycle에서 처리할 monitor event 수에 상한을 둔다.
- 장기적으로는 gateway 상태 계산과 monitor fanout을 분리한다.

### 기대 효과

- churn 시 한 thread가 monitor 처리에 과도하게 오래 붙잡히는 것을 막을 수 있다.

### 주의

- 상태 반영 지연이 허용 가능한 범위인지 확인해야 한다.

---

## 6. 우선순위

| 우선순위 | 항목 | 이유 |
|---|---|---|
| P0 | socket monitor fast-path early exit | monitor 미사용 비용 제거 |
| P0 | socket monitor producer/consumer 분리 | producer 보호에 가장 직접적 |
| P0 | service monitor watcher 0 fast-path | consumer 없을 때 비용 제거 |
| P1 | service monitor per-watcher allocation 축소 | watcher 수 증가 대응 |
| P1 | spot drain batch 최적화 또는 비동기화 | burst 완화 |
| P1 | spot event 증폭 완화 | 총 fanout 이벤트 수 감소 |
| P1 | socket monitor serialization 단순화 | 동기 비용 자체 감소 |
| P2 | gateway bounded processing | churn 시 공정성 개선 |

---

## 7. 실행 단계

### 단계 1: 안전한 quick win

- socket monitor fast-path early exit
- service monitor watcher 0 fast-path
- spot drain batch `fetch_sub()` 최적화

### 단계 2: producer 보호

- socket monitor producer/consumer 분리 설계 및 적용
- service monitor allocation 축소
- spot batch emit 또는 비동기 drain 도입

### 단계 3: 포맷 및 의미론 정리

- socket monitor serialization format 단순화
- spot event 의미론 재정리
- gateway monitor 처리 모델 조정

---

## 8. 검증 항목

### 측정 시나리오

1. monitor 미등록 상태에서 steady-state send/recv latency
2. 100~1000 peer connect storm에서 p50/p99 latency
3. watcher 수 0, 1, 10, 100에 대한 service monitor emit 비용
4. spot pub/sub readiness churn 시 event 수와 drain 시간
5. gateway route churn 시 processing cycle 점유 시간

### 성공 기준

- monitor 미등록 시 steady-state 오버헤드가 통계적으로 무시 가능한 수준일 것
- monitor 사용 시 producer thread latency spike가 현저히 줄어들 것
- watcher 수 증가에 따른 service monitor 비용 증가율이 완만해질 것

---

## 9. 정리

- 현재 구현은 "steady-state message path에 monitor 코드가 직접 섞여 있다" 수준은 아니다.
- 하지만 "monitoring이 messageing 성능에 영향을 주면 안 된다"는 요구에는 아직 미달이다.
- 가장 큰 이유는 socket monitor event 송신이 producer thread에서 동기식으로 수행된다는 점이다.
- 다음으로 service monitor fanout의 선형 비용과, spot 계열의 event 증폭 및 inline drain이 영향을 키운다.
- 따라서 개선 우선순위는 "포맷 최적화"보다 먼저 "producer 보호"와 "fanout 비용 절감"에 두는 것이 맞다.
