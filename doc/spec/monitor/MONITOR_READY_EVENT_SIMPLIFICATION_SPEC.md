# Monitor Low-Cost Event Model Redefinition Spec

## 목적

현재 monitor는 관찰용 surface를 넘어서 readiness coordination 일부까지 담당하고 있다.
특히 delivery-ready/count 계열과 source마다 다른 ready 의미는 대규모 topology
에서 유지 비용과 오용 비용이 크다.

- subject별 subscription 상태 유지
- peer join/leave/reconnect 시 replay/sync
- subject별 remote membership 추적
- ready count 집계와 snapshot 노출

이 문서는 monitor를 다시 **low-cost observability surface** 로 재정의한다.

- monitor는 싼 edge/event만 남긴다.
- aggregate readiness/count/membership 유지는 monitor contract에서 제거한다.
- perf는 raw pattern 에서만 low-cost ready event counting 을 사용한다.
- SPOT perf 는 monitor gate 를 사용하지 않고 explicit `READY/START` barrier
  protocol 로 시작한다.
- snapshot은 모든 source 에서 구조적으로 지원되는 현재 상태만 제공한다.

이 변경은 public contract breaking change 다.

### 저장소 공통 규칙과의 관계

이 스펙은 저장소의 fail-fast 규칙을 그대로 따른다.

- 일반 `core/tests/` 는 deterministic bounded wait 만 사용한다.
- raw perf 는 `CONNECTION_READY` counting 기반 bounded wait 만 사용한다.
- SPOT perf 는 explicit `READY/START` barrier 기반 bounded wait 만 사용한다.
- sleep 기반 synchronization 예외는 두지 않는다.


## 설계 원칙

### 유지 기준

- transport/service의 직접적인 로컬 상태 변화다.
- event 의미를 유지하기 위해 비싼 aggregate membership state가 필요하지 않다.
- 운영 진단, 장애 분석, 시작 가능 시점 판단에 직접 유용하다.
- watcher가 없을 때 별도 유지비용을 만들지 않는다.

### 삭제 기준

- exact ready count 또는 membership을 노출해야 한다.
- subject 수 x peer 수에 비례하는 상태 유지가 필요하다.
- perf barrier를 위해서만 존재한다.
- event 하나를 위해 replay/sync 비용을 계속 강제한다.


## 최종 결정

### 유지 이벤트

#### Raw socket monitor

- `CONNECTED`
- `ACCEPTED`
- `DISCONNECTED`
- `LISTENING`
- `BIND_FAILED`
- `ACCEPT_FAILED`
- `CLOSED`
- `CLOSE_FAILED`
- `HANDSHAKE_FAILED_NO_DETAIL`
- `HANDSHAKE_FAILED_PROTOCOL`
- `HANDSHAKE_FAILED_AUTH`
- `MONITOR_STOPPED`
- `CONNECTION_READY`

#### Snapshot

- `source_kind`
- `state_flags`
- `snd_pending_msgs`
- `rcv_pending_msgs`

### 삭제 이벤트 / 항목

#### Raw socket monitor

- `SUB_DELIVERY_READY_CHANGED`
- `PUB_DELIVERY_READY_CHANGED`

#### SPOT service monitor

- SPOT service monitor surface 전체

#### Snapshot

- `zlink_monitor_snapshot_t.ready_count`
- `ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_COUNT`


## 현재 vs 변경 후 표

### Raw socket monitor

| 이벤트 | 현재 | 변경 후 | 결정 |
|---|---|---|---|
| `CONNECTION_READY` | raw usable connection ready edge | raw send/recv ready edge로 유지 | 유지 |
| `SUB_DELIVERY_READY_CHANGED` | SUB delivery-ready 의미 | 제거 | 삭제 |
| `PUB_DELIVERY_READY_CHANGED` | PUB ready subscriber 의미 | 제거 | 삭제 |
| 기타 transport/session 이벤트 | transport 진단 | 동일 | 유지 |

### SPOT service monitor

| 항목 | 현재 | 변경 후 | 결정 |
|---|---|---|---|
| service monitor surface | topology / filter / queue / ready 혼합 | public surface 에서 제거 | 삭제 |

### Snapshot

| 항목 | 현재 | 변경 후 | 결정 |
|---|---|---|---|
| `ready_count` | aggregate ready count | 제거 | 삭제 |
| `snd_pending_msgs` | queue depth | 동일 | 유지 |
| `rcv_pending_msgs` | queue depth | 동일 | 유지 |
| `state_flags` | 현재 상태 | raw socket에서만 `READY`를 유지하고 SPOT source에서는 coordination 의미를 제거 | 유지 |


## 이벤트 payload 계약

### Raw socket monitor event 구조

```c
typedef struct {
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;
```

### Raw socket monitor payload 표

| 이벤트 | `value` | `routing_id` | `local_addr` | `remote_addr` | 비고 |
|---|---|---|---|---|---|
| `CONNECTED` | transport/provider-specific 보조값, ready count 아님 | peer event 에서는 유효한 peer 식별자 | 항상 초기화 | 항상 초기화 | transport connect edge |
| `ACCEPTED` | transport/provider-specific 보조값, ready count 아님 | peer event 에서는 유효한 peer 식별자 | 항상 초기화 | 항상 초기화 | accept edge |
| `DISCONNECTED` | transport/provider-specific 보조값, ready count 아님 | peer event 에서는 유효한 peer 식별자 | 항상 초기화 | 항상 초기화 | disconnect edge |
| `LISTENING` | transport/provider-specific 보조값, ready count 아님 | peer 없음, sentinel/empty 허용 | 항상 초기화 | 항상 초기화 | bind/listen 성공 |
| `BIND_FAILED` | errno 또는 provider-specific 오류값 가능 | peer 없음, sentinel/empty 허용 | 항상 초기화 | 항상 초기화 | 실패 원인은 event 종류와 errno 로 판정 |
| `ACCEPT_FAILED` | errno 또는 provider-specific 오류값 가능 | peer 없음, sentinel/empty 허용 | 항상 초기화 | 항상 초기화 | 실패 edge |
| `CLOSED` | 0 또는 reserved | peer 없음, sentinel/empty 허용 | 항상 초기화 | 항상 초기화 | close edge |
| `CLOSE_FAILED` | errno 또는 provider-specific 오류값 가능 | peer 없음, sentinel/empty 허용 | 항상 초기화 | 항상 초기화 | close 실패 |
| `HANDSHAKE_FAILED_NO_DETAIL` | provider-specific 보조값 가능 | peer event 에서는 유효한 peer 식별자 | 항상 초기화 | 항상 초기화 | 상세 cause는 transport/provider 구현 의존 |
| `HANDSHAKE_FAILED_PROTOCOL` | provider-specific 보조값 가능 | peer event 에서는 유효한 peer 식별자 | 항상 초기화 | 항상 초기화 | protocol failure edge |
| `HANDSHAKE_FAILED_AUTH` | provider-specific 보조값 가능 | peer event 에서는 유효한 peer 식별자 | 항상 초기화 | 항상 초기화 | auth failure edge |
| `MONITOR_STOPPED` | 0 또는 reserved | peer 없음, sentinel/empty 허용 | 항상 초기화 | 항상 초기화 | monitor lifecycle edge |
| `CONNECTION_READY` | reserved, ready count 아님 | 유효한 ready peer 식별자 | 항상 초기화 | 항상 초기화 | raw send/recv ready edge |

Raw socket contract 에서 payload field 는 항상 초기화된 값으로 전달되어야 한다.
peer 와 직접 연결된 event 에서는 `routing_id` 가 공식 peer 식별자다. peer 가 없는
event 는 `routing_id=0` 같은 sentinel 과 빈 address 를 허용한다. `value` 만 보조
정수 payload 로 남으며, ready count, subscriber count, exact connection
cardinality 로는 해석하지 않는다.

### Service monitor event 구조

```c
typedef struct zlink_service_event_t
{
    zlink_service_kind_t service_kind;
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    zlink_service_event_detail_mask_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    char subject[256];
    uint32_t subject_kind;
} zlink_service_event_t;
```

### Service monitor 공통 payload 규칙

| 필드 | 의미 | 사용 규칙 |
|---|---|---|
| `service_kind` | discovery / spot sub / spot pub / socket | 항상 유효 |
| `event_type` | 실제 event mask 값 | 항상 유효 |
| `status` | 서비스별 상태 코드 | event 별로 필요할 때만 사용, 없으면 0 |
| `error_code` | 오류 코드 | `ERROR`, 실패 계열에서만 의미 있음 |
| `value` | 보조 정수 payload | ready count 로 해석 금지, event별 reserved 또는 provider-specific |
| `detail_flags` | 아래 optional field 유효 여부 | 항상 확인 후 읽음 |
| `service_name` | 서비스 이름 | 항상 초기화, 의미 판정은 `DETAIL_SERVICE_NAME` 기준 |
| `endpoint` | local 또는 relevant endpoint | 항상 초기화, 의미 판정은 `DETAIL_ENDPOINT` 기준 |
| `routing_id` | peer/subscriber 식별자 | 항상 초기화, 의미 판정은 `DETAIL_SUBJECT_RID` 또는 `DETAIL_PEER_RID` 기준 |
| `subject` | topic/pattern subject | 항상 초기화, 의미 판정은 `DETAIL_SUBJECT` 기준 |
| `subject_kind` | topic/pattern 종류 | 항상 초기화, 의미 판정은 `DETAIL_SUBJECT_KIND` 기준 |

### SPOT service monitor payload 표

SPOT service monitor 는 삭제 대상이므로 payload contract 를 유지하지 않는다.
SPOT 관찰 surface 는 `spot node` snapshot/query API 와 raw socket monitor 내부
구현으로만 남긴다.


## 왜 이 이벤트들이 비싼가

### `PUB_DELIVERY_READY_CHANGED`

가장 비싼 축이다. 유지하려면:

- subject별 remote subscriber membership 추적
- peer별 readiness 반영
- reconnect 시 membership 복구
- subject별 집계 갱신

즉 `subject x peer` 상태를 monitor contract 때문에 계속 유지해야 한다.

### `SUB_DELIVERY_READY_CHANGED`

sub 측 delivery-safe 상태를 유지하려면:

- local filter install
- peer 전파 상태
- delivery-safe 진입 여부
- reconnect 후 재판정

이 역시 local-only edge보다 훨씬 무겁다.

### `SUB_SUBSCRIPTION_READY_CHANGED`

delivery-ready보다 앞단이지만 여전히 aggregate subscription readiness state를
유지해야 하므로 비용이 있다.

### `PUB_FIRST_DELIVERY_READY_CHANGED`

이 event 는 publisher/service 의 first send-ready 의미를 별도 이름으로 정의한다.
하지만 SPOT service layer 에서는 이 의미를 구조적으로 안정적으로 제공하지 못한다.
이번 재정의에서는 이 event 자체를 삭제하고, SPOT monitor 는 topology / filter /
queue-pressure 관찰만 남긴다.


## 새 monitor contract

### Raw socket monitor

raw socket monitor는 transport/session 관찰과 raw send/recv ready 계약만 남긴다.

- connect 성공/지연/재시도
- accept 성공/실패
- handshake 성공/실패
- disconnect/close
- raw send/recv 시작 가능 edge (`CONNECTION_READY`)

삭제되는 의미:

- subscriber propagation readiness
- publisher-side subscriber cardinality
- delivery-ready coordination

### SPOT service monitor

SPOT service monitor 는 유지하지 않는다. SPOT public observability surface 는 아래로
단순화한다.

- `spot_node_status_snapshot()`
- `spot_node_peers_snapshot()`
- `spot_node_subjects_snapshot()`
- 필요 시 내부 raw socket monitor (public contract 아님)


## Snapshot contract 재정의

### 변경 후 구조

```c
typedef struct zlink_monitor_snapshot_t
{
    zlink_monitor_source_kind_t source_kind;
    zlink_monitor_state_mask_t state_flags;
    zlink_monitor_snapshot_detail_mask_t detail_flags;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_monitor_snapshot_t;
```

### snapshot 역할

- queue/backpressure 관찰
- source 공통 level 상태 관찰
- 운영 디버깅

snapshot은 더 이상 제공하지 않는다.

- ready peer 수
- ready subscriber 수
- coordination barrier 의미

### `state_flags` 의미

`zlink_monitor_snapshot_t.state_flags` 는 source 별로 다른 임의 의미를 허용하지
않는다. 특히 `READY` 는 지원되는 source 에서 아래 공통 의미로 통일한다.

- 마지막으로 관측된 `CONNECTION_READY` 와 동일 계약의 현재 level 상태
- raw socket 에서는 send/recv 가능한 usable connection 이 존재함
- `ZLINK_MONITOR_SOURCE_SPOT_PUB` 에서는 `READY` 를 사용하지 않으며 0으로 유지함
- `ZLINK_MONITOR_SOURCE_SPOT_SUB` 에서는 `READY` 를 사용하지 않으며 0으로 유지함

반대로 아래 의미는 공통 snapshot 에 두지 않는다.

- exact delivery-ready
- ready peer/subscriber count
- subject별 membership 집계
- source 전용 `SEND_READY`


## Perf 대체 모델

### 공통 원칙

- perf는 delivery-ready exactness를 monitor에서 묻지 않는다.
- raw pattern 은 low-cost event counting 을 사용한다.
- SPOT 은 monitor gate 를 사용하지 않고 explicit `READY/START` barrier protocol 을
  사용한다.

### Multi raw 패턴

- gate event: `CONNECTION_READY`
- 판정 방식: expected client 수만큼 counting
- 시작: gate 충족 후 warmup/active 진입

### Multi SPOT

- 각 client spot 은 아래가 끝나면 control topic 으로 `READY` 를 보낸다.
  - peer connect
  - subscription setup
  - recv/callback setup
- server spot 은 unique client 기준으로 `READY` 를 `expected_clients` 개 수신할
  때까지 bounded wait 를 수행한다.
- server spot 은 모두 모이면 control topic 으로 `START` 를 broadcast 한다.
- 모든 client 와 server 는 `START` 수신 후에만 warmup/active 로 진입한다.
- monitor event, snapshot polling, fixed settle sleep 을 start gate 로 사용하지
  않는다.

### Single

- raw 패턴: `CONNECTION_READY`
- SPOT: 같은 프로세스 안에서 explicit local `READY/START` barrier 를 사용한다.
- SPOT single 도 service monitor 를 start gate 로 사용하지 않는다.


## 구현 변경 상세

### Public header

`core/include/zlink.h`

- `zlink_monitor_snapshot_t.ready_count` 제거
- `ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_COUNT` 제거
- 삭제 대상 event enum 정리
- monitor/event 주석에서 delivery-ready/count 의미 제거

### 구현 범위 체크리스트

이 변경은 문서만 바꾸고 끝낼 수 없다. 아래 범위를 한 세트로 추적해야 한다.
체크리스트 목적은 실제 구현 변경에서 누락을 막는 것이다.

#### 1. `core/include/`

- `core/include/zlink.h`
  - monitor snapshot 구조체에서 `ready_count` 제거
  - snapshot detail mask 에서 `READY_COUNT` 제거
  - `SEND_READY` 같은 source-specific ready state 제거
  - raw socket monitor event enum 에서 삭제 대상 event 제거
  - service monitor event enum / alias 에서 SPOT event 전체 제거
  - `CONNECTION_READY_CHANGED` 를 `CONNECTION_READY` 로 rename
  - raw socket 의 `CONNECTION_READY` 계약 주석만 유지
  - SPOT service monitor enum / option / alias surface 제거
  - `ZLINK_EVENT_ALL`, `ZLINK_SERVICE_MONITOR_EVENT_ALL` 같은 aggregate mask 를
    새 event set 기준으로 재정의
  - discovery/spot ready-count 관련 주석 제거
- public header 를 그대로 복제/동기화하는 binding include 사본이 있으면 동일 반영

#### 2. `core/src/`

- raw socket monitor emit 경로
  - `SUB_DELIVERY_READY_CHANGED`
  - `PUB_DELIVERY_READY_CHANGED`
  제거
- SPOT service monitor emit/open/snapshot 경로 전체 제거
- discovery/service snapshot provider 에서 ready-count 계산 제거
- socket monitor snapshot provider 에서 ready-count 계산 제거
- monitor contract 때문에만 유지되던 aggregate membership/count 상태 제거
- monitor watcher 가 없을 때 불필요한 집계/유지 경로가 남아 있지 않은지 확인

#### 3. `core/tests/`

- monitor API/ABI 회귀 추가/수정
- 삭제 대상 event 가 더 이상 수신되지 않음을 검증
- snapshot 에 `ready_count` 가 없음을 검증
- raw snapshot `READY` 가 `CONNECTION_READY` 와 동일 계약을 유지함을 검증
- SPOT snapshot 에서 `READY` 가 사용되지 않음을 검증
- raw socket regression:
  - `CONNECTION_READY` 기반 send/recv 시작 가능 계약 검증
- SPOT regression:
  - service monitor 가 더 이상 열리지 않음 검증
  - node snapshot/query 기반 observability 유지 검증
- 기존 delivery-ready/count 기반 테스트는 새 contract 기준으로 전면 정리

#### 4. `core/perf/`

- single perf:
  - raw PUB/SUB gate 를 `CONNECTION_READY` 로 유지
  - SPOT gate 를 explicit local `READY/START` barrier 로 전환
  - delivery-ready/count gate 제거
- multi perf:
  - raw PUB/SUB gate 를 `CONNECTION_READY` counting 으로 유지
  - SPOT gate 를 explicit `READY/START` barrier protocol 로 전환
  - quorum / exact-count / snapshot polling / `event.value` / service monitor gate 제거
- perf helper/common:
  - `wait_ready()` 류 helper 가 raw=`CONNECTION_READY` counting, SPOT=`READY/START`
    barrier 만 수행하도록 정리
  - delivery-ready/count helper 삭제
- perf runner/report:
  - 문구, effective options, error text 에 delivery-ready/count 용어 제거

#### 5. `bindings/` public surface

- binding enum / constant surface 에서 삭제 대상 monitor event 제거
- binding 의 `ALL` / aggregate event mask surface 도 새 event set 기준으로 재정의
- binding monitor snapshot object 에서 `readyCount` 제거
- `event.value` 를 ready count 로 해석하는 helper 제거
- service/socket monitor convenience helper 가 delivery-ready/count gate 를 만들지 않도록 정리
- binding generated docs / README / API reference 와 public surface 를 일치시킴

#### 6. `bindings/` samples / perf / tests

- sample code:
  - raw 는 `CONNECTION_READY`
  - SPOT 은 explicit `READY/START` barrier 로 정렬
- binding perf:
  - delivery-ready/count 의존 제거
  - `event.value > 0` 류 gate 제거
- binding tests:
  - 삭제된 event / snapshot field 를 더 이상 기대하지 않도록 수정
  - low-cost event gate 계약으로 회귀 추가

#### 7. 문서

- `doc/spec/monitor/`
- `doc/api/`
- `doc/guide/`
- `doc/perf/`
- `bindings/* README`
를 한 방향으로 맞춘다.
- `AGENTS.md` 또는 동등한 repository-wide test policy 문서와 충돌하지 않게
  raw=`CONNECTION_READY`, SPOT=`READY/START` bounded wait 기준으로 정렬한다.

#### 8. 호환성/릴리즈 노트

- breaking change 목록에 포함
- migration note 에 아래 항목 명시
  - snapshot `ready_count` 삭제
  - delivery-ready/count monitor event 삭제
  - `*_ALL` aggregate mask 에서 삭제 이벤트가 빠짐
  - raw perf gate 의 `CONNECTION_READY` 유지
  - SPOT perf gate 의 explicit `READY/START` barrier 전환

#### 9. 정책 선행 조건

- repository test policy / `AGENTS.md` 와 충돌하지 않게 perf 문서를 정렬
- raw perf 는 `CONNECTION_READY` counting bounded wait 로만 설명
- SPOT perf 는 explicit `READY/START` barrier bounded wait 로만 설명

### 구현 실행 순서

이 변경은 `core` 와 `bindings` 를 동시에 섞어서 진행하지 않는다.
`bindings` 는 변경된 native library 가 먼저 준비되어야 검증 가능하므로,
아래 순서를 표준 실행 순서로 사용한다.

#### 1. `core` 선행 변경

먼저 아래 범위만 변경한다.

- `core/include/`
- `core/src/`
- `core/tests/`
- `core/perf/`

이 단계에서는 `bindings` runtime/ffi/perf/tests 구현을 같이 수정하지 않는다.
문서는 병행 수정 가능하지만, 실제 바인딩 구현은 native contract 가 고정된 뒤에
들어간다.

#### 2. `core/build/` 기준 검증

`core` 변경이 끝나면 `core/build/` 만 사용해서 빌드와 테스트를 완료한다.

- header/ABI 빌드 확인
- `core/tests/` monitor regression 확인
- 필요 시 `core/perf/` smoke 확인

이 단계가 통과하기 전에는 binding 쪽 구현 변경을 진행하지 않는다.

#### 3. 임시 native 배포

`bindings` 작업을 위해 임시 native library 배포물을 만든다.

- 목적: bindings 가 변경된 ABI/API 를 실제로 링크하고 테스트할 수 있게 함
- 성격: 작업용 임시 배포물이며 정식 릴리즈 태그가 아님
- 형태: 각 binding 이 소비하는 로컬/사내 배포 경로 기준 임시 artifact

이 임시 배포물은 다음 조건을 만족해야 한다.

- 변경된 `core/include/zlink.h` 반영
- 변경된 monitor ABI/API 반영
- bindings 테스트 환경에서 설치/참조 가능

#### 4. `bindings` 구현 변경

임시 배포물이 준비된 뒤에 아래 범위를 변경한다.

- bindings runtime / ffi
- bindings public API surface
- bindings samples
- bindings perf
- bindings tests
- bindings docs / README

즉 `bindings` 는 항상 변경된 native artifact 를 기준으로 맞춘다.

#### 5. `bindings` 검증

binding 별 빌드/테스트를 수행한다.

- binding runtime build
- binding unit/integration/tests
- binding samples smoke
- binding perf smoke

이 단계에서는 더 이상 old native contract 와의 양립을 목표로 하지 않는다.
breaking change 는 새 contract 로 일괄 전환한다.

#### 6. 정식 배포 준비

`core` 와 `bindings` 가 모두 새 contract 로 정렬된 뒤 정식 배포를 준비한다.

- 변경 로그 정리
- breaking change note 정리
- migration guide 정리
- 최종 release artifact / tag 준비

### POSD 기반 리팩토링 단계

위 계약 변경이 끝나면, 삭제로 인해 남은 복잡도 잔재를 줄이기 위한 POSD 기반
리팩토링을 반복 루프로 수행한다. 목적은 동작 유지가 아니라 시스템 설명 비용과
변경 증폭을 줄이는 것이다.

#### 리팩토링 목표

- 삭제된 monitor contract 때문에만 남아 있는 얕은 wrapper 제거
- source/socket/service 별 중복 ready 계산 경로 통합
- dead state, dead branch, legacy alias, unused helper 제거
- monitor 와 perf 가 공유하던 숨은 결합 축소
- 공통 contract 와 source-specific contract 의 경계를 더 깊고 명확한 모듈로 재편

#### 리팩토링 기준

- 외부 contract 설명에 필요 없는 내부 단계는 합치거나 제거
- 동일 의미를 다른 이름으로 반복하는 enum/alias/helper 는 하나로 통합
- watcher 유무와 무관하게 유지되던 불필요한 상태는 제거
- 테스트를 통과시키기 위한 temporal decomposition 코드는 더 단순한 module API 로 흡수
- 문서 설명이 한두 문장으로 안 되는 경로는 설계 smell 로 보고 다시 단순화
- 도달 불가능한 dead code, 호출되지 않는 helper, 더 이상 읽히지 않는 state,
  의미 없는 fallback/compat path 는 찾아서 삭제

#### 리팩토링 루프

각 라운드는 아래 순서로 반복한다.

1. 현재 contract 기준으로 불필요한 branch, state, alias, helper, wrapper, shim,
   dead code 후보를 리뷰한다.
2. 실제로 불필요하거나 legacy-only 인 코드를 삭제하거나 더 깊은 모듈로 흡수한다.
3. 회귀와 빌드를 다시 확인한다.
4. 다시 리뷰해서 추가로 줄일 수 있는 shallow layer, hidden coupling, dead path 가
   남아 있는지 확인한다.

이 루프는 새 삭제/단순화 후보가 더 이상 나오지 않을 때까지 반복한다.

#### 우선 정리 대상

- old ready/delivery/count 삭제 후 남은 compatibility alias
- socket monitor 와 service monitor 의 중복 snapshot/ready bookkeeping
- SPOT pub/sub 에 남은 subject membership 유지 코드 중 monitor 계약 때문에만 존재하던 축
- perf helper 에 남아 있는 event normalization / dedupe / barrier wrapper 중 얕은 계층
- bindings 별 enum rename shim, deprecated constant shim, old helper forwarding layer

#### 실행 시점

- 1차: `core` 계약 변경과 회귀가 닫힌 직후
- 2차: `bindings` 반영이 끝난 직후
- 정식 배포 전, dead code 와 중복 계층이 남아 있지 않은지 마지막 정리 수행

#### 완료 조건

- 새 contract 를 설명할 때 old event/history/compat layer 를 언급하지 않아도 됨
- monitor ready 경로가 raw 와 service 각각에서 한 개의 주 경로로 설명 가능함
- 삭제된 event/count 를 위한 전용 helper/state 가 더 이상 남아 있지 않음
- 같은 의미를 가진 enum/alias/helper 가 두 군데 이상 존재하지 않음
- 반복 리뷰에서 dead code, unused helper, unnecessary wrapper, meaningless fallback
  후보가 더 이상 발견되지 않음
- dead code 와 불필요한 코드가 실제로 삭제되어 더 이상 build/test surface 에
  남아 있지 않음
- 변경 후 문서와 코드의 개념 수가 함께 줄어듦

### 작업 관리 원칙

- `core` 가 검증되기 전에는 `bindings` 구현을 확정하지 않는다.
- `bindings` 는 임시 native 배포물을 기준으로 순차 적용한다.
- `core` 와 `bindings` 가 서로 다른 monitor contract 를 잠시라도 public 하게
  노출하지 않도록, 임시 배포 단계와 정식 배포 단계를 분리한다.
- 작업 체크는 항상 `core 완료 -> 임시 배포 -> bindings 완료` 순서로 닫는다.
- 계약 변경 완료만으로 종료하지 않고, POSD 리팩토링 단계까지 닫아야 작업을 완료로 본다.

### 코드 변경 대상 파일군

아래는 구현 시 실제로 빠짐없이 확인해야 하는 대표 파일군이다.
파일명이 일부 달라도 같은 책임 모듈이면 동일 범위로 본다.

#### core header / ABI

- `core/include/zlink.h`

#### core monitor / service runtime

- `core/src/*monitor*`
- `core/src/services/common/*monitor*`
- `core/src/services/spot/*`
- `core/src/discovery/*`

#### core regression tests

- `core/tests/integration/monitoring/*`
- `core/tests/e2e/spot/*`
- monitor snapshot / monitor contract 검증 파일

#### core perf

- `core/perf/single/common/*`
- `core/perf/single/src/*`
- `core/perf/multi/common/*`
- `core/perf/multi/src/*`
- `core/perf/run_*.py`
- `core/perf/*.sh`

#### bindings runtime / ffi

- `bindings/node/src/*`
- `bindings/node/native/src/*`
- `bindings/python/*`
- `bindings/rust/src/*`
- `bindings/go/*`
- `bindings/java/*`
- 각 바인딩의 bundled/generated `zlink.h` 사본

#### bindings verification surfaces

- `bindings/*/samples/*`
- `bindings/*/perf/*`
- `bindings/*/tests/*`


## 테스트 스펙

### API/ABI 회귀

- `zlink_monitor_snapshot_t`에서 `ready_count` 제거 검증
- `READY_COUNT` detail flag 제거 검증
- 삭제 대상 monitor event가 더 이상 노출되지 않음 검증
- raw snapshot `READY` state 가 `CONNECTION_READY` 와 동일 계약을 유지함 검증
- SPOT snapshot 은 `READY` 를 coordination 의미로 노출하지 않음 검증

### Raw socket 회귀

- `CONNECTION_READY`만으로 send/recv 시작 가능 검증
- 기존 delivery-ready raw event 미노출 검증

### SPOT 회귀

- service monitor open/recv surface 제거 검증
- node snapshot/query observability 유지 검증
- 삭제 대상 SPOT monitor event 미노출 검증

### Perf/process 회귀

- multi raw:
  - `CONNECTION_READY` counting 으로 `100`, `1000` 검증
- multi SPOT:
  - `READY/START` barrier 로 `100`, `400`, `1000` 검증


## 문서 업데이트 대상

- `doc/api/monitoring.md`
- `doc/api/monitoring.ko.md`
- `doc/api/events.md`
- `doc/api/events.ko.md`
- `doc/api/README.md`
- `doc/api/README.ko.md`
- `doc/guide/06-monitoring.md`
- `doc/guide/06-monitoring.ko.md`
- `doc/perf/PERF_POLICY.md`
- `doc/perf/PERF_SINGLE_TEST_POLICY.md`
- `doc/perf/PERF_MULTI_TEST_POLICY.md`
- bindings public docs


## 호환성

이번 변경은 즉시 breaking change 다.

영향 받는 사용자:

- `CONNECTION_READY_CHANGED` 이름을 직접 참조하는 사용자
- monitor `value`를 ready count 로 읽는 사용자
- snapshot `ready_count`를 읽는 사용자
- delivery-ready/count 이벤트를 gate 로 쓰는 perf/bench/helper
- SPOT service monitor surface 를 직접 여는 사용자
- bindings monitor wrapper에서 ready-count surface 를 노출하는 코드

호환 전략:

- deprecated 단계 없이 즉시 제거
- `CONNECTION_READY_CHANGED` 는 `CONNECTION_READY` 로 즉시 rename
- 문서, 헤더, 구현, 테스트를 한 번에 업데이트


## 최종 요약

이번 재정의의 핵심은 다음 한 줄이다.

**monitor는 cheap observability만 남기고, expensive readiness coordination은 제거한다.**

실제 perf 시작 기준은 다음으로 단순화한다.

- raw: `CONNECTION_READY` x expected clients
- SPOT: explicit `READY/START` barrier

이 방향이 대규모 topology 에서 가장 설명 가능하고, 유지비용이 낮고,
perf와 runtime의 책임 분리도 가장 명확하다.
