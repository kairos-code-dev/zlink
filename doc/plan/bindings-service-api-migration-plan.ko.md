# Bindings Service API Migration Plan

## 1. 목적

이 문서는 `core`에서 완료된 service public API 정리를 기준으로,
언어별 bindings 담당자가 어떤 API를 제거하고 무엇으로 포팅해야 하는지
빠르게 판단할 수 있도록 정리한 전달 문서다.

이번 변경의 핵심은 다음 4가지다.

- `poller = data readiness`
- `monitor = state transition`
- `registry topology = global summary`
- `service-level option / routing id = public configuration surface`

즉, 사용자는 더 이상 내부 socket role이나 raw internal socket을 알아야 하지
않고, `Gateway`, `Receiver`, `SpotPub`, `SpotSub`, `Discovery` 같은
service subject 기준으로 API를 사용해야 한다.

## 2. 범위

이번 문서는 bindings 포팅 대상만 다룬다.

- Java
- .NET
- Python
- Node
- C++

다음 항목은 이미 `core`에서 반영 완료되었다.

- legacy service socket-role API 제거
- raw/unsafe service socket getter 제거
- service-level option API 정리
- representative routing id API 정리
- service monitor / registry topology API 추가

## 3. 이번 core 변경의 최종 방향

### 3.1 Gateway

- public option:
  - `zlink_gateway_set_option`
- public identity:
  - `zlink_gateway_set_routing_id`
  - `zlink_gateway_routing_id`
- public readiness:
  - `zlink_poller_add_gateway`
  - `zlink_gateway_monitor_open`
  - `zlink_poller_add_monitor`
- debug/inspection:
  - `zlink_gateway_router_peers`
  - `zlink_gateway_connection_count`

### 3.2 Receiver

- public option:
  - `zlink_receiver_set_option`
- public identity:
  - `zlink_receiver_set_routing_id`
  - `zlink_receiver_routing_id`
- public data path:
  - `zlink_receiver_recv`
  - `zlink_receiver_last_endpoint`
  - `zlink_receiver_peer_info`
- public readiness:
  - `zlink_poller_add_receiver`
  - `zlink_receiver_monitor_open`
  - `zlink_poller_add_monitor`

### 3.3 Spot

- `SpotNode`
  - bind/connect/discovery/TLS wiring owner만 담당
  - public option surface 아님
  - public raw socket owner 아님
- `SpotPub`
  - `zlink_spot_pub_set_option`
  - `zlink_spot_pub_set_routing_id`
  - `zlink_spot_pub_routing_id`
  - `zlink_spot_pub_peers`
  - `zlink_spot_pub_monitor_open`
  - `zlink_poller_add_spot_pub`
- `SpotSub`
  - `zlink_spot_sub_set_option`
  - `zlink_spot_sub_set_routing_id`
  - `zlink_spot_sub_routing_id`
  - `zlink_spot_sub_peers`
  - `zlink_spot_sub_monitor_open`
  - `zlink_poller_add_spot_sub`

### 3.4 Discovery / Registry

- `Discovery`
  - representative RID 대상
  - monitor 대상
  - option surface 1차 대상 아님
- `Registry topology`
  - global summary 조회용
  - local monitor의 대체가 아니라 1차 진단 surface

## 4. 제거된 core public API

아래 API는 `core` public surface에서 제거되었다.

### 4.1 Gateway / Receiver

- `zlink_gateway_setsockopt`
- `zlink_gateway_router_socket`
- `ZLINK_GATEWAY_SOCKET_ROUTER`
- `zlink_receiver_setsockopt`
- `ZLINK_RECEIVER_SOCKET_ROUTER`
- `ZLINK_RECEIVER_SOCKET_DEALER`

### 4.2 Spot

- `zlink_spot_node_setsockopt`
- `zlink_spot_node_pub_socket`
- `zlink_spot_node_sub_socket`
- `ZLINK_SPOT_NODE_SOCKET_NODE`
- `ZLINK_SPOT_NODE_SOCKET_PUB`
- `ZLINK_SPOT_NODE_SOCKET_SUB`
- `ZLINK_SPOT_NODE_SOCKET_DEALER`
- `ZLINK_SPOT_NODE_OPT_PUB_MODE`
- `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM`
- `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_FULL_POLICY`

주의:

- `ZLINK_SPOT_NODE_PUB_MODE_SYNC/ASYNC`
- `ZLINK_SPOT_NODE_PUB_QUEUE_FULL_EAGAIN/DROP`

이 값 상수는 아직 public meaning value로 유지된다.
즉 `SpotPub` option 값으로는 계속 사용할 수 있다.

## 5. bindings에서 해야 할 직접 포팅

### 5.1 Gateway

기존:

- raw router handle getter wrapper
- `SetSockOpt` / `setSockOpt` / `setsockopt` wrapper

변경:

- raw router handle getter 제거
- `setOption(...)`만 유지
- readiness는:
  - `Poller.addGateway(...)`
  - `Gateway.openMonitor()` + `Poller.addMonitor(...)`

### 5.2 Receiver

기존:

- role 기반 `setsockopt(receiver, role, option, ...)`
- raw router handle getter 또는 내부 socket access

변경:

- `Receiver.setOption(...)`만 유지
- receive path는:
  - `Receiver.recv(...)`
  - `Receiver.lastEndpoint()`
  - `Receiver.peerInfo(...)`
- readiness는:
  - `Poller.addReceiver(...)`
  - `Receiver.openMonitor()` + `Poller.addMonitor(...)`

### 5.3 Spot

기존:

- `SpotNode.setSockOpt(role, option, ...)`
- `SpotNode.pubSocket()/subSocket()`
- `SpotNode`를 실사용 public API처럼 노출

변경:

- `SpotNode`는 wiring owner만 유지
- `SpotPub.setOption(...)`
- `SpotSub.setOption(...)`
- `SpotPub.routingId()/setRoutingId(...)`
- `SpotSub.routingId()/setRoutingId(...)`
- `SpotPub.peers()`
- `SpotSub.peers()`
- readiness는:
  - `Poller.addSpotPub(...)`
  - `Poller.addSpotSub(...)`
  - `SpotPub.openMonitor()`
  - `SpotSub.openMonitor()`

## 6. migration map

### 6.1 Gateway / Receiver

| 기존 binding surface | 새 binding surface |
|---|---|
| `gateway.setSockOpt(...)` | `gateway.setOption(...)` |
| `gateway.routerSocket()` | 제거 |
| `receiver.setSockOpt(role=router, ...)` | `receiver.setOption(...)` |
| `receiver.setSockOpt(role=dealer, ...)` | 제거 |
| `receiver.routerSocket()` | 제거 |
| raw socket에서 `LAST_ENDPOINT` 조회 | `receiver.lastEndpoint()` |
| raw socket direct recv | `receiver.recv(...)` |

### 6.2 Spot

| 기존 binding surface | 새 binding surface |
|---|---|
| `spotNode.setSockOpt(pub, SNDHWM, ...)` | `spotPub.setOption(SNDHWM, ...)` |
| `spotNode.setSockOpt(sub, RCVTIMEO, ...)` | `spotSub.setOption(RCVTIMEO, ...)` |
| `spotNode.setSockOpt(node, PUB_MODE, ...)` | `spotPub.setOption(MODE, ...)` |
| `spotNode.setSockOpt(node, PUB_QUEUE_HWM, ...)` | `spotPub.setOption(QUEUE_HWM, ...)` |
| `spotNode.setSockOpt(node, PUB_QUEUE_FULL_POLICY, ...)` | `spotPub.setOption(QUEUE_FULL_POLICY, ...)` |
| `spotNode.pubSocket()` | 제거 |
| `spotNode.subSocket()` | 제거 |
| `spotNode`를 pollable/data-plane 진입점으로 사용 | `spotPub` / `spotSub` + poller/monitor 로 변경 |

## 7. 언어별 구현 지침

### 7.1 Java

- `Gateway`, `Receiver`, `SpotPub`, `SpotSub`에 API를 집중
- `SpotNode`는 bind/connect/discovery/TLS만 남김
- `Poller`는 기존 poller에 service / monitor를 add하는 방식 유지
- 예외 메시지는 제거된 native symbol 이름이 아니라 새 surface 이름 기준으로 정리

### 7.2 .NET

- `NativeMethods`에서 제거된 core 심볼 삭제
- service class는 raw `IntPtr socket` 노출을 없앰
- `Dispose` 패턴은 monitor handle 포함해서 유지

### 7.3 Python

- `_ffi.py`, `_native.py`에서 제거된 심볼 선언 삭제
- `_spot.py`, `_discovery.py`의 role 기반 설정 API 정리
- user-facing Python class는 `set_option`, `routing_id`, `open_monitor` 중심으로 정리

### 7.4 Node

- addon에서 제거된 core 심볼 binding 삭제
- service-level `setOption`, `openMonitor`, poller add 계열 wrapper 유지
- raw socket getter 노출 제거

### 7.5 C++

- `compat.hpp`, `services/*.hpp`, `types.hpp`에서 role enum과 removed wrapper 제거
- C++ thin wrapper도 `service-level option` 기준으로 단순화
- 기존 core mode-split 테스트 포팅

## 8. 테스트 우선순위

bindings 담당자는 최소 아래를 먼저 통과시키는 것이 좋다.

### 8.1 기능

- Gateway service poller
- Receiver recv / lastEndpoint
- SpotPub / SpotSub option
- SpotPub / SpotSub peers
- service monitor open / recv

### 8.2 회귀

- Gateway handover 관련 기존 포팅 테스트
- Spot send blocking wakeup 포팅 테스트
- monitor + poller 같은 루프에서 처리하는 통합 테스트

### 8.3 perf

bindings perf가 있다면 최소:

- single:
  - `GATEWAY`
  - `SPOT`
- multi:
  - `GATEWAY`
  - `SPOT`

서비스 readiness/setup이 제거된 raw socket getter에 의존하지 않는지
먼저 확인해야 한다.

## 9. 주의사항

### 9.1 bindings는 현재 깨져 있을 가능성이 높다

이번 core 변경으로 인해 아래 종류의 코드는 모두 깨질 수 있다.

- 제거된 native symbol lookup
- role enum 상수 참조
- raw socket getter 호출
- old `setsockopt` wrapper 호출

즉 bindings는 단순 재빌드가 아니라 source migration이 필요하다.

### 9.2 Spot에서 가장 많이 바뀐다

`SpotNode`를 실제 public data-plane owner처럼 쓰던 바인딩은
가장 수정량이 많다.

정리 기준은 한 줄이다.

- `SpotNode`는 wiring owner
- `SpotPub` / `SpotSub`가 실제 public service surface

### 9.3 monitor는 두 방식 다 지원해야 한다

bindings는 monitor를:

- 직접 blocking/nonblocking recv
- 기존 poller에 add

둘 다 지원하는 방향으로 유지하는 것이 좋다.

## 10. 현재 core 검증 상태

이번 정리 후 core에서 확인한 상태는 다음과 같다.

- targeted ctest pass
  - gateway
  - service discovery / introspection
  - spot pubsub / scenario / mode split / send wakeup
- representative perf pass
  - single `GATEWAY,SPOT`
  - multi `GATEWAY,SPOT`

즉 bindings migration은 새 core public surface를 기준으로 진행하면 된다.

## 11. bindings 담당자 체크리스트

- 제거된 core symbol reference 삭제
- service-level option API로 포팅
- routing id API로 identity surface 정리
- raw socket getter 노출 제거
- service poller add 경로 유지
- monitor standalone + poller integration 둘 다 유지
- service-level 테스트 통과
- representative perf 통과

## 12. 결론

bindings 포팅의 기준은 다음 한 문장으로 요약된다.

> 내부 socket을 아는 API를 걷어내고, `Gateway`, `Receiver`,
> `SpotPub`, `SpotSub`, `Discovery` 같은 service subject 기준 API로
> bindings surface를 다시 정리한다.

