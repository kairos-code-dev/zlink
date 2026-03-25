# Discovery / Registry 서비스 자동 연결 실행 가이드

> 상태: active
> 기준 문서: `doc/plan/discovery/raw-socket-service-discovery-spec.ko.md`
> 대상 범위: `core/`, `core/tests/`, `doc/plan/discovery/`
> 목적: discovery-owned service model 전환과 registry/discovery/raw socket/gateway/spot 확장을 중단 없이 끝까지 밀기 위한 실행 순서 고정

## 1. 문서 목적

이 문서는 메인 스펙 문서의 구현 내용을
실제 코드 변경 순서와 완료 판정 기준으로 고정하는 실행 문서다.

이 문서는 새 설계를 제안하지 않는다.
설계 authority는 아래 메인 스펙 문서 하나로 고정한다.

- [`raw-socket-service-discovery-spec.ko.md`](./raw-socket-service-discovery-spec.ko.md)
  - 목적 / 요구사항 / 문제 정의:
    [`1. 목적`](./raw-socket-service-discovery-spec.ko.md#1-목적),
    [`2. 요구사항 정리`](./raw-socket-service-discovery-spec.ko.md#2-요구사항-정리),
    [`3. 문제 정의`](./raw-socket-service-discovery-spec.ko.md#3-문제-정의)
  - 설계 원칙 / 개념 모델:
    [`4. 설계 원칙`](./raw-socket-service-discovery-spec.ko.md#4-설계-원칙),
    [`5. 개념 모델`](./raw-socket-service-discovery-spec.ko.md#5-개념-모델)
  - API / 모델 / 런타임:
    [`6. Public Surface 제안`](./raw-socket-service-discovery-spec.ko.md#6-public-surface-제안),
    [`7. Family / Role 모델`](./raw-socket-service-discovery-spec.ko.md#7-family--role-모델),
    [`8. Registry / Discovery 프로토콜 확장`](./raw-socket-service-discovery-spec.ko.md#8-registry--discovery-프로토콜-확장),
    [`9. Registry 저장 모델`](./raw-socket-service-discovery-spec.ko.md#9-registry-저장-모델),
    [`10. Discovery 저장 모델`](./raw-socket-service-discovery-spec.ko.md#10-discovery-저장-모델),
    [`11. Socket Attachment 설계`](./raw-socket-service-discovery-spec.ko.md#11-socket-attachment-설계),
    [`12. Runtime 동작 상세`](./raw-socket-service-discovery-spec.ko.md#12-runtime-동작-상세),
    [`13. 에러 정책`](./raw-socket-service-discovery-spec.ko.md#13-에러-정책)
  - 검증 / 실행 / 완료 기준:
    [`14. 테스트 계획`](./raw-socket-service-discovery-spec.ko.md#14-테스트-계획),
    [`15. 구현 단계 제안`](./raw-socket-service-discovery-spec.ko.md#15-구현-단계-제안),
    [`16. 구현 매핑`](./raw-socket-service-discovery-spec.ko.md#16-구현-매핑),
    [`17. 구현 체크리스트`](./raw-socket-service-discovery-spec.ko.md#17-구현-체크리스트),
    [`18. Definition of Done`](./raw-socket-service-discovery-spec.ko.md#18-definition-of-done)

실행 중 설계 판단이 필요해 보이면
먼저 메인 스펙을 갱신하고 그 다음 코드를 수정한다.
코드와 실행 가이드만 바꿔서 설계 불일치를 남기지 않는다.

## 2. 실행 authority

단일 설계 authority:

- [`raw-socket-service-discovery-spec.ko.md`](./raw-socket-service-discovery-spec.ko.md)

이 가이드는 아래 내용을 메인 스펙에서 그대로 따른다.

- service ownership은 discovery가 가진다
- raw `ROUTER/DEALER/PUB/SUB`, `gateway`, `spot_node`를 discovery attach 대상으로 함께 전환한다
- `spot`은 `spot_node` 내부 구성으로 정렬하고 독립 attach 대상은 두지 않는다
- registry provider identity는 `service_name + socket_role + endpoint` 기준이다
- registry query surface는 raw role까지 조회 가능해야 한다
- attach된 service participant의 종료는 discovery destroy가 담당한다
- 구현 후 회귀 테스트와 POSD 리팩토링까지 이번 작업 범위에 포함한다

자동 실행 관계:

- 수동 실행 기준 문서는 이 guide와 메인 스펙이다.
- 자동 실행이 필요하면 [`run_discovery_service_execution.sh`](./run_discovery_service_execution.sh)를 사용한다.
- 이 스크립트는 내부적으로 공통 supervisor인
  [`core/tools/run_codex_execution_guide_loop.sh`](../../../core/tools/run_codex_execution_guide_loop.sh)
  를 호출한다.
- 공통 supervisor는 guide / master plan / logs / gate label만 주입받는 제너릭 루프이고,
  discovery 전용 정책은 이 guide와 메인 스펙이 결정한다.

배포 경고:

- 이번 변경은 wire compatibility를 유지하지 않는다.
- `register`, `unregister`, `heartbeat`, `service-list`, `sync` payload가 role-aware로 바뀐다.
- 따라서 구버전 registry / discovery / attached socket과의 rolling upgrade는 지원하지 않는다.
- 운영 환경 적용 시 registry, discovery, service participant를 한 번에 일괄 배포해야 한다.

## 2.1 단계 매핑

아래 표는 스펙의 `Phase`, 스펙의 `Step`, 실행 가이드의 `5.x`를 1:1로 대응시킨다.

| 실행 가이드 | 메인 스펙 Phase | 메인 스펙 Step | 의미 |
| --- | --- | --- | --- |
| `5.1 protocol / enum 정리` | `15.2 Phase 2` | `17.1 Step A` | family / role / enum / validation 도입 |
| `5.2 discovery ownership 전환` | `15.1 Phase 1` | `17.2 Step B` | discovery를 single service view로 고정 |
| `5.3 registry wire / state / query 확장` | `15.2 Phase 2` | `17.3 Step C` | role-aware wire, state, query 도입 |
| `5.4 raw socket attach 도입` | `15.3 Phase 3` | `17.4 Step D`, `17.5 Step E` | raw attach와 bind/register/refresh 연결 |
| `5.5 gateway / spot / spot_node ownership 전환` | `15.4 Phase 4` | `17.6 Step F` | constructor ownership 제거와 attach 정렬 |
| `5.6 회귀 테스트 작성 및 유지` | `15.5 Phase 5` | `17.8 Step H` 일부 | 기능 추가 후 회귀 보호선 보강 |
| `5.7 POSD 리팩토링 정리` | `15.6 Phase 6` | `17.9 Step I` | ownership, lifecycle, helper 공통화 |
| `5.8 문서 / 주석 정리` | `15.5 Phase 5` | `17.8 Step H` 일부 | 문서, 주석, 종료 증거 정렬 |

## 3. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- 메인 스펙만으로는 해결할 수 없는 C API/ABI 계약 충돌
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견
- `core/`와 `core/tests/`만으로 해결 불가능한 blocker

위 경우가 아니면:

1. 첫 미완료 항목을 잡는다.
2. 코드 수정과 회귀 테스트 추가를 같이 한다.
3. 관련 검증을 끝낸다.
4. 관련 변경만 묶어서 commit 한다.
5. 원격에 push 한다.
6. guide 체크 상태를 갱신한다.
7. 다음 미완료 항목으로 바로 넘어간다.

단계 완료 후에는 반드시 commit / push까지 끝낸다.
로컬 수정만 남겨둔 채 다음 단계로 넘어가지 않는다.

commit / push 규칙:

- 한 단계의 코드, 테스트, 문서, 주석 변경은 하나의 논리 단위 commit으로 묶는다
- commit 전에 해당 단계의 완료 기준 검증을 통과시킨다
- commit 메시지는 단계와 의도를 드러내야 한다
- push는 단계 commit 직후 바로 수행한다
- 다음 단계는 직전 단계 commit / push가 끝난 뒤에만 시작한다

권장 commit 메시지 예시:

- `refactor: convert discovery ownership surface`
- `feat: add raw socket discovery attachment`
- `refactor: align gateway and spot with discovery-owned services`
- `test: add discovery service regression coverage`
- `refactor: apply posd cleanup to discovery service flow`

## 4. 기본 실행 명령

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1

./core/tests/run_test_lanes.sh --include-e2e
```

대표 discovery/service 회귀 명령:

```bash
ctest --test-dir core/build --output-on-failure -R \
'^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection)$'
```

단계 완료 후 필수 git 명령:

```bash
git status --short
git add <관련 파일들>
git commit -m "<단계 목적을 드러내는 메시지>"
git push
```

배포 / 검증 주의:

- 이 작업은 rolling upgrade를 지원하지 않으므로, 단계별 로컬 검증과 commit / push는 가능해도 운영 반영은 일괄 배포 단위로 계획해야 한다.
- 단계별 push는 구현 이력 관리 목적이고, 혼합 버전 클러스터 운영 허용을 의미하지 않는다.

## 5. 남은 작업 체크리스트

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 5.1 protocol / enum 정리

스펙 참조:

- [`7.1 discovery family 확장`](./raw-socket-service-discovery-spec.ko.md#71-discovery-family-확장)
- [`7.2 role enum`](./raw-socket-service-discovery-spec.ko.md#72-role-enum)
- [`7.3 family-role 유효 조합`](./raw-socket-service-discovery-spec.ko.md#73-family-role-유효-조합)
- [`8.1 register 계열 payload`](./raw-socket-service-discovery-spec.ko.md#81-register-계열-payload)
- [`17.1 Step A: protocol / enum 정리`](./raw-socket-service-discovery-spec.ko.md#171-step-a-protocol--enum-정리)

상태: `미착수`

작업:

- `zlink_service_type_t`에 `ZLINK_SERVICE_TYPE_SOCKET` 추가
- internal raw socket family 추가
- `service_role_t`와 family-role validation helper 추가

완료 기준:

- protocol layer가 raw socket family와 role을 이해한다

### 5.2 discovery ownership 전환

스펙 참조:

- [`4.1 discovery는 service view다`](./raw-socket-service-discovery-spec.ko.md#41-discovery는-service-view다)
- [`4.2 service 선택 책임은 discovery가 가진다`](./raw-socket-service-discovery-spec.ko.md#42-service-선택-책임은-discovery가-가진다)
- [`5.1 service view`](./raw-socket-service-discovery-spec.ko.md#51-service-view)
- [`6.1 discovery 생성 시 service_name 고정`](./raw-socket-service-discovery-spec.ko.md#61-discovery-생성-시-service_name-고정)
- [`10.1 discovery state`](./raw-socket-service-discovery-spec.ko.md#101-discovery-state)
- [`13.4 discovery destroy 실패`](./raw-socket-service-discovery-spec.ko.md#134-discovery-destroy-실패)
- [`17.2 Step B: discovery surface ownership 전환`](./raw-socket-service-discovery-spec.ko.md#172-step-b-discovery-surface-ownership-전환)

상태: `미착수`

작업:

- `zlink_discovery_new(ctx, service_type, service_name)`로 전환
- discovery state를 single service view 기준으로 재정의
- destroy cascade shutdown semantics 정리

완료 기준:

- discovery 하나가 정확히 하나의 service view만 대표한다
- discovery destroy가 attach된 service participant 종료 경로를 소유한다

### 5.3 registry wire / state / query 확장

스펙 참조:

- [`8.1 register 계열 payload`](./raw-socket-service-discovery-spec.ko.md#81-register-계열-payload)
- [`8.2 service list shape`](./raw-socket-service-discovery-spec.ko.md#82-service-list-shape)
- [`9.1 service view key`](./raw-socket-service-discovery-spec.ko.md#91-service-view-key)
- [`9.2 provider key`](./raw-socket-service-discovery-spec.ko.md#92-provider-key)
- [`9.3 provider entry`](./raw-socket-service-discovery-spec.ko.md#93-provider-entry)
- [`9.4 registry 조회 surface 확장`](./raw-socket-service-discovery-spec.ko.md#94-registry-조회-surface-확장)
- [`17.3 Step C: registry wire / state 확장`](./raw-socket-service-discovery-spec.ko.md#173-step-c-registry-wire--state-확장)

상태: `미착수`

작업:

- register/unregister/heartbeat/service-list/sync에 role 추가
- registry key와 merge/expire 로직 role-aware 변경
- topology/service_summary query에 raw role 노출

완료 기준:

- registry query에서 같은 서비스 안의 다중 role provider를 식별할 수 있다

### 5.4 raw socket attach 도입

스펙 참조:

- [`5.3 socket attachment`](./raw-socket-service-discovery-spec.ko.md#53-socket-attachment)
- [`6.2 raw socket attach API`](./raw-socket-service-discovery-spec.ko.md#62-raw-socket-attach-api)
- [`11.1 local attachment state`](./raw-socket-service-discovery-spec.ko.md#111-local-attachment-state)
- [`11.2 attach lifecycle`](./raw-socket-service-discovery-spec.ko.md#112-attach-lifecycle)
- [`11.3 bind와 register 관계`](./raw-socket-service-discovery-spec.ko.md#113-bind와-register-관계)
- [`11.4 attach validation`](./raw-socket-service-discovery-spec.ko.md#114-attach-validation)
- [`12.2 peer filtering 절차`](./raw-socket-service-discovery-spec.ko.md#122-peer-filtering-절차)
- [`12.3 duplicate connect 방지`](./raw-socket-service-discovery-spec.ko.md#123-duplicate-connect-방지)
- [`13.2 attach 실패`](./raw-socket-service-discovery-spec.ko.md#132-attach-실패)
- [`13.5 attach 상태 API 금지 규칙`](./raw-socket-service-discovery-spec.ko.md#135-attach-상태-api-금지-규칙)
- [`17.4 Step D: raw socket attachment 도입`](./raw-socket-service-discovery-spec.ko.md#174-step-d-raw-socket-attachment-도입)
- [`17.5 Step E: bind / register / peer refresh 연결`](./raw-socket-service-discovery-spec.ko.md#175-step-e-bind--register--peer-refresh-연결)

상태: `미착수`

작업:

- `zlink_socket_attach_discovery()` 추가
- bind/register/refresh/disconnect lifecycle 연결
- attach 상태 `connect`/`disconnect`/`unbind`/개별 `close` gate 구현

완료 기준:

- raw socket이 discovery service view participant로 동작한다

### 5.5 gateway / spot / spot_node ownership 전환

스펙 참조:

- [`6.3 gateway / spot에도 같은 철학 적용`](./raw-socket-service-discovery-spec.ko.md#63-gateway--spot에도-같은-철학-적용)
- [`15.4 Phase 4: gateway / spot 정렬`](./raw-socket-service-discovery-spec.ko.md#154-phase-4-gateway--spot-정렬)
- [`16.5 gateway / spot 영향 범위`](./raw-socket-service-discovery-spec.ko.md#165-gateway--spot-영향-범위)
- [`17.6 Step F: gateway / spot ownership 전환`](./raw-socket-service-discovery-spec.ko.md#176-step-f-gateway--spot-ownership-전환)

상태: `미착수`

작업:

- `zlink_gateway_new(ctx)`로 전환
- `zlink_spot_new(ctx)`로 전환
- `zlink_spot_node_new(ctx)`로 전환
- `gateway`, `spot_node`의 service selection이 constructor가 아니라 discovery attach에서 결정되도록 정렬
- `spot`은 `spot_node` 내부 구성으로 두고 독립 attach surface를 추가하지 않음
- attach된 participant는 discovery destroy로 종료되도록 정렬

완료 기준:

- `gateway`, `spot`, `spot_node`도 discovery attach 없이는 service에 속하지 않는다
- discovery destroy가 attach된 `gateway`, `spot`, `spot_node` 종료까지 담당한다

### 5.6 회귀 테스트 작성 및 유지

스펙 참조:

- [`14.1 protocol 단위 테스트`](./raw-socket-service-discovery-spec.ko.md#141-protocol-단위-테스트)
- [`14.2 discovery 단위 테스트`](./raw-socket-service-discovery-spec.ko.md#142-discovery-단위-테스트)
- [`14.3 raw socket integration 테스트`](./raw-socket-service-discovery-spec.ko.md#143-raw-socket-integration-테스트)
- [`14.4 회귀 테스트`](./raw-socket-service-discovery-spec.ko.md#144-회귀-테스트)
- [`14.5 신규 회귀 테스트 작성 원칙`](./raw-socket-service-discovery-spec.ko.md#145-신규-회귀-테스트-작성-원칙)
- [`16.6 테스트`](./raw-socket-service-discovery-spec.ko.md#166-테스트)
- [`17.8 Step H: regression / cleanup`](./raw-socket-service-discovery-spec.ko.md#178-step-h-regression--cleanup)

상태: `미착수`

작업:

- 기존 `gateway`/`spot`/registry/manual raw 경로 회귀 테스트 추가 또는 보강
- 새 기능 검증 테스트와 별도로 regression 목적 테스트를 명시적으로 남김

완료 기준:

- 기존 계약이 테스트로 보호된다

### 5.7 POSD 리팩토링 정리

스펙 참조:

- [`3.2 service ownership을 discovery로 올려야 하는 이유`](./raw-socket-service-discovery-spec.ko.md#32-service-ownership을-discovery로-올려야-하는-이유)
- [`4.3 role 매칭 정책은 중앙화한다`](./raw-socket-service-discovery-spec.ko.md#43-role-매칭-정책은-중앙화한다)
- [`4.5 attach된 자동 연결 ownership은 단일해야 한다`](./raw-socket-service-discovery-spec.ko.md#45-attach된-자동-연결-ownership은-단일해야-한다)
- [`15.6 Phase 6: POSD 리팩토링 정리`](./raw-socket-service-discovery-spec.ko.md#156-phase-6-posd-리팩토링-정리)
- [`17.9 Step I: POSD 리팩토링`](./raw-socket-service-discovery-spec.ko.md#179-step-i-posd-리팩토링)

상태: `미착수`

작업:

- service ownership 관련 중복 상태 제거
- attach/register/peer refresh 정책 공통화
- gateway/spot/raw socket 사이 얕은 래퍼와 반복 조건문 축소

완료 기준:

- 새 모델의 핵심 정책이 공통 코드에 모여 있고 변경 증폭이 줄어든다

### 5.8 문서 / 주석 정리

스펙 참조:

- [`6. Public Surface 제안`](./raw-socket-service-discovery-spec.ko.md#6-public-surface-제안)
- [`13. 에러 정책`](./raw-socket-service-discovery-spec.ko.md#13-에러-정책)
- [`15.5 Phase 5: docs / tests 정리`](./raw-socket-service-discovery-spec.ko.md#155-phase-5-docs--tests-정리)
- [`18. Definition of Done`](./raw-socket-service-discovery-spec.ko.md#18-definition-of-done)

상태: `미착수`

작업:

- public API 주석 갱신
- 메인 스펙과 guide 상태 동기화

완료 기준:

- 코드/가이드/메인 스펙 설명이 서로 충돌하지 않는다

## 6. 종료 판정

아래를 모두 만족할 때만 종료한다.

종료 판정 스펙 참조:

- [`14. 테스트 계획`](./raw-socket-service-discovery-spec.ko.md#14-테스트-계획)
- [`17. 구현 체크리스트`](./raw-socket-service-discovery-spec.ko.md#17-구현-체크리스트)
- [`18. Definition of Done`](./raw-socket-service-discovery-spec.ko.md#18-definition-of-done)

- guide의 5.1~5.8이 전부 `완료`
- 메인 스펙의 Definition of Done을 코드와 테스트로 만족
- 회귀 테스트가 추가되어 기존 계약이 보호됨
- POSD 리팩토링 단계까지 반영됨
- 각 단계 완료 직후 commit / push 기록이 남아 있음

종료 시 정확히 아래 한 줄만 출력한다.

```text
미적용 사항이 없습니다.
```

사용자 결정 없이는 더 진행할 수 없는 blocker가 있을 때만 아래 형식을 쓴다.

```text
사용자 입력 필요: <한 줄 이유>
```

그 외에는 정확히 아래 한 줄만 출력한다.

```text
계속 진행 필요
```
