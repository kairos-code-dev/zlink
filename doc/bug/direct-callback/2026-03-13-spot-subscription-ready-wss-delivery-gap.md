# SPOT `SUBSCRIPTION_READY` 이후 WSS 전달 불일치 버그 리포트

## 요약

`SPOT` single perf가 startup probe/sleep 없이 monitor event만으로 준비를 확인하도록 정리된 상태에서,
`ZLINK_SPOT_SUB_SUBSCRIPTION_READY`를 확인한 뒤에도 첫 publish가 수신되지 않는 경우가 발생한다.

핵심은 perf 보정 문제가 아니라, `SUBSCRIPTION_READY` 이벤트 의미와 실제 data-plane readiness가
일치하지 않는다는 점이다. 팀장님 지시대로 event 확인 후 통신이 안 되면 bug로 보고한다.

## 상태 업데이트 (2026-03-13)

- 가이드와 monitor contract 스펙에서는 이제 `SUBSCRIPTION_READY`를
  delivery gate로 쓰지 않는다.
- 현재 canonical gate는 `*_DELIVERY_READY_CHANGED`다.
- 즉 이 리포트의 원래 문제 제기는 "왜 `SUBSCRIPTION_READY`만으로는 안 되나"에 대한
  semantic 정리는 끝난 상태다.
- 다만 WSS single perf 자체는 아직 실패한다.

```bash
timeout 45s env PERF_SINGLE_DURATION_SECONDS=2 \
  PERF_SINGLE_SNDTIMEO_MS=200 PERF_SINGLE_RCVTIMEO_MS=200 \
  ./core/build/bin/perf_spot current wss 64
```

- 관측 결과: `exit=1`, throughput/bandwidth/latency 전부 `0.00`
- 따라서 WSS delivery 문제는 닫지 않는다.
- 다만 current contract 기준으로는 이 리포트보다
  `single-spot-delivery-ready-flake.md`의 후속 이슈로 보는 것이 더 정확하다.

## 현재 perf 측 조건

현재 [perf_spot.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/perf/single/src/perf_spot.cpp#L699) 경로는 아래 순서만 사용한다.

1. `zlink_spot_monitor_open(... ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY ...)`
2. `zlink_spot_node_bind()`
3. `zlink_spot_node_connect_peer_pub()`
4. `zlink_spot_subscribe()`
5. `wait_for_pub_peers()`
6. `wait_for_sub_peers()`
7. `wait_for_service_event(... ZLINK_SPOT_SUB_FILTER_APPLIED ...)`
8. `wait_for_service_event(... ZLINK_SPOT_SUB_SUBSCRIPTION_READY ...)`
9. 바로 warmup publish 시작

추가 startup probe, retry loop, post-event sleep settle은 넣지 않았다.

## 재현 명령

```bash
cmake --build /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build --target perf_spot -j$(nproc)
./core/build/bin/perf_spot current wss 64
```

같은 명령을 연속 실행하면 실패/성공이 섞여 나온다.

## 관측 결과

실패 사례:

```text
RESULT,current,SPOT,wss,64,throughput,0.00
RESULT,current,SPOT,wss,64,bandwidth,0.00
RESULT,current,SPOT,wss,64,latency,0.00
RESULT,current,SPOT,wss,64,latency_p95,0.00
RESULT,current,SPOT,wss,64,latency_p99,0.00
RESULT,current,SPOT,wss,64,snd_pending_max,162.00
RESULT,current,SPOT,wss,64,rcv_pending_max,0.00
RESULT,current,SPOT,wss,64,rcv_pending_end,0.00
exit=1
```

직후 동일 명령 성공 사례:

```text
RESULT,current,SPOT,wss,64,throughput,12093.60
RESULT,current,SPOT,wss,64,bandwidth,0.77
RESULT,current,SPOT,wss,64,latency,133.10
RESULT,current,SPOT,wss,64,latency_p95,195.00
RESULT,current,SPOT,wss,64,latency_p99,317.00
RESULT,current,SPOT,wss,64,snd_pending_max,979.00
RESULT,current,SPOT,wss,64,rcv_pending_max,63.00
RESULT,current,SPOT,wss,64,rcv_pending_end,11.00
exit=0
```

즉, 동일한 `SUBSCRIPTION_READY` gate를 지난 뒤에도 실제 첫 delivery는 불안정하다.

## 기대 동작

`ZLINK_SPOT_SUB_SUBSCRIPTION_READY`가 발생했다면, 그 시점 이후 첫 `zlink_spot_publish()`는
transport 종류와 무관하게 subscriber까지 도달해야 한다.

실패가 난다면 monitor event가 너무 이르게 발생했거나, control-plane readiness와 data-plane readiness가
분리돼 있는 것이다.

## suspect 영역

- [spot_sub.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_sub.cpp#L580)
  - `ZLINK_EVENT_CONNECTION_READY` 시점에 `emit_subscription_ready_event()`를 내보내는 로직
- [spot_sub.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_sub.cpp#L373)
  - `emit_subscription_ready_event()`
- `spot` control/data plane 사이 subscription forwarding 완료 시점
- WSS handshake 완료와 `SUBSCRIPTION_READY` publish 가능 시점의 ordering

현재 코드상 `SUBSCRIPTION_READY`는 socket monitor의 `CONNECTION_READY`와 로컬 filter 존재 여부를
기준으로 발생한다. 이 이벤트가 실제 publish delivery 보장 시점보다 앞설 가능성이 있다.

## 영향

- single perf full gate를 신뢰성 있게 닫을 수 없다.
- `SPOT`의 WSS readiness contract가 모호해 perf 쪽에서 event-only gate를 사용할 수 없다.
- perf workaround를 더 넣는 대신 core에서 readiness 의미를 정리해야 한다.

## 요청 사항

1. `SUBSCRIPTION_READY`가 실제 첫 delivery 가능 시점과 일치하는지 확인
2. WSS transport에서 `CONNECTION_READY`와 subscription forwarding 완료 순서 점검
3. 필요 시 `SUBSCRIPTION_READY` emit 조건을 data-plane readiness 기준으로 조정
4. 수정 후 `./core/build/bin/perf_spot current wss 64` 연속 실행에서 fail/success flake가 사라지는지 확인

## 현 이벤트로 해결이 안 되면 필요한 추가 이벤트

가장 좋은 해결은 기존 `ZLINK_SPOT_SUB_SUBSCRIPTION_READY` 의미를 강화하는 것이다.
perf는 새 완충/재시도 로직 없이 이 이벤트만 기다리고 바로 publish 해야 한다.

만약 기존 이벤트 의미를 유지해야 해서 강화를 못 한다면, 아래 성격의 새 이벤트가 필요하다.

- 제안 이름: `ZLINK_SPOT_SUB_DELIVERY_READY`
- 주체: `spot sub` handle / node monitor
- detail: 기존과 동일하게 endpoint 포함
- 의미:
  - local filter 적용 완료
  - remote peer 연결/handshake 완료
  - subscription forwarding 완료
  - 첫 `zlink_spot_publish()`가 추가 sleep/retry 없이 subscriber까지 도달 가능한 상태
- 금지:
  - `CONNECTION_READY` 직후처럼 실제 첫 delivery 보장 전 선행 emit 금지

perf 쪽 요구사항은 단순하다. `SUBSCRIPTION_READY`를 고치든 `DELIVERY_READY`를 추가하든,
monitor event 하나만으로 정확한 송수신 시작 조건이 보장돼야 한다.
