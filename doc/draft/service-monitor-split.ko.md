[스펙 목차](../README.ko.md)

# Draft -- Service Monitor Removal

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API, 상수, 기본 동작을
> 보장하지 않는다.
> 구현과 공개 헤더, 관련 테스트, 정식 문서가 확정되면 적절한 spec 문서로 나누어
> 반영한다.

## 1. 목적

이 초안은 현재의 service monitor 계층을 아예 제거하는 방향을 정리한다.

최종 방향은 아래와 같다.

- raw socket monitor는 `zlink_socket_monitor_open()`만 유지한다.
- `Discovery`는 monitor 없이 snapshot/query만 사용한다.
- `SpotNode`는 monitor 없이 snapshot/query만 사용한다.
- `Registry`는 monitor 없이 snapshot/query만 사용한다.
- `Spot`은 monitor 없이 기존 facade API와 snapshot/query만 사용한다.

즉 service handle 계열에 대한 공통 monitor surface는 더 이상 두지 않는다.

## 2. 왜 제거하는가

이 계층은 이름과 실제 의미가 잘 맞지 않는다.

- `zlink_service_monitor_open()`은 범용 service monitor처럼 보이지만, 실제로는
  대상별 의미가 다르다.
- `Registry`는 원래 이 경로를 타지 않아서 service handle 전체를 통일하지 못한다.
- `Discovery`, `SpotNode`도 운영 판단의 핵심은 현재 상태 조회이며, monitor보다
  snapshot/query가 더 직접적이다.
- `Spot`은 monitor보다 facade API와 pub/sub surface가 더 자연스럽다.

결과적으로 monitor를 유지할수록 공개 API와 문서가 복잡해지고, 사용자가 얻는
이익은 크지 않다.

## 3. 호환성 정책

이 초안은 **호환성 유지 없이 바로 반영**하는 방향을 전제로 한다.

즉 아래 정책을 따른다.

- deprecated 단계 없음
- compatibility shim 없음
- `zlink_service_monitor_open()` 계열은 바로 제거
- service monitor 전용 enum, 타입, 옵션도 함께 제거

## 4. 삭제할 공개 API

아래 공개 API는 제거 대상이다.

| API |
|---|
| `void *zlink_service_monitor_open(void *target_, const zlink_service_monitor_open_options_t *options_);` |
| `zlink_handler_result_t zlink_service_monitor_handler(void *monitor_, zlink_service_monitor_handler_fn handler_, void *userdata_);` |
| `zlink_recv_result_t zlink_service_monitor_recv(void *monitor_, zlink_service_monitor_event_t *out_, zlink_recv_flags_t flags_);` |

## 5. 삭제할 공개 타입

아래 공개 타입은 service monitor 계층과 함께 제거 대상이다.

| 타입 |
|---|
| `zlink_service_event_t` |
| `zlink_service_monitor_handler_fn` |
| `zlink_service_monitor_event_t` |
| `zlink_service_monitor_event_detail_mask_t` |
| `zlink_service_monitor_open_options_t` |

## 6. 삭제할 enum / 상수

### 6.1 service monitor event mask

아래 상수는 service monitor 전용 event mask이므로 전부 제거한다.

| 이름 |
|---|
| `ZLINK_SERVICE_MONITOR_EVENT_ERROR` |
| `ZLINK_SERVICE_MONITOR_EVENT_CLOSED` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_ALL` |

### 6.2 discovery monitor event mask

`Discovery` monitor 자체를 유지하지 않으므로 아래 상수도 제거 대상이다.

다만 `ZLINK_MONITOR_EVENT_ERROR`, `ZLINK_MONITOR_EVENT_CLOSED`는 raw socket
monitor 쪽에서 계속 쓸 수 있으므로 이 문서의 삭제 목록에 넣지 않는다.

| 이름 |
|---|
| `ZLINK_DISCOVERY_MONITOR_EVENT_ERROR` |
| `ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP` |
| `ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN` |
| `ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED` |
| `ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED` |
| `ZLINK_DISCOVERY_SERVICE_UP` |
| `ZLINK_DISCOVERY_SERVICE_DOWN` |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` |

### 6.3 monitor target kind

service handle monitor target 식별값도 함께 제거한다.

| 이름 |
|---|
| `ZLINK_MONITOR_TARGET_DISCOVERY` |
| `ZLINK_MONITOR_TARGET_SPOT` |
| `ZLINK_MONITOR_TARGET_SPOT_NODE` |

`ZLINK_MONITOR_TARGET_SOCKET`은 raw socket monitor에 계속 필요하므로 유지한다.

## 7. 삭제 목록에서 제외하는 항목

아래 항목은 이름이 비슷해 보여도 이번 제거 범위에는 넣지 않는다.

| 이름 | 이유 |
|---|---|
| `zlink_service_kind_t` | 현재 service summary, registry, 상태 구조체 쪽에서 계속 쓸 수 있다 |
| `zlink_service_event_detail_mask_t` | monitor 외의 상태 payload 정리에 재사용될 여지가 있다 |
| `ZLINK_MONITOR_EVENT_ERROR` | raw socket monitor alias로 계속 필요하다 |
| `ZLINK_MONITOR_EVENT_CLOSED` | raw socket monitor alias로 계속 필요하다 |
| `ZLINK_MONITOR_TARGET_SOCKET` | raw socket monitor target 식별값이다 |

## 8. 회귀 테스트 항목

구현 시 아래 항목을 회귀 테스트에 포함한다.

1. `zlink.h`에서 아래 선언이 제거되었는지 확인한다.
   - `zlink_service_monitor_open`
   - `zlink_service_monitor_handler`
   - `zlink_service_monitor_recv`
   - `zlink_service_event_t`
   - `zlink_service_monitor_handler_fn`
   - `zlink_service_monitor_event_t`
   - `zlink_service_monitor_event_detail_mask_t`
   - `zlink_service_monitor_open_options_t`

2. `zlink_enum.h`에서 아래 상수가 제거되었는지 확인한다.
   - `ZLINK_SERVICE_MONITOR_EVENT_*`
   - `ZLINK_DISCOVERY_MONITOR_EVENT_*`
   - `ZLINK_DISCOVERY_SERVICE_*`
   - `ZLINK_MONITOR_TARGET_DISCOVERY`
   - `ZLINK_MONITOR_TARGET_SPOT`
   - `ZLINK_MONITOR_TARGET_SPOT_NODE`

3. raw socket monitor는 그대로 동작하는지 확인한다.
   - `zlink_socket_monitor_open()` 정상 동작
   - `ZLINK_MONITOR_EVENT_ERROR` 사용 가능
   - `ZLINK_MONITOR_EVENT_CLOSED` 사용 가능
   - `ZLINK_MONITOR_TARGET_SOCKET` snapshot/close 경로 정상

4. `Discovery`, `SpotNode`, `Registry`, `Spot` 관련 예제와 테스트가 service
   monitor API를 더 이상 참조하지 않는지 확인한다.

5. 기존 service monitor 구현 파일이 남아 있더라도 공개 헤더와 공개 문서에서는
   접근할 수 없도록 정리되었는지 확인한다.

## 9. 정식 문서 반영 시 정리할 항목

구현이 끝나면 아래 문서에서 service monitor 관련 문구를 제거하거나
snapshot/query 중심 설명으로 바꿔야 한다.

- `doc/spec/core/monitoring.ko.md`
- `doc/spec/core/service/discovery.ko.md`
- `doc/spec/core/service/spot.ko.md`
- `doc/guide/06-monitoring.ko.md`
- `doc/guide/07-1-discovery.ko.md`
- `doc/guide/07-3-spot.ko.md`

정리 방향은 단순하다.

- raw socket만 monitor를 사용한다.
- `Discovery`, `SpotNode`, `Registry`, `Spot`은 monitor가 아니라 현재 상태
  조회와 query를 사용한다.
