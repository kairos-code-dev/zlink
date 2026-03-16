# `[03]` `core` 시스템 리팩토링 Phase 1 Resource Inventory

> 상태: draft
> 목적: ownership map을 실제 리소스 단위로 분해

| nav | link |
| --- | --- |
| 목록 | [README](README.ko.md) |
| 이전 | [02 Phase 1 Ownership Map](02-core-system-phase1-ownership-map.ko.md) |
| 다음 | [04 Phase 2 Socket Runtime Split](04-core-system-phase2-socket-runtime-split.ko.md) |
| 관련 | [01 Phase 0 Baseline](01-core-system-phase0-baseline.ko.md) |
| thread-safe 규약 | [thread-safe-socket-plan](../thread-safe/thread-safe-socket-plan.ko.md) — lifecycle strict 계층. 현재 구현 수준을 유지한다. |

## 1. 목적

이 문서는 Phase 1 ownership map을
실제 코드 리소스 단위로 풀어 쓴 inventory 문서다.

Phase 1 문서가 원칙 문서라면,
이 문서는 구현 착수 전에 보는 설계 입력 문서다.

핵심 질문은 하나다.

```text
"현재 코드에서 어떤 포인터/핸들/registry/task/thread/socket이
누구 소유인지, 누가 닫는지, 누가 기다리는지"
```

이 표가 없으면 ownership 정리는 추상론에 머문다.

## 2. 분석 기준

이번 inventory는 아래 구현을 기준으로 정리한다.

- [socket_base.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp)
- [own.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/own.hpp)
- [reaper.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/reaper.hpp)
- [service_runtime_base.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/common/service_runtime_base.hpp)
- [gateway.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/gateway/gateway.hpp)
- [gateway.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/gateway/gateway.cpp)
- [spot_node.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_node.hpp)
- [spot_node.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_node.cpp)
- [spot_runtime.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_runtime.hpp)
- [discovery.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/discovery/discovery.hpp)
- [discovery.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/discovery/discovery.cpp)
- [zlink.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/api/zlink.cpp)

## 3. 현재 구조에서 확인된 핵심 사실

코드 기준으로 먼저 고정해야 할 사실은 다음과 같다.

### 3.1 `socket_base_t`의 최종 파괴는 reaper 경유 + `own_t::process_destroy()`

현재 흐름:

- `socket_base_t::process_destroy()`는 `_destroyed = true`를 세팅
- `socket_base_t::check_destroy()`는 mailbox refcount와 poller post를 통해 finalize 시점 조율
- `socket_base_t::finalize_destroy()`는
  - `destroy_socket(this)`
  - `send_reaped()`
  - `own_t::process_destroy()`

즉 socket의 final free는 이미 socket runtime 내부 + reaper 협업 구조다.

### 3.2 `service_runtime_base_t`는 현재 coordinator이면서 closer다

현재 `service_runtime_base_t`는 다음을 함께 가진다.

- lifecycle state machine
- owned socket registry
- closing socket registry
- `close_socket`
- `close_socket_and_wait`
- `wait_drained`
- `force_wait_remaining`

즉 "무엇을 닫아야 하는지"와 "어떻게 기다릴지"가 같은 타입에 묶여 있다.

### 3.3 `gateway`와 `spot`은 lifecycle 구조가 다르다

`gateway`:

- `gateway_runtime_t` 안에 `service_runtime_base_t lifecycle`가 있다.
- 실제 close는 `runtime_->lifecycle.close_socket_and_wait(...)` 형태로 수행된다.

`spot`:

- `spot_node_t`가 직접 `service_runtime_base_t _lifecycle`를 가진다.
- `spot_runtime_t`는 attachment/control/data-plane socket 포인터를 별도 보관한다.
- 실제 close는 `owner->_lifecycle.close_socket_and_wait(...)`로 수행된다.

즉 같은 service 계열인데도 lifecycle owner의 위치가 다르다.

### 3.4 `spot`은 이미 "runtime + node + child handle" 3자 ownership 구조다

확인된 경로:

- `spot_runtime_t::destroy_attachment()`
- `spot_runtime_t::destroy_attachment_async()`
- `spot_pub_t::destroy_from_node()`
- `spot_sub_t::destroy_from_node()`
- `spot_node_t::destroy_handles()`
- `spot_node_t::close_control_sockets()`
- `spot_runtime_t::stop_and_join()`
- `spot_runtime_t::abortive_stop()`

즉 `spot`은 가장 먼저 정리해야 할 ownership hotspot
(소유권이 가장 복잡하게 얽힌 지점)이다.

### 3.5 `spot` 리소스 의존성 그래프

```text
spot_node_t (orchestration owner)
    |
    +---> _lifecycle (service_runtime_base_t)
    |         |
    |         +---> _owned_sockets ─── registry (추적만)
    |         +---> _closing_sockets ── drain 대기 (close owner 겸임 문제)
    |
    +---> _default_pub / _default_sub ─── child handle
    +---> _internal_receiver
    +---> _pubs / _subs registry
    |
    +---> _runtime (spot_runtime_t)
              |
              +---> data_ctrl_front / data_ctrl_back ─── thread handshake
              |
              +---> data plane internal sockets (6개)
              |     (mesh_pub, mesh_xsub, peer_ctrl_pub,
              |      peer_ctrl_sub, local_pub_ingress_sub,
              |      local_fanout_xpub)
              |
              +---> data_plane_thread ─── socket close와 join 순서 얽힘
              +---> task_id ─── control runtime 연동
              +---> attachments map ─── child/node/runtime 3자 close 가능

  문제: 점선 화살표가 없어도 3개 계층(node, runtime, child)이
        같은 리소스를 close할 수 있는 경로가 존재
```

## 4. 공통 inventory

## 4.1 socket / core lifecycle inventory

| 리소스 | 현재 저장 위치 | 생성 주체 | 현재 close 주체 | 현재 final destroy 주체 | 리스크 |
| --- | --- | --- | --- | --- | --- |
| socket object | `ctx` / service field / runtime field | `ctx->create_socket()` 호출자 | service runtime 또는 직접 socket | `socket_base_t::finalize_destroy()` | close owner와 final owner가 분리되어 이해 비용 큼 |
| mailbox refcount | `socket_base_t` 내부 | socket runtime | socket runtime | socket runtime | destroy 시점이 mailbox quiesce(보류 메시지 처리 완료 대기)에 의존 |
| owned child graph | `own_t::_owned` | owner object | owner object | `own_t::process_destroy()` | ownership graph가 service 설명에 잘 드러나지 않음 |
| reaper tracked sockets | `reaper_t` 내부 | ctx/reaper 경로 | reaper 흐름 | reaper + socket finalizer | policy owner처럼 오해되기 쉬움 |

### 4.1.1 목표 해석

- `socket_base_t::finalize_destroy()`는 socket-level cleanup owner로 유지
  (destroy_socket + send_reaped 수행)
- reaper는 최종 memory free executor로 제한
  (socket-level cleanup 이후 reaped 통지를 받아 free만 수행)
- service runtime은 concrete close 실행자가 아니라 orchestration coordinator로 축소

이 해석은 [02] ownership 표의 executor=reaper와 같은 체인을 가리킨다.
socket_base_t가 socket-level cleanup을 수행하고, reaper가 최종 memory free를 수행한다.

## 4.2 `service_runtime_base_t` inventory

| 리소스/책임 | 현재 의미 | 문제 |
| --- | --- | --- |
| `_state` | service lifecycle state | 적절하지만 socket close mechanics와 같은 타입에 있음 |
| `_owned_sockets` | service가 추적하는 socket registry | 추적과 실소유가 혼합됨 |
| `_closing_sockets` | close 후 drain 대기 registry | close owner 책임이 이 타입으로 집중됨 |
| `close_socket()` | stop + close + registry 이동 | coordinator가 closer 역할도 수행 |
| `close_socket_and_wait()` | ctx close wait 호출 | ctx contract와 service contract가 직접 결합 |
| `wait_drained()` | closing socket wait loop | sleep 기반 진행에 기대고 있음 |

### 4.2.1 목표 해석

`service_runtime_base_t`는 아래 둘 중 하나로 재정의하는 방향이 맞다.

- lifecycle coordinator + resource registry
- lifecycle coordinator + close request broker

반대로 아래는 이 타입에서 빼는 것이 맞다.

- concrete socket close mechanics
- sleep 기반 drain polling

## 5. `gateway` inventory

## 5.1 주요 리소스

`gateway_runtime_t` 기준 주요 리소스:

- `lifecycle`
- `monitor_socket`
- `router_socket`
- `refresh_task_id`
- `pools`
- `manual_routes`
- `ready_endpoints`
- `inflight_endpoints`
- `down_endpoints`
- `ready_peer_reports`

## 5.2 리소스 표

| 리소스 | 현재 저장 위치 | 생성 주체 | 현재 close 주체 | **목표 close 주체** | 비고 |
| --- | --- | --- | --- | --- | --- |
| `router_socket` | `gateway_runtime_t` | `gateway_t::ensure_router_socket()` 계열 | `runtime_->lifecycle.close_socket_and_wait()` | **socket runtime** | gateway hot path 핵심 |
| `monitor_socket` | `gateway_runtime_t` | monitor open/init 경로 | `runtime_->lifecycle.close_socket_and_wait()` | **socket runtime** | observer/child 경계와 연결 |
| refresh task | `gateway_runtime_t::refresh_task_id` | control runtime | `service_control_runtime_t::remove_task()` 계열 | service runtime (coordinator) | socket close보다 먼저 끊겨야 함 |
| pools/manual routes | `gateway_runtime_t` | gateway control path | 메모리 정리 | service runtime (coordinator) | socket close와 분리되어야 함 |
| discovery observer link | `gateway_t::_discovery` | attach path | observer detach | service runtime (coordinator) | ownership 방향성 명시 필요 |

> 현재 상태는 과도기다. 목표 상태가 구조 계약이다.

## 5.3 현재 구조 해석

`gateway`는 비교적 정리된 편이지만,
여전히 `service_runtime_base_t lifecycle`이 concrete close를 수행한다.

따라서 Phase 1에서 `gateway`의 핵심은 다음이다.

- `lifecycle`은 close owner가 아니라 close coordinator로 낮춘다.
- `router_socket`은 별도 socket runtime owner를 갖게 정리한다.
- monitor child/observer 경계는 parent detach와 child close를 분리한다.

## 6. `spot` inventory

## 6.1 주요 리소스

`spot_runtime_t` 기준:

- `data_ctrl_front`
- `data_ctrl_back`
- `mesh_pub`
- `mesh_xsub`
- `peer_ctrl_pub`
- `peer_ctrl_sub`
- `local_pub_ingress_sub`
- `local_fanout_xpub`
- `data_plane_thread`
- `task_id`
- `attachments`
- `connected_peer_endpoints`

`spot_node_t` 기준:

- `_runtime`
- `_default_pub`
- `_default_sub`
- `_internal_receiver`
- `_pubs`
- `_subs`
- `_lifecycle`
- discovery registration state

## 6.2 리소스 표

| 리소스 | 현재 저장 위치 | 생성 주체 | 현재 close 주체 | **목표 close 주체** | 리스크 |
| --- | --- | --- | --- | --- | --- |
| attachment socket | `spot_runtime_t::attachments` | `spot_runtime_t::create_attachment()` | `owner->_lifecycle.close_socket_and_wait()` | **socket runtime** | child/node/runtime 경계가 섞이기 쉬움 |
| `data_ctrl_front` | `spot_runtime_t` | `spot_runtime_t::start()` | `owner->_lifecycle.close_socket_and_wait()` | **socket runtime** | data plane thread와 handshake됨 |
| `data_ctrl_back` | `spot_runtime_t` | data plane thread 쪽 | `owner->_lifecycle.close_socket_and_wait()` | **socket runtime** | thread join 순서와 결합 |
| data plane internal sockets (6개: `mesh_pub`, `mesh_xsub`, `peer_ctrl_pub`, `peer_ctrl_sub`, `local_pub_ingress_sub`, `local_fanout_xpub`) | `spot_runtime_t` | data plane init | `owner->_lifecycle.close_socket_and_wait()` | **socket runtime** | 동일 경로로 닫히는 그룹. data/control fast path 핵심 |
| `data_plane_thread` | `spot_runtime_t` | `start()` | thread join | service runtime (coordinator) | socket close와 join 순서가 얽힘 |
| control task | `spot_runtime_t::task_id` | control runtime | control runtime remove | service runtime (coordinator) | socket stop보다 먼저 내려야 함 |
| `_default_pub` / `_default_sub` | `spot_node_t` | node helper | child handle destroy + node cleanup | service runtime (coordinator) | hidden owner 구조와 닿음 |
| `_internal_receiver` | `spot_node_t` | node init | internal receiver destroy | service runtime (coordinator) | public child와 섞이면 안 됨 |
| `_pubs` / `_subs` registry | `spot_node_t` | pub/sub create | registry detach + child destroy orchestration | service runtime (coordinator) | registry와 actual owner 분리 필요 |

> 현재 상태는 과도기다. 목표 상태가 구조 계약이다.

## 6.3 현재 구조 해석

`spot`은 다음 세 가지가 동시에 존재한다.

- node-level lifecycle
- runtime-level socket topology
- child handle lifecycle

따라서 `spot`의 핵심 정리 대상은 아래다.

### 6.3.1 attachment owner 분리

현재 attachment는 `spot_runtime_t` map에 저장되지만,
close 실행은 `owner->_lifecycle`를 통해 이뤄진다.

즉 저장 owner와 close owner가 다르다.

### 6.3.2 control/data plane socket owner 분리

현재 `spot_runtime_t`가 control/data plane socket 포인터를 보관하지만,
실제 close는 `_lifecycle`로 위임한다.

즉 topology owner와 close owner가 다르다.

### 6.3.3 child handle와 node destroy orchestration 정리

`destroy_from_node()` 계열은 필요하지만,
이 경로가 "누가 최종 close owner인가"를 흐리면 안 된다.

목표는 다음이다.

- node destroy는 orchestration owner
- child handle은 자기 public 자원 owner
- runtime은 internal topology owner
- socket runtime은 concrete close owner

## 7. `discovery` inventory

### 7.1 주요 리소스

`discovery_t` 기준:

- `_lifecycle` (service_runtime_base_t)
- `_sub_socket` (SUB socket — registry pub 수신)
- `_bootstrap_states` (endpoint별 DEALER socket map)
- `_report_dealers` (uplink endpoint별 DEALER socket map)
- `_control_dealers` (uplink endpoint별 DEALER socket map)
- `_task_id` (control runtime background task)
- `_monitor` (service_monitor_hub_t)
- `_observers` (observer 포인터 set — 비소유)
- `_services` / `_registered_services` (service state map)
- `_summary_store` / `_gateway_peer_summary_store` (topology summary)
- `_public_api` (API entrance guard)

### 7.2 리소스 표

| 리소스 | 현재 저장 위치 | 생성 주체 | 현재 close 주체 | **목표 close 주체** | 비고 |
| --- | --- | --- | --- | --- | --- |
| sub socket (ZLINK_SUB) | `discovery_t::_sub_socket` | `ensure_sub_socket()` | `destroy()` → `_lifecycle.close_socket()` | **socket runtime** | registry pub endpoint에 connect/disconnect 관리 |
| bootstrap dealers | `discovery_t::_bootstrap_states[].dealer` | `ensure_bootstrap_dealer_locked()` | `destroy()` → `_lifecycle.close_socket_and_wait()` | **socket runtime** | endpoint별 1개, bootstrap 후 폐기 |
| report dealers | `discovery_t::_report_dealers` | `ensure_report_dealer_locked()` | `destroy()` → `_lifecycle.close_socket_and_wait()` | **socket runtime** | uplink endpoint별 1개, heartbeat/topology report 전송 |
| control dealers | `discovery_t::_control_dealers` | `ensure_control_dealer_locked()` | `destroy()` → `_lifecycle.close_socket_and_wait()` | **socket runtime** | uplink endpoint별 1개, control message 전송 |
| control task | `discovery_t::_task_id` | `service_control_runtime->register_task()` | `destroy()` → `service_control_runtime->remove_task()` | service runtime (coordinator) | socket close보다 먼저 제거 |
| monitor hub | `discovery_t::_monitor` | embedded member | `destroy()` → `_monitor.close_all()` | service runtime (coordinator) | CLOSED event emit 후 정리 |
| observers | `discovery_t::_observers` | 외부 등록 (`add_observer()`) | `destroy()` → set clear + 통지 | service runtime (coordinator) | 비소유 — 포인터 참조만, 삭제 안 함 |
| service/summary state | `discovery_t` 내부 map | discovery control path | `destroy()` → map clear | discovery 자체 | socket이 아닌 순수 데이터 |

> 현재 상태는 과도기다. 목표 상태가 구조 계약이다.

### 7.3 현재 구조 해석

`discovery`는 `gateway`/`spot`과 비교하면 구조가 단순한 편이지만,
다음 ownership 냄새가 있다.

- **socket 수가 가변** — bootstrap/report/control dealer가 endpoint 수에 비례해서
  동적으로 생성/삭제된다. 고정 socket set인 gateway/spot과 다르다.
- **destroy가 snapshot-then-close 패턴** — lock 안에서 socket collection을 snapshot하고,
  lock 밖에서 순회 close한다. 패턴 자체는 합리적이지만,
  `_lifecycle.close_socket_and_wait()`를 socket 수 × 1000ms timeout으로 호출하므로
  socket 수가 많으면 destroy 총 시간이 길어질 수 있다.
- **observer callback drain** — `_observer_callbacks_inflight` + condition variable로
  observer callback 실행 중 destroy를 방어한다.
  이 패턴은 gateway/spot에는 없는 discovery 고유 패턴이다.
- **observer/summary ordering 제약** — destroy 흐름에서 observer 통지는
  모든 socket close + `wait_drained()` 이후에 수행된다.
  반면 `_summary_store` / `_gateway_peer_summary_store` clear는
  socket close 이전(`_sync` lock 안 snapshot 단계)에 수행된다.
  즉 observer가 통지 시점에 summary를 조회하면 이미 비어 있다.
  **이 ordering은 이번 리팩토링에서 유지해야 하는 계약으로 고정한다.**
  즉 destroy observer callback은 "summary가 이미 비워진 뒤" 호출되는 것이
  현재/목표 동작이다.

#### 7.3.1 destroy 경로 흐름도

```text
destroy()
  │
  ├─ Phase 1: API guard
  │    public API lock 획득 (실패 시 busy 반환)
  │    _destroying = true, _stop = 1
  │
  ├─ Phase 2: monitor / task 정리
  │    _monitor.close_all() → CLOSED event emit
  │    service_control_runtime->remove_task(_task_id)
  │
  ├─ Phase 3: snapshot under _sync lock
  │    _sync lock 획득
  │    socket collection snapshot (sub, bootstrap, report, control)
  │    map clear + summary store clear
  │    _sync lock 해제
  │
  ├─ Phase 4: socket close 순회
  │    sub_socket → disconnect + close_socket_and_wait(1000)
  │    bootstrap dealers → 순회 disconnect + close_socket_and_wait(1000)
  │    report dealers → 순회 disconnect + close_socket_and_wait(1000)
  │    control dealers → 순회 disconnect + close_socket_and_wait(1000)
  │
  └─ Phase 5: drain + observer 통지
       wait_drained(10000)
       observer set 순회 통지
       observer set clear
```

주의 사항:

- Phase 4의 총 시간은 `(1 + bootstrap 수 + report 수 + control 수) × 1000ms`에 비례한다.
  endpoint가 N개면 최악 `(1 + N + N + N) × 1000 = (3N+1)초`다.
- Phase 3에서 map을 clear하므로 Phase 4는 snapshot된 로컬 벡터를 순회한다.
  이 분리 덕분에 close 중 새 dealer 생성 시도는 `_destroying` guard에 의해 차단된다.

### 7.4 동적 dealer lifecycle 패턴

discovery의 dealer socket은 gateway/spot의 고정 socket set과 달리
endpoint 이벤트에 의해 동적으로 생성/삭제된다.

#### bootstrap dealer (ephemeral)

- **생성**: `ensure_bootstrap_dealer_locked()` — registry endpoint에 최초 연결 시
- **용도**: bootstrap 요청 전송 (1회성 handshake)
- **삭제**: bootstrap 성공 후 `_bootstrap_states` 에서 제거 + close
- **특성**: 수명이 짧고, endpoint당 최대 1개, bootstrap 완료 후 폐기

#### report / control dealer (persistent per endpoint)

- **생성**: `ensure_report_dealer_locked()` / `ensure_control_dealer_locked()`
  — uplink endpoint 연결 확인 후
- **용도**: heartbeat/topology report 전송 (report), control message 전송 (control)
- **삭제**: endpoint disconnect 시 또는 destroy 시
- **특성**: endpoint 연결 기간 동안 유지, endpoint당 각 1개

#### ownership 관점 해석

```text
dealer 유형          수명            생성 trigger         삭제 trigger
──────────────────  ──────────────  ──────────────────  ──────────────────
bootstrap dealer    ephemeral       registry connect    bootstrap 완료
report dealer       persistent      uplink connect      uplink disconnect / destroy
control dealer      persistent      uplink connect      uplink disconnect / destroy
```

이 패턴에서 ownership 냄새는 다음이다:

- 삭제 trigger가 2가지(정상 disconnect vs destroy)로 갈린다.
  정상 경로는 개별 close, destroy 경로는 snapshot 후 일괄 close다.
- Phase 2 socket runtime의 endpoint registry 하위 계약이 도입되면,
  동적 dealer의 생성/삭제를 endpoint 이벤트 hook으로 통합할 수 있다.

### 7.5 목표 해석

- discovery의 socket close도 gateway/spot과 같은 ownership 원칙을 따라야 한다:
  `_lifecycle`은 coordinator로 남기고, concrete close는 socket runtime이 수행.
- 동적 dealer socket의 lifecycle은 endpoint connect/disconnect 이벤트에 연결되므로,
  Phase 2 socket runtime의 endpoint registry 하위 계약과 자연스럽게 맞닿는다.
- observer 비소유 참조는 현재 패턴을 유지하되, destroy 시 통지 순서를
  socket close 완료 이후로 고정한다 (현재 코드와 동일).
- `_summary_store` / `_gateway_peer_summary_store` clear는 observer 통지보다
  먼저 수행하는 현재 ordering을 유지한다.
  즉 observer는 destroy 통지 시점에 summary를 읽을 수 없다고 가정한다.

## 8. API layer inventory

`zlink.cpp` 기준 destroy 진입점:

- `zlink_gateway_destroy`
- `zlink_spot_node_destroy`
- `zlink_spot_destroy`
- `zlink_discovery_destroy`
- `zlink_registry_destroy`
- `zlink_registry_query_destroy`

현재 해석:

- API layer가 destroy orchestration entry 역할을 수행
- 다만 일부 경로는 child/node destroy fallback을 직접 분기

목표 해석:

- API layer는 destroy entry로 유지
- fallback 분기와 close 세부는 service/runtime 내부 contract로 내림

## 9. 리팩토링 우선순위

실제 구현 순서는 아래가 맞다.

1. `service_runtime_base_t` 역할 축소 설계
2. `spot` attachment/control/data-plane ownership 분해
3. `gateway` router/monitor/task ownership 정리
4. `discovery` 동적 dealer/sub socket ownership 정리
5. API destroy entry 단순화
6. 공통 socket runtime contract 도입

## 10. 구현 시 바로 확인할 질문

각 리소스마다 아래 질문을 채운다.

1. 저장 owner는 누구인가?
2. 상태 전이 owner는 누구인가?
3. concrete close owner는 누구인가?
4. drain 확인 owner는 누구인가?
5. final free owner는 누구인가?

현재 코드에서 1과 3이 다르면,
그 차이가 intentional contract인지 accidental coupling인지 먼저 판정해야 한다.

## 11. Phase 1 inventory 완료 조건

아래 상태가 되면 inventory 단계는 완료다.

- `gateway`, `spot`, `discovery`, 공통 lifecycle에 대해 리소스 표가 채워져 있다.
- 각 리소스의 저장 owner와 close owner가 구분되어 있다.
- 핵심 리소스 표에 current/target owner 차이가 명시되어 있다.
- 다중 owner가 있는 경우 의도/부채 여부가 표시되어 있다.
- 이후 코드 리팩토링 PR이 이 표를 근거로 설명 가능하다.

## 12. 구현 PR에 바로 쓰는 요약

이 inventory 기준으로 PR 설명은 아래 형식을 권장한다.

- 변경 리소스:
- 기존 저장 owner:
- 기존 close owner:
- 변경 후 close owner:
- drain 확인 owner:
- perf 영향 관찰 포인트:
