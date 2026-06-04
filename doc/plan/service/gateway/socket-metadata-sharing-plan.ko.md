# Socket Metadata 공유 상세 계획

> 상태: completed
> 대상 범위: `core/`, `core/tests/`, `doc/plan/service/`, `doc/plan/discovery/`, `doc/plan/registry/`
> 목적: `gateway` 제거 이후에도 필요한 service peer별 `value`와 opaque `metadata` 공유 모델을 generic contract로 설계한다.

## 진행 상태

- Phase 1~6 구현이 `core/`와 `core/tests/`에 반영됐다.
- public C API는 `zlink_discovery_set/get_value`, `zlink_discovery_set/get_metadata`,
  `zlink_registry_member_peers`, `zlink_registry_member_peer_metadata`,
  `zlink_discovery_member_peers`, `zlink_discovery_member_peer_metadata`,
  `zlink_member_peer_entry_t`, `ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE` 기준으로 고정됐다.
- registry/discovery는 `value + metadata`를 canonical row/blob query로 배포하고
  discovery peer-view는 remote member attribute snapshot만 반환한다.
- topology refresh는 provider snapshot 경계를 유지하고
  member peer query는 정책/attribute query 경계로만 남겨
  local attached participant 제외 규칙과 연결 그래프 계산을 다시 섞지 않도록 정리했다.
- 검증: `./core/tests/run_test_lanes.sh --include-e2e`
- 검증: `./core/tools/run_execution_gate_loop.sh --label gateway_removal_metadata_gate --count 1`

## 0. 선행 조건

이 문서는 `gateway` 삭제 이후 후속 작업을 다룬다.
즉 이 계획은 `gateway` 존치를 전제로 공통화를 준비하는 문서가 아니다.

- 선행 작업: [`gateway-removal-plan.ko.md`](./gateway-removal-plan.ko.md)의 삭제 범위 실행
- 이 문서의 역할: 삭제 후에도 필요한 요구만 더 작은 metadata/member query surface로 재도입
- 비전제: `gateway` API/타입/프로토콜을 유지한 채 내부만 공통화하는 접근

## 1. 문제 정의

weighted routing 같은 후속 기능은
각 서비스 멤버의 endpoint 외에도 추가 속성을 알아야 한다.

현재 필요한 대표 속성은 아래 두 가지다.

- `value`
- opaque `metadata`

이 정보가 특정 family에 종속되면
`gateway` 같은 별도 abstraction이 계속 필요해질 수 있다.

반대로 registry/discovery 공통 기능으로 끌어올리면
각 family/profile은 같은 distribution layer를 공유할 수 있다.

## 2. 목표

- service peer별 `value`와 `metadata`를 공통 모델로 배포할 수 있어야 한다
- registry는 canonical source로서 이를 저장/조회할 수 있어야 한다
- discovery는 특정 service view의 cached peer row로 이를 전달해야 한다
- local runtime은 discovery peer view에서 다른 소켓의 `value`/`metadata`를 읽을 수 있어야 한다
- discovery peer view는 local attached participant를 제외한 remote peer 집합만 반환해야 한다
- member peer row는 routing policy consumer를 위한 최소 attribute surface로 유지해야 한다
- `gateway` 전용 용어 없이 raw socket/service profile이 같은 contract를 소비할 수 있어야 한다
- public API는 지금 단계에서 초안을 고정하되, ABI는 후속 단계에서 확정한다

## 2.1 이 문서가 채워야 하는 공백

`gateway`를 먼저 제거하면 아래 요구가 비게 된다.

- service peer별 numeric attribute 공유
- service peer별 opaque metadata 공유
- routing policy consumer가 사용할 최소 peer row 조회

이 문서는 위 요구만 다시 도입한다.
`gateway`가 갖고 있던 lifecycle, monitor, facade 의미까지 복원하는 것은 목표가 아니다.

## 3. 핵심 원칙

### 3.1 distribution path

metadata distribution path는 아래로 고정한다.

`local owner -> registry -> discovery -> local consumer`

### 3.2 ownership

- local 설정 ownership은 discovery가 가진다
- canonical 저장 ownership은 registry가 가진다
- local runtime consume는 discovery peer view를 통해 이뤄진다

### 3.3 identity와 attribute 분리

- service peer identity는 여전히
  `service_type + service_name + role + endpoint`
- `value`, `metadata`는 identity의 속성이다

### 3.4 runtime consume와 운영 관측 분리

- `zlink_member_peer_entry_t`는 routing policy consumer를 위한 최소 attribute row다
- `value`, `metadata` distribution과 직접 관련 없는 운영/관측 필드는 여기에 넣지 않는다
- topology state, connected timestamp, reported timestamp 같은 운영 정보는
  registry topology query 또는 별도 introspection surface가 담당한다
- 즉 member peer query는 "정책 소비용 attribute snapshot"이고,
  운영 query는 별도 surface로 분리한다

## 4. 데이터 모델

### 4.1 numeric value와 blob 분리

현재 우선안:

- `value`: `int64_t`
- `metadata`: opaque binary blob

이유:

- `value`는 metadata blob보다 가볍고 query-friendly한 simple typed channel이다
- `value`를 별도 field로 두면 common infra는 단순한 정수 하나를 빠르게 전달한다
- 그 값을 `weight`, `priority`, `bias` 등으로 해석할지는 consumer policy가 정하게 둘 수 있다
- 나머지 확장 정보는 opaque blob으로 두는 편이 registry/discovery 책임과 잘 맞는다

### 4.2 metadata size 방향

- metadata maximum size는 runtime-configurable contract로 둔다
- discovery local owner는 metadata set 시 현재 configured max size를 초과하면
  fail-fast로 실패해야 한다
- oversize는 truncate하지 않는다
- registry/discovery/query path는 같은 effective max-size contract를 따라야 한다
- 초기 기본값 예시는 `4 KiB`다

## 5. registry / discovery 역할

### 5.1 registry

- canonical service/peer read surface
- 각 service peer row에 `value`를 포함
- full metadata는 별도 blob 조회 API로 제공

### 5.2 discovery

- local attached participant의 `value`/`metadata` owner surface
- `(service_type, service_name)` service view의 cached peer read surface
- service view 기준 remote peer row 조회 제공
- full metadata는 peer-view 기준 별도 blob 조회 API 제공

## 6. 조회 모델

- 운영/관측: registry query
- runtime consume: discovery peer-view query

즉 remote에 있는 다른 소켓의 `value`와 `metadata`를 읽는 canonical 방법은
registry query 또는 discovery peer-view query다.

raw socket handle이 peer metadata를 직접 query하는 모델은 우선안에서 제외한다.

## 6.1 삭제 후 적용 순서

metadata/member query contract는 아래 순서로 도입한다.

1. `gateway` 삭제 후 남은 사용자 시나리오를 raw socket/service 기준으로 다시 적는다
2. migration guide만으로 해결되지 않는 시나리오만 API 후보로 남긴다
3. registry/discovery 공통 row와 blob query를 최소 surface로 정의한다
4. 이후 필요한 profile만 이 surface를 소비하게 연결한다

## 7. C API 초안

이 절의 시그니처는 토론용 초안이다.

### 7.1 discovery local 설정/조회 API

```c
int zlink_discovery_set_value(void *discovery_, int64_t value_);
int zlink_discovery_get_value(void *discovery_, int64_t *value_out_);

int zlink_discovery_set_metadata(void *discovery_,
                                 const void *data_,
                                 size_t size_);
int zlink_discovery_get_metadata(void *discovery_,
                                 zlink_msg_t *metadata_out_);
```

설계 메모:

- local attached participant는 discovery가 가진 `value`/`metadata`를 통해
  registry/discovery distribution path에 참여한다
- `value` 기본값은 현재 `0`이 가장 유력하다
- `metadata`가 없으면 길이 `0`의 blob으로 본다

### 7.2 registry peer 조회 확장 API

```c
typedef struct zlink_member_peer_entry_t
{
    zlink_service_type_t service_type;
    uint16_t service_role;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    int64_t value;
} zlink_member_peer_entry_t;

int zlink_registry_member_peers(
  void *registry_,
  zlink_service_type_t service_type_,
  const char *service_name_,
  zlink_member_peer_entry_t *entries_,
  size_t *count_inout_);

int zlink_registry_member_peer_metadata(
  void *registry_,
  zlink_service_type_t service_type_,
  const char *service_name_,
  uint16_t service_role_,
  const char *endpoint_,
  zlink_msg_t *metadata_out_);
```

설계 메모:

- 기존 서비스별 registry 조회에 `value`와 `routing_id`를 확장한다
- full metadata는 별도 `zlink_msg_t` 조회로 분리한다
- peer identity는
  `service_type + service_name + role + endpoint`다
- topology state, connected timestamp, reported timestamp 같은 운영 필드는
  이 row에 포함하지 않는다

### 7.3 discovery peer view 조회 API

```c
int zlink_discovery_member_peers(
  void *discovery_,
  zlink_member_peer_entry_t *entries_,
  size_t *count_inout_);

int zlink_discovery_member_peer_metadata(
  void *discovery_,
  uint16_t service_role_,
  const char *endpoint_,
  zlink_msg_t *metadata_out_);
```

설계 메모:

- registry/discovery는 같은 `zlink_member_peer_entry_t`를 공유한다
- 이 row는 discovery가 생성 시 고정된
  `(service_type, service_name)` view 안에서 현재 보고 있는 remote peer를 표현한다
- weighted routing local policy는 peer row에서 `value`를 읽고
  필요한 peer에 한해 metadata blob을 추가로 조회한다
- discovery peer view는 local attached participant를 제외한 remote peer만 반환한다
- `service_role_ + endpoint_`는 해당 discovery peer view 안에서 target peer를 식별하는 key다

### 7.4 metadata ownership contract

```c
/*
 * - metadata_out_의 ownership은 caller로 이동한다.
 * - 성공 시 caller는 zlink_msg_close()로 해제한다.
 * - 실패 시 metadata_out_은 닫힌 상태거나 caller가 그대로 재사용 가능해야 한다.
 */
```

### 7.5 metadata size contract

```c
/*
 * - metadata maximum size는 runtime-configurable이다.
 * - 현재 구현은 discovery handle에서
 *   zlink_set_option(discovery_, ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE, ...)
 *   로 조정한다.
 * - 설정된 maximum size를 초과하는 metadata set/update는 실패해야 한다.
 * - oversize 입력은 truncate하지 않는다.
 * - 실패 시 errno는 EMSGSIZE로 고정한다.
 */
```

## 8. runtime / lifecycle 원칙

- `value`는 connection graph와 독립된 service peer attribute다
- `metadata` blob은 registry/discovery가 의미를 해석하지 않는다
- metadata update는 local owner overwrite 기준으로 적용한다
- 길이 `0` metadata는 empty blob으로 본다
- discovery는 최신 snapshot으로 local runtime을 refresh한다
- 연결 대상 계산과 metadata 기반 정책 적용은 서로 다른 단계여야 한다

## 9. 구현 단계 제안

### Phase 1. 삭제 후 요구 재수집

- `gateway` 제거 후 실제로 남은 사용자 시나리오 재정리
- migration guide만으로 충분한 항목과 새 API가 필요한 항목 분리
- `value = int64_t`, `metadata = opaque blob` 가정 유지 여부 확인

### Phase 2. 모델 확정

- member peer row 최소 field 집합 확정
- metadata size limit 기본 정책 확정
- ownership/error contract 확정

### Phase 3. internal plumbing

- registry peer row 확장
- discovery peer view row 확장
- internal update/propagation path 구현

### Phase 4. query surface 확장

- 기존 registry 서비스 조회에 `value` 반영
- registry/discovery metadata blob 조회 API 추가
- discovery peer-view 조회 API 추가

### Phase 5. policy consumer 연결

- raw socket/service profile이 discovery peer-view를 consume하도록 연결
- 특정 policy consumer가 필요한 추가 query가 있는지 검증

### Phase 6. metadata 작업 완료 후 POSD 관점 리팩토링

- metadata/member query 도입 뒤 남은 registry/discovery/service 코드를 다시 훑어
  shallow wrapper, 중복 query helper, hidden coupling을 정리한다
- John Ousterhout의 POSD 기준으로
  metadata distribution 책임과 policy consume 책임의 경계를 더 깊고 작게 만든다
- `value`/`metadata` query가 topology/introspection 경로와 다시 얽히지 않도록
  adapter, 임시 branch, 우회 surface를 제거한다
- migration 과정에서 임시로 도입한 compatibility helper가
  deep module을 만들지 못하면 통합하거나 삭제한다
- 결과 구조가 시간 순서가 아니라 추상 경계 기준으로 설명되도록
  discovery, registry, consumer API를 다시 다듬는다

## 10. 테스트 방향

- registry peer metadata merge/update/expire
- discovery peer-view metadata propagation
- local set 이후 remote/peer-view 조회 일관성
- empty metadata set/get contract
- oversize metadata fail-fast (`EMSGSIZE`)
- metadata blob ownership contract
- weighted routing consumer 연계
- metadata 작업 완료 후 POSD 리팩토링으로도
  contract와 테스트 의미가 흐려지지 않는지 확인

## 11. 리스크

- 삭제 전에 이 문서의 API를 먼저 굳히면
  오히려 `gateway` 구조를 새 generic API에 옮겨 적을 위험이 있다
- metadata 구현만 끝내고 POSD 리팩토링을 생략하면
  generic 이름의 얕은 wrapper와 query 중복 경로가 남을 수 있다
- metadata를 너무 generic하게 만들면 public API가 흐려질 수 있다
- metadata size limit를 너무 엄격하게 두면 확장성이 떨어진다
- registry query와 discovery peer-view query의 역할 경계가 흐려질 수 있다

## 12. 열린 질문

- `value == 0` 기본 의미를 어떻게 둘 것인가
- 음수 `value`를 common infra에서 허용만 할지, 일부 제약을 둘 것인가
- metadata update를 heartbeat와 통합할 것인가, 별도 update path를 둘 것인가
- discovery peer-view 조회를 즉시 public으로 열 것인가
- metadata 작업 완료 후 남는 helper/adapter 중
  POSD 기준으로 제거 대상은 무엇인가
