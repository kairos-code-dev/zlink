# Service Topology Reporting Runtime Plan

## 1. 문서 목적

이 문서는 `registry topology summary`를 실제 runtime에서 안정적으로
보고(report)하기 위한 `core` 개선안을 정리한다.

기존 문서:

- [service-routing-id-policy-plan.ko.md](/home/hep7/project/kairos/zlink/doc/plan/service-routing-id-policy-plan.ko.md)
- [service-monitor-readiness-plan.ko.md](/home/hep7/project/kairos/zlink/doc/plan/service-monitor-readiness-plan.ko.md)
- [registry-topology-introspection-plan.ko.md](/home/hep7/project/kairos/zlink/doc/plan/registry-topology-introspection-plan.ko.md)

은 각각 다음을 정의했다.

- representative `routing_id`
- local monitor
- registry topology snapshot/query

하지만 실제 구현 단계에서 다음 문제가 드러났다.

- 기존에는 `Receiver` 쪽 dealer/heartbeat 경로가 registry liveness에 관여했지만,
  이 경로가 topology summary, provider presence, global visibility를
  뒤섞는 원인이 되었다.
- 반면 `Gateway`, `Discovery`, `SpotPub`, `SpotSub`는
  topology summary를 어떤 socket/lifetime/thread에서 report해야 하는지가
  service contract로 명확하지 않았다.
- 그 결과 service별로 임시 `DEALER`를 만들거나 기존 control socket을
  재활용하는 방식이 섞였고,
- 이로 인해 delivery loss, lifetime 꼬임, lock 순서 충돌, 테스트 timeout이
  발생하기 쉬운 구조가 되었다.

이 문서는 그 문제를 해결하기 위한
`discovery-owned topology reporting runtime` 계층을 제안한다.

한 줄 요약:

```text
monitor = local state transition
registry topology = global summary
discovery = registry uplink / heartbeat / topology report owner
service = local summary producer
```

---

## 2. 문제 정의

### 2.1 현재 드러난 문제

현재 registry topology는 `snapshot/query` surface는 존재하지만,
reporting path owner가 일관되지 않다.

대표 문제:

- `Discovery`
  - 기존 API는 discovery/broadcast 연결만 드러냈고,
    topology report와 heartbeat uplink에 필요한 내부 endpoint 정보를
    어떻게 얻을지가 명확하지 않았다.
- `Gateway`
  - discovery 기반 consumer라서 원래 registry에 직접 붙지 않는다.
  - topology summary를 보내려면 별도 reporting client가 필요해 보였지만,
    실제로는 `Discovery`가 uplink owner가 되는 편이 더 자연스럽다.
- `SpotPub` / `SpotSub`
  - `SpotNode`의 control path가 있긴 하지만,
    public subject는 `SpotPub` / `SpotSub`다.
  - `SpotPub` / `SpotSub` 각각에 reporter를 둘지,
    `SpotNode`에 둘지 애매했고,
  - 그보다 `Discovery`가 summary uplink owner가 되는 편이
    역할 분리가 더 명확하다.

### 2.2 현재 구조가 실수하기 쉬운 이유

현재 구조에서는 implementer가 service별로 직접 다음을 결정해야 한다.

- 어떤 socket으로 report를 보낼지
- report sender를 누가 소유할지
- connect 완료를 어떻게 기다릴지
- service destroy 시 어떻게 정리할지
- monitor event와 topology state를 어디서 연결할지
- report 대상 RID는 무엇인지
- service 이름/endpoint/source/state를 어디서 꺼낼지

즉, registry topology feature가 존재해도
그 feature를 안정적으로 사용하는 공용 primitive가 없다.

이 상태에서는 같은 종류의 버그가 반복된다.

- ephemeral socket send 후 즉시 close로 인한 report loss
- control/runtime lock 안에서 report를 호출해 deadlock 또는 timeout
- service마다 다른 state mapping
- service마다 다른 lifetime model
- report path와 public monitor path가 서로 어긋남

### 2.3 단순 구현 실수 이상의 문제

이 문제는 단순 bug fix만으로 닫기 어렵다.

핵심은 registry visibility/liveness를 담당하는 owner가
하나로 고정되지 않았다는 점이다.

즉 필요한 건:

- registry uplink owner를 한 곳으로 고정
- service는 local summary producer만 하게 정리
- `Discovery`가 summary를 수집해 registry로 올리는 구조

---

## 3. 목표

### 3.1 기능 목표

- `Gateway`, `Discovery`, `SpotPub`, `SpotSub`, `Receiver`가 모두
  registry topology summary를 안정적으로 report할 수 있어야 한다.
- registry로 향하는 실제 heartbeat/report sender lifetime은
  `Discovery`가 소유해야 한다.
- registry liveness 판단에 쓰이는 heartbeat도 `Discovery` 하나로 단일화해야 한다.
- 각 service는 service별 임시 reporter가 아니라
  local summary producer로만 동작해야 한다.
- local monitor event와 registry summary state의 대응이 service별로 일관되어야 한다.
- setup, steady-state, destroy 모두에서 동일한 lifetime contract를 가져야 한다.

### 3.2 API/설계 목표

- public service API는 단순해야 한다.
- topology reporting을 위해 사용자가 raw socket이나 내부 endpoint role을
  직접 다룰 필요가 없어야 한다.
- `Discovery`의 registry 연결은 하나의 public API로 끝나야 한다.
- topology/heartbeat uplink에 필요한 내부 endpoint 정보는 bootstrap 후
  자동으로 학습되어야 한다.
- topology reporting을 위해 별도의 public `RuntimeSet` 타입을 추가하지 않는다.
- `SpotNode`는 wiring owner로 남고,
  topology subject는 `SpotPub` / `SpotSub`로 유지해야 한다.
- `Discovery`를 쓰지 않는 manual 구성은 registry topology visibility가
  없는 것이 정상이어야 한다.
- `Receiver`는 더 이상 registry heartbeat owner가 아니어야 한다.

### 3.3 비목표

이번 범위에서 하지 않는 것:

- registry를 실시간 telemetry bus로 만드는 것
- queue depth, `POLLIN/POLLOUT` 같은 순간 상태를 registry에 싣는 것
- raw socket peer detail을 registry summary에 직접 노출하는 것
- bindings redesign

---

## 4. 핵심 결론

### 4.1 필요한 건 새 public API 몇 개가 아니라 공용 runtime 계층이다

이번 문제의 본질은 “함수 하나가 없다”가 아니다.

없는 것은:

- single registry uplink/heartbeat owner
- stable sender socket ownership
- stable local summary -> registry summary mapping
- stable destroy semantics

그래서 해결책은 다음 두 층을 추가하는 것이다.

1. `Discovery-owned topology uplink runtime`
2. `service -> local summary builder / discovery submit path`

즉:

```text
service local state
-> service-specific summary builder
-> Discovery local summary store
-> Discovery-owned topology uplink runtime
-> registry TOPOLOGY_REPORT
```

### 4.2 public API는 최소 변경만 한다

public API는 가능한 한 기존 surface를 유지한다.

권장:

- `zlink_discovery_connect_registry(...)`를 canonical registry bootstrap entry로 유지
- 다른 service-level connect helper가 남더라도 내부적으로는
  `Discovery` runtime에 위임하는 thin wrapper여야 한다
- topology/heartbeat uplink용 내부 endpoint 정보는 bootstrap metadata로 자동 획득

핵심은 public API보다 internal runtime/helper와 ownership 계약이다.

---

## 5. 제안 구조

### 5.1 Discovery-owned topology uplink runtime

새 internal component:

```text
services/discovery/discovery_topology_uplink.hpp
services/discovery/discovery_topology_uplink.cpp
```

역할:

- registry uplink endpoint 집합 유지
- persistent `DEALER` sender lifetime 관리
- discovery representative RID 적용
- connect once / reuse many
- batched `TOPOLOGY_REPORT` frame 송신
- registry heartbeat / topology flush / destroy 시 orderly close

#### 내부 API 초안

```cpp
class discovery_topology_uplink_t
{
  public:
    explicit discovery_topology_uplink_t(ctx_t *ctx);
    ~discovery_topology_uplink_t();

    int set_routing_id(const zlink_routing_id_t *rid);
    int add_registry_endpoint(const char *registry_uplink_endpoint);
    int remove_registry_endpoint(const char *registry_uplink_endpoint);

    int ensure_connected();

    int upsert(const zlink_registry_topology_entry_t &entry);
    int erase(uint16_t service_kind,
              const zlink_routing_id_t *rid,
              const char *service_name);

    int flush();
    int close();
};
```

핵심 계약:

- sender socket은 `Discovery` lifetime 동안 재사용
- report마다 ephemeral socket 생성 금지
- `Gateway`, `Receiver`, `SpotNode`, `SpotPub`, `SpotSub`는
  registry로 직접 send하지 않음
- local summary는 `Discovery`가 수집하고, safe tick에서 flush
- destroy 시 sender close는 `Discovery` runtime helper가 책임

### 5.2 service-specific summary builder

공용 runtime만으로는 충분하지 않다.
service마다 summary 의미가 다르기 때문이다.

예:

- `Gateway`
  - `desired_count`
  - `ready_count`
  - `service_name`
  - consumer endpoint/context
- `Discovery`
  - watched service name
  - provider count
  - `READY / DISCOVERED / LOST`
- `SpotPub` / `SpotSub`
  - SPOT 문서 section 12.3의 state mapping 참조
  - proxy 재작성 후 기존 queue/filter event 기반 trigger는 사용하지 않는다

따라서 각 service는 다음 helper 하나만 구현한다.

```cpp
int build_topology_entry(..., zlink_registry_topology_entry_t *out);
```

그 다음 summary는 `Discovery`에 제출한다.

```cpp
int discovery_t::upsert_service_summary(
  const zlink_registry_topology_entry_t *entry);

int discovery_t::erase_service_summary(
  uint16_t service_kind,
  const zlink_routing_id_t *rid,
  const char *service_name);
```

이 submit API는 internal-only 계약이다.

- public C API로 노출하지 않는다
- bindings surface로 노출하지 않는다
- 호출 주체는 `gateway.cpp`, `receiver.cpp`, `spot_node.cpp`, `discovery.cpp`
  같은 core service 구현 코드뿐이다
- 사용자가 `report_state()` 같은 함수를 직접 호출하는 모델로 가지 않는다

정리하면:

- `Gateway`는 자기 local summary를 `Discovery`에 제출
- `Receiver`는 provider control request와 topology summary를 모두
  `Discovery` runtime에 위임한다
- `SpotPub`, `SpotSub`는 직접 `Discovery`를 알지 않고
  `SpotNode`가 local summary를 모아 `Discovery`에 제출
- `Discovery` 자신에 대한 summary도 같은 local store에 넣고 uplink

중요:

- 기존 provider registration과
  topology summary uplink는 같은 것이 아니다.
- topology summary uplink는 관찰/진단용 global summary와 registry visibility를 담당한다.
- registry liveness/freshness 판단에 쓰이는 heartbeat는 `Discovery`가 소유한다.
- `Receiver`는 provider/router 역할만 하고, registry heartbeat owner는 아니다.

특히 `Receiver`는 아래 둘을 분리해야 한다.

- provider registration
  - provider discovery contract의 일부
  - `REGISTER`, `UNREGISTER` 흐름 유지
- topology summary submit
  - global summary/diagnostics 목적
  - `READY`, `STOPPED`, `ERROR`, `ready_count` 같은 관찰 상태 반영
  - `Discovery` summary store에 제출

즉 `Receiver` 구현에서 provider registration helper와
topology summary helper는 내부 함수/파일 레벨에서도 분리하는 것이 권장된다.

### 5.3 monitor와 topology report는 직접 결합하지 않는다

monitor event를 emit하는 함수 안에서 곧바로 registry send를 보내는 구조는
위험하다.

이유:

- monitor emit 시점은 lock 안일 수 있다
- callback-like 의미와 reporting I/O가 섞인다
- deadlock, reentrancy, timeout 위험이 커진다

권장 구조:

```text
local state update
-> summary state 결정
-> Discovery summary store update
-> monitor emit
```

또는

```text
local state update
-> monitor emit
-> safe call site에서 Discovery flush
```

하지만 금지할 것:

```text
emit_event() 안에서 바로 registry network send
```

### 5.4 report trigger는 명시적으로 서비스 코드에 둔다

핵심 send/recv loop와 마찬가지로,
summary update trigger도 service 파일 안에서 명시적으로 보여야 한다.

즉,

- `gateway.cpp`
- `receiver.cpp`
- `discovery.cpp`
- `spot_node.cpp`

안에서 어느 state transition이 `Discovery summary update`를 트리거하는지
보여야 한다.

`Discovery` uplink helper는 transport/lifetime/flush만 맡는다.

---

## 6. 필요한 core API 개선

### 6.1 Discovery

현재 부족한 것:

- `zlink_discovery_connect_registry(...)`가 discovery/broadcast 연결만 의미하는
  API처럼 읽힌다
- topology/heartbeat uplink에 필요한 내부 endpoint 정보를 bootstrap하는 계약이
  없다

권장:

```c
int zlink_discovery_connect_registry(void *discovery,
                                     const char *registry_endpoint);
```

의미:

- `Discovery`를 하나의 registry bootstrap control endpoint에 연결한다
- discovery/broadcast 수신과 topology/heartbeat uplink에 필요한
  내부 endpoint 정보는 bootstrap metadata로 자동 획득한다
- 사용자는 `PUB endpoint`, `ROUTER endpoint` 같은 내부 socket role을
  알 필요가 없다

정책:

- setup 단계에서 호출
- 1차에서도 여러 registry endpoint를 허용한다
- public API에서 endpoint role을 분리하지 않는다
- destroy 전까지 유지

granularity:

- `Discovery` summary entry는 watched `service_name`별 1개를 기본으로 한다.
- 즉 하나의 `Discovery`가 여러 service를 watch하면
  registry topology에도 여러 `Discovery` entry가 생긴다.
- 별도의 “Discovery process self entry”는 1차 범위에 넣지 않는다.

### 6.2 Gateway

새 public API는 꼭 필요하지 않다.

이유:

- `Gateway`는 `Discovery`를 주입받는다
- topology summary uplink는 `Discovery`가 담당한다
- `Gateway`는 local summary만 `Discovery`에 제출하면 된다

필요한 internal contract:

```cpp
int gateway_t::update_discovery_summary(...);
```

필수 구현 항목:

- `Gateway`가 `Discovery*`를 보유하는지 항상 확인
- `Discovery`가 없으면 summary submit을 건너뛰되,
  internal state는 그대로 유지
- `ready_count`, `desired_count`, `last_error`, `service_name`, `endpoint`
  기준으로 entry build
- `SERVICE_READY`, `SERVICE_LOST`, `COUNT_CHANGED`, fatal error 경로에서
  summary dirty update

### 6.3 Spot

SPOT의 topology reporting 세부 계약은
[`spot-proxy-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-proxy-rewrite-spec.ko.md)
section 12를 따른다.

원칙만 여기 남긴다:

- `SpotNode`는 wiring owner / discovery bridge다
- `SpotPub` / `SpotSub`는 topology summary subject다
- SPOT summary는 discovery-owned uplink runtime을 사용한다
- `SpotPub` / `SpotSub`는 `Discovery`를 직접 참조하지 않는다
- `SpotNode`는 registry 전용 소켓을 두지 않는다
- registry heartbeat는 `Discovery`가 전담한다

state mapping, ownership, submit API, destroy semantics의 상세 규범은
SPOT 문서를 참조한다.

### 6.4 Receiver

`Receiver`는 provider registration/control path와 topology summary reporting을
모두 discovery-owned runtime에 위임한다.

개선 포인트:

- `Receiver`는 registry raw sender를 소유하지 않음
- register/unregister/update_weight request도 `Discovery` runtime으로 보냄
- topology summary도 `Discovery` summary store/uplink를 사용함
- 별도 ad-hoc sender를 만들지 않음

필수 구현 항목:

- `register ok/failed`, `unregister ok`, router peer 변화, fatal error에서
  receiver-local state를 summary로 갱신
- `Receiver` heartbeat sender/timer를 제거
- `register failed`는 topology summary에서 `ERROR`로 명시적으로 매핑
- registry freshness는 `Discovery` heartbeat만 기준으로 삼는다

`Discovery`가 없는 경우 계약:

- summary submit은 no-op이다
- service local state와 monitor state는 그대로 유지한다
- topology visibility가 없다는 이유만으로 fatal error를 만들지 않는다
- perf/setup 코드도 이 경우 registry summary를 readiness source로 기대하지 않는다

---

## 7. state mapping 원칙

### 7.1 공통 원칙

registry summary state는 “local service가 결정한 결과”다.

즉:

- registry가 state를 계산하지 않는다
- local service가 monitor-like internal event를 보고 state를 결정한다
- local service 또는 owner bridge가 `Discovery` summary store를 갱신한다
- `Discovery`가 그 state를 registry로 전송한다
- registry는 받은 summary를 저장/쿼리만 한다

### 7.2 service별 기본 mapping

| Service | Local trigger | Registry state |
|---|---|---|
| `Discovery` | service up | `READY` |
| `Discovery` | providers changed with count>0 | `READY` |
| `Discovery` | providers empty / service down | `LOST` |
| `Gateway` | route/service ready | `READY` |
| `Gateway` | ready count 0 after loss | `LOST` |
| `Gateway` | discovered but not ready | `CONNECTING` |
| `Receiver` | register ok | `READY` |
| `Receiver` | register failed | `ERROR` |
| `Receiver` | unregister ok | `STOPPED` |
| `Receiver` | fatal error | `ERROR` |
| `SpotPub` | SPOT 문서 section 12.3 참조 | SPOT 문서 참조 |
| `SpotSub` | SPOT 문서 section 12.3 참조 | SPOT 문서 참조 |

### 7.3 Spot state mapping은 SPOT 문서가 기준

SPOT의 topology state mapping은 proxy 재작성에 맞춰 재정의됐다.

- 기존 queue/filter monitor event 기반 trigger는 더 이상 사용하지 않는다
- 상세 state mapping은
  [`spot-proxy-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-proxy-rewrite-spec.ko.md)
  section 12.3을 따른다
- strong subscription delivery 보장은 후속 phase로 남긴다

---

## 8. lifetime / threading 정책

### 8.1 sender socket lifetime

금지:

- report마다 `DEALER` 새로 생성
- connect 직후 send 후 즉시 close

권장:

- `Discovery`당 reporter socket 1개
- endpoint set connect를 누적
- `DEALER` sender는 `Discovery` destroy 때 정리
- summary store는 change-only dirty mark를 유지하고,
  flush는 discovery heartbeat tick 또는 state-change wakeup에서 수행

flush 실패 정책:

- 기본 정책은 best-effort다.
- `flush()`는 store에 있는 dirty entry 전체에 대해 send를 시도한다.
- 일부 endpoint 전송 실패가 있어도 process/service를 fail시키지 않는다.
- 실패한 flush cycle 이후 dirty entry는 유지하고 다음 tick에서 재시도한다.
- 같은 값이 반복 제출돼도 store 기준으로 change-only dirty mark를 써서
  불필요한 재전송을 줄인다.
- `EAGAIN`, endpoint disconnect, registry down은 fatal이 아니라
  retryable flush failure로 취급한다.
- serialization 오류, invalid entry build, contract 위반은 local fatal error다.

flush 주기/우선순위 기본값:

- 기본 flush cadence는 discovery heartbeat interval과 동일하게 둔다.
- state-change가 발생하면 즉시 network send를 하지 않고
  flush wakeup만 건다.
- wakeup 이후 다음 control tick 또는 heartbeat tick에서 dirty store를 flush한다.
- 즉시 network send는 금지하고, 즉시 wakeup만 허용한다.
- 동일 tick 안에서 여러 update가 겹치면 하나의 flush cycle로 coalesce한다.

multi-endpoint fan-out 정책:

- configured registry uplink endpoint 각각에 대해 send를 시도한다.
- 성공 기준은 `all-or-nothing`이 아니라 `best-effort per endpoint`다.
- endpoint 일부만 성공해도 dirty는 유지하고 다음 tick에서 다시 보낼 수 있다.
- registry side는 동일 key의 repeated upsert를 idempotent하게 처리해야 한다.

multi-Discovery / multi-registry 주의:

- 같은 topology key가 여러 discovery 또는 여러 uplink 경로에서
  반복 upsert될 수 있다.
- registry는 key 기준 upsert를 idempotent하게 처리해야 한다.
- 최신 `last_reported_ms` 기준으로 최종 상태가 수렴하면 된다.

erase / tombstone 정책:

- `STOPPED`는 direct erase 대신 tombstone entry로 먼저 반영한다.
- tombstone은 짧은 grace TTL 동안 registry snapshot에 남긴다.
- grace TTL 이후 registry가 tombstone entry를 제거한다.
- 일반적인 transient summary replacement는 direct upsert로 처리한다.
- `STOPPED`가 아닌 단순 summary disable/remove는 direct erase를 기본으로 한다.

권장 TTL 기본값:

- `stopped_tombstone_ttl_ms = 2 * heartbeat_interval_ms`
- `lost_timeout_ms = 2 * heartbeat_interval_ms`
- `stale_gc_timeout_ms = 4 * heartbeat_interval_ms`

### 8.2 thread ownership

summary submit과 uplink flush는 각각 안전한 thread/phase에서만 호출한다.

금지:

- lock 안에서 registry network send
- monitor emit 안에서 직접 report send
- user-facing callback 안에서 report send

권장:

- service는 state update 후 summary만 submit
- `Discovery` control tick 또는 heartbeat tick이 flush
- registry uplink flush는 `Discovery` 경로에서만 수행

추가 원칙:

- `Gateway`, `Receiver`는 자기 state thread/control path에서만 summary submit
- `SpotPub`, `SpotSub`는 직접 submit하지 않고 `SpotNode` bridge를 통해 submit
- `Discovery` summary store 접근은 내부 lock 또는 single-thread ownership으로 보호

### 8.3 destroy semantics

destroy 시:

1. local monitor에 `CLOSED` event emit
2. topology summary는 optional `STOPPED`를 `Discovery` store에 반영
3. `Discovery`가 필요 시 final flush
4. reporter socket close

주의:

- destroy 중 report를 동기적으로 강하게 보장하려고 하지 않는다
- 대신 steady-state update가 안정적인 것이 우선이다
- `Discovery`가 없으면 final topology visibility도 없을 수 있다

권장 정리 순서:

1. service local state를 `STOPPED` 또는 erase 대상으로 표시
2. local monitor `CLOSED` emit
3. `Discovery`가 있으면 summary store update
4. optional final flush
5. socket/resource close

destroy 안전성 계약:

- `Gateway`, `Receiver`, `SpotNode`가 보유한 `Discovery*`는 weak reference처럼 취급한다.
- `Discovery` destroy 이후 summary submit은 no-op이 되어야 한다.
- 사용자가 destroy 순서를 완벽히 맞추지 않아도 dangling pointer가 나지 않도록
  guard를 둔다.
- 엄격한 owner destroy ordering을 public 계약으로 강요하지 않는다.

---

## 9. public API 초안

이번 문서는 새 public 함수를 늘리기보다 기존 함수를 재의미화하는 쪽을 권장한다.

### 9.1 Discovery

```c
int zlink_discovery_connect_registry(void *discovery,
                                     const char *registry_endpoint);
```

의미:

- registry bootstrap connect
- 내부적으로 discovery/broadcast subscribe path와
  topology/heartbeat uplink path를 자동 구성

### 9.2 나머지 서비스

추가 public API는 필수가 아니다.

`Gateway`, `Receiver`, `SpotPub`, `SpotSub`, `SpotNode`는
기존 service constructor + routing id + monitor + option surface로 충분하다.

핵심은 internal contract다.

즉 이 문서의 중심은:

- public API 확장 최소화
- `Discovery`를 registry uplink owner로 정식화
- 각 service를 local summary producer로 정식화

---

## 10. 내부 구현 초안

### 10.1 공용 runtime 경계

```text
core/src/services/discovery/discovery_topology_uplink.hpp
core/src/services/discovery/discovery_topology_uplink.cpp
```

위 파일 분리는 권장 구조다. 다만 1차 구현에서는 같은 역할이
`discovery.cpp` / `discovery.hpp` 내부 helper와 field로 인라인되어 있어도 된다.

핵심 요구는 파일명 자체가 아니라 아래 runtime 경계다:

- topology report sender owner
- control request sender owner
- summary store / dirty flush owner
- heartbeat sender owner

### 10.2 Discovery

- `_registry_uplink_endpoints`
- `_summary_store`
- `_dirty_entries`
- `_topology_uplink` 또는 그에 준하는 internal runtime field 집합
- `connect_registry()`
- `upsert_service_summary()`
- `erase_service_summary()`
- service list / summary store 변경 시 `flush_topology()`

세부 구현:

- `connect_registry()`는 bootstrap connect만 public으로 받음
- bootstrap 후 registry metadata에서 internal uplink endpoint를 학습
- `_summary_store` key는 `service_kind + routing_id + service_name`
- `upsert_service_summary()`는 value compare 후 변경이 있을 때만 dirty mark
- `erase_service_summary()`는 `STOPPED`면 tombstone, 그 외 remove면 direct erase
- `flush_topology()`는 dirty snapshot을 잡고 lock 밖에서 uplink send
- heartbeat tick이 있으면 그 tick에서 flush, 없으면 wakeup-based flush 보조
- registry heartbeat 송신과 topology flush를 같은 uplink owner에서 수행

bootstrap 세부 구현 권장:

1. `zlink_discovery_connect_registry(discovery, registry_endpoint)`
2. `Discovery`가 registry control plane에 bootstrap request 전송
3. registry가 bootstrap metadata reply 반환
4. `Discovery`가 reply를 바탕으로 내부 channel 자동 구성

권장 bootstrap metadata:

- `registry_pub_endpoint`
- `registry_uplink_endpoint`
- `heartbeat_interval_ms`
- `registry_id`
- `feature_flags`

권장 내부 protocol 흐름:

```text
DISCOVERY_BOOTSTRAP_REQ
-> service_type
-> discovery_routing_id

DISCOVERY_BOOTSTRAP_REP
-> registry_pub_endpoint
-> registry_uplink_endpoint
-> heartbeat_interval_ms
-> registry_id
-> feature_flags
```

권장 구현 원칙:

- public API는 bootstrap endpoint 하나만 받는다
- `PUB`, `ROUTER`, `uplink` 같은 internal role endpoint는 metadata에서 학습한다
- bootstrap 성공 전까지 summary flush는 보류한다
- bootstrap 실패는 connect failure로 취급한다

Discovery 내부 상태 권장 필드:

- `_bootstrap_endpoints`
- `_registry_pub_endpoints`
- `_registry_uplink_endpoints`
- `_summary_store`
- `_dirty_entries`
- `_heartbeat_interval_ms`
- `_topology_uplink`
- `_bootstrap_ready`

summary store 동작:

1. service가 `upsert_service_summary()` 또는 `erase_service_summary()` 호출
2. `Discovery`가 key 기준 latest value를 store에 반영
3. 값이 실제로 바뀌었으면 dirty mark
4. heartbeat tick 또는 wakeup tick에서 `flush_topology()`
5. flush 성공한 entry는 dirty clear, 실패한 entry는 dirty 유지

### 10.3 Gateway

- `SERVICE_READY / LOST / COUNT_CHANGED` 시 summary build
- `Discovery`가 있으면 `upsert_service_summary()`

권장 submit 흐름:

```text
gateway local state transition
-> build gateway topology entry
-> discovery->upsert_service_summary(...)
-> return
```

금지:

- gateway state path에서 registry network send 직접 호출

### 10.4 Receiver

- `Receiver`는 registry raw sender를 두지 않는다
- register/unregister/update_weight는 discovery-owned control runtime으로 위임한다
- receiver topology summary도 discovery-owned summary runtime으로 위임한다
- receiver-local heartbeat sender/timer는 제거한다

세부 구현:

- provider registration helper와 topology summary helper를 `Discovery` runtime에 통합
- receiver-local heartbeat sender/timer는 제거한다
- `Receiver`는 local state를 보고 summary entry를 build해서
  `Discovery::upsert_service_summary()` / `erase_service_summary()`를 호출한다
- registry는 receiver control ack / topology report를 저장하고 query만 수행한다
- registry freshness는 Discovery heartbeat가 전담

권장 흐름:

```text
receiver local state transition
-> build receiver topology entry
-> discovery->upsert_service_summary(...)
-> return
```

즉:

- provider discovery contract는 기존 message type을 유지하되 sender owner는
  `Discovery`다
- receiver topology visibility도 동일한 discovery uplink runtime으로 반영한다
- discovery heartbeat는 receiver provider freshness와 summary uplink freshness를
  함께 담당한다

### 10.5 Spot

SPOT의 topology reporting 내부 구현은
[`spot-proxy-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-proxy-rewrite-spec.ko.md)
section 12에 정의되어 있다.

원칙만 여기 남긴다:

- `SpotNode`가 `Discovery` bridge 소유
- `SpotPub` / `SpotSub`가 summary subject
- `SpotNode`가 `submit_pub_summary()` / `submit_sub_summary()` internal helper 제공
- lock 안에서 `Discovery` submit 금지
- summary update trigger, state mapping, submit API, destroy semantics는
  SPOT 문서를 따른다

### 10.6 flush / wakeup 상세

권장 flush 사이클:

```text
service local state change
-> discovery summary dirty mark
-> discovery wakeup
-> next control/heartbeat tick
-> dirty snapshot capture
-> lock 밖에서 uplink send
-> per-entry success/fail 반영
```

세부 원칙:

- dirty snapshot은 lock 안에서 잡고, actual send는 lock 밖에서 한다
- success/fail 반영은 다시 lock 안으로 들어와 최소 범위로 처리한다
- partial success여도 process를 fail시키지 않는다
- registry down, uplink reconnect 중이어도 service local state는 계속 유지한다

---

## 11. 테스트 계획

### 11.1 unit / functional

- `Discovery`
  - `connect_registry()` 후 self summary와 submitted service summary가
    snapshot에 반영되는지
- `Gateway`
  - provider 등록 후 `READY` summary가 discovery를 통해 반영되는지
  - disconnect 후 `LOST` 반영되는지
- `Receiver`
  - register -> `READY`
  - unregister -> `STOPPED`
  - provider registration path와 topology summary path가 분리돼 있는지
  - receiver-local heartbeat 제거 후도 summary/state가 유지되는지
- `SpotPub` / `SpotSub`
  - SPOT 문서 section 12 기준의 topology summary lifecycle 테스트를 따른다
  - 상세 테스트 항목은
    [`spot-proxy-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-proxy-rewrite-spec.ko.md)
    section 14.3을 참조한다

### 11.2 lifetime

- uplink socket reuse
- destroy 후 monitor `CLOSED`
- destroy 중 deadlock 없음
- dirty summary가 남은 상태에서도 destroy 순서가 안전한지

### 11.3 consistency

- local monitor transition과 registry snapshot이 심하게 어긋나지 않는지
- `READY` 후 snapshot count >= 1
- `LOST` 후 snapshot state change
- repeated upsert / partial flush 후에도 key 기준 최종 상태가 수렴하는지

### 11.4 perf 영향

- service perf setup에서 topology readiness를 summary/query 또는 monitor로 대체 가능 여부 확인
- active hot loop에는 reporting code가 들어가지 않도록 유지

perf integration 기본 원칙:

- registry summary/query는 global/coarse readiness와 diagnostics 용도다
- registry summary는 eventually consistent view이므로 final strict start gate로 쓰지 않는다
- perf의 최종 readiness gate는 local service monitor를 사용한다
- bootstrap/query/summary 조회는 measurement phase 밖에서만 사용한다
- active hot loop 안에서는 registry query, monitor logging, topology reporting을 수행하지 않는다
- `Discovery` 없는 bench/manual 구성은 registry summary를 기대하지 않고
  local monitor 또는 service-local setup만 사용한다

---

## 12. 단계별 구현 순서

### Phase 0: 공용 runtime

- `discovery_topology_uplink_t`
- `Discovery connect_registry bootstrap`
- `Receiver` 경로 정리

완료 기준:

- receiver 기반 summary report가 discovery uplink 위에서도 계속 동작
- receiver provider registration과 topology summary 경로가 코드에서 분리됨
- receiver-local heartbeat 의존이 제거됨

`Receiver`를 Phase 0에 두는 이유:

- 기존 provider register/unregister/heartbeat 경로가 이미 존재한다
- uplink runtime을 가장 적은 추가 변수로 검증할 수 있다
- `Gateway`나 `Spot`보다 state trigger와 ownership이 단순하다
- 이후 phase에서 `Gateway`, `Spot`을 올릴 때 기준 구현 역할을 한다

### Phase 1: Discovery / Gateway

- discovery summary report
- gateway summary report
- registry query test 추가

완료 기준:

- `Discovery`, `Gateway` entry가 registry snapshot/query에 안정적으로 보임

### Phase 2: Spot

SPOT topology summary 구현은
[`spot-proxy-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-proxy-rewrite-spec.ko.md)
section 15 Phase 5와 연계한다.

완료 기준:

- SPOT 문서 section 14.3 topology summary 테스트 통과
- timeout/deadlock 없음

### Phase 3: 문서 / perf 정리

- `doc/api`
- `doc/guide`
- `doc/perf`
- service perf setup를 report/query 기반 예시로 정리

완료 기준:

- perf setup이 provider heartbeat와 topology readiness를 혼동하지 않음
- perf/setup이 discovery heartbeat를 유일한 registry freshness source로 사용
- service perf path가 새 summary/query/monitor 역할 분리를 문서와 같이 사용

---

## 13. 기대 효과

### 13.1 구현 안정성

- service별 ad-hoc sender 제거
- report delivery loss 감소
- deadlock/timeout 위험 감소

### 13.2 API 일관성

- `monitor`, `RID`, `registry summary`가 같은 identity model을 공유
- `Receiver`만 특수한 구현이라는 느낌이 줄어듦

### 13.3 운영/관찰성

- registry snapshot이 실제로 신뢰 가능한 global summary가 됨
- 이상 징후를 먼저 registry에서 보고
- 필요 시 local monitor로 drill-down 하는 흐름이 안정화됨

---

## 14. 리스크와 대응

| 리스크 | 설명 | 대응 |
|---|---|---|
| sender lifetime 누수 | uplink socket이 discovery destroy 후 남을 수 있음 | discovery uplink close 의무화 |
| lock-order 문제 | state update와 registry send가 엮이면 deadlock 가능 | submit/send 분리, safe call site 고정 |
| report storm | count-changed 과다 report 가능 | discovery dirty-store + change-only flush |
| Spot semantics 애매함 | strong readiness가 아직 없음 | SPOT 문서 section 12.3 state mapping 기준으로 Phase 2 구현 |
| bootstrap 실패 | discovery/broadcast와 topology uplink 구성이 함께 실패할 수 있음 | bootstrap failure를 connect failure로 명확히 취급 |
| Discovery 없는 구성 | registry visibility가 없음 | 문서에 intended limitation으로 명시 |
| multi-Discovery 중복 보고 | 같은 key가 여러 discovery에서 반복 upsert될 수 있음 | registry upsert idempotent 처리, 최신 report 시간 기준 수렴 |

---

## 15. 최종 권장 방향

이 이슈의 핵심은 “service마다 topology report를 어떻게든 보내게 만들자”가 아니다.

정답은:

```text
registry uplink는 Discovery 하나가 맡고,
각 service는 local summary만 Discovery에 제출하게 고정한다.
```

즉:

- public API는 최소 추가
- internal runtime contract는 명확하게
- monitor와 topology summary는 연결되지만, 직접 뒤섞지 않게

이 방향이 현재 문제를 가장 근본적으로 줄인다.
