# Gateway 삭제 / Metadata 후속 작업 Execution Guide

> 상태: active
> 대상 범위: `core/`, `core/tests/`, `core/perf/`, `doc/plan/service/gateway/`
> 목적: `gateway` 삭제와 metadata 후속 작업을 랄프 루프에서 중단 없이 순서대로 끝내기 위한 실행 기준 고정

## 1. 문서 목적

이 문서는 `gateway` 관련 작업의 상위 authority이자 실행 문서다.
즉 별도 master plan 없이 이 문서가 작업 순서, 종료 조건, 실행 체크리스트를 함께 고정한다.
새 설계를 제안하지 않는다.

설계 authority는 아래 문서들로 고정한다.

- [`gateway-removal-plan.ko.md`](./gateway-removal-plan.ko.md)
- [`socket-metadata-sharing-plan.ko.md`](./socket-metadata-sharing-plan.ko.md)
- [`README.ko.md`](./README.ko.md)

설계 판단이 흔들리면 먼저 authority 문서를 고치고 그 다음 코드를 수정한다.

## 2. 실행 authority

단일 상위 실행 authority:

- [`gateway-removal-metadata-execution-guide.ko.md`](./gateway-removal-metadata-execution-guide.ko.md)

상세 authority:

- [`gateway-removal-plan.ko.md`](./gateway-removal-plan.ko.md)
- [`socket-metadata-sharing-plan.ko.md`](./socket-metadata-sharing-plan.ko.md)

자동 실행 관계:

- 수동 실행 기준 문서는 이 guide와 상세 authority 문서들이다.
- 자동 실행이 필요하면 [`run_gateway_removal_metadata_execution.sh`](./run_gateway_removal_metadata_execution.sh)를 사용한다.
- 이 스크립트는 내부적으로 공통 supervisor인
  [`core/tools/run_codex_execution_guide_loop.sh`](../../../core/tools/run_codex_execution_guide_loop.sh)
  를 호출한다.

## 2.1 고정 작업 순서

1. `gateway` 삭제 범위와 migration 공백을 먼저 확정한다.
2. `gateway` public/internal/protocol/test/core-perf/doc/bindings 잔여물을 제거한다.
3. 삭제 직후 POSD 관점으로 관련 코드를 한 번 리팩토링한다.
4. 삭제 후에도 필요한 `value` / `metadata` / member query contract가 실제로 남는지 판정한다.
5. 실제 공백이 남을 때만 metadata 작업을 진행한다.
6. metadata 작업 완료 후 POSD 관점으로 다시 한 번 리팩토링한다.
7. 문서, 검증, 종료 판정을 정리한다.

기본값은 `gateway` 삭제로 끝내는 것이다.
metadata infra를 먼저 만들고 `gateway` 삭제를 나중에 결정하는 흐름으로 되돌리지 않는다.

## 2.2 Definition of Done

아래를 모두 만족해야 이번 묶음 작업이 끝난다.

- `gateway` family symbol과 전용 구현이 `core/`, `core/tests/`, `core/perf/`에서 제거된다.
- 삭제 직후 관련 discovery/registry/service/perf 코드에 대해 POSD 리팩토링이 반영된다.
- metadata/member query contract는 삭제 후 실제 공백이 남을 때만 구현된다.
- metadata 작업을 진행했다면 완료 후 관련 코드에 대해 두 번째 POSD 리팩토링이 반영된다.
- execution guide와 세부 plan 문서 상태가 실제 코드 상태와 일치한다.

## 2.3 단계 매핑

| 실행 가이드 | authority 문서 | 의미 |
| --- | --- | --- |
| `5.1 authority / preflight 정리` | execution guide 2.1, removal plan 9 | 순서, 범위, migration 공백, 검증 baseline 고정 |
| `5.2 gateway 제거 구현` | removal plan 9 Phase 2, 11, 12 | core/core-tests/core-perf 기준 `gateway` 제거 |
| `5.3 삭제 직후 POSD 리팩토링` | removal plan 9 Phase 5 | 삭제 후 discovery/registry/service/perf 단순화 |
| `5.4 metadata 착수 판정` | removal plan 9 Phase 3, metadata plan 6.1 | 실제 공백이 남는지 판정하고 metadata 착수 여부 결정 |
| `5.5 metadata 모델 / plumbing` | metadata plan 7, 8, 9 Phase 1~3 | `value` / `metadata` / member row 모델과 internal plumbing |
| `5.6 metadata query / consumer 연결` | metadata plan 7, 9 Phase 4~5 | query surface와 consumer 연결 |
| `5.7 metadata 완료 후 POSD 리팩토링` | metadata plan 9 Phase 6 | metadata 도입 후 구조 단순화 |
| `5.8 문서 / 최종 검증` | removal plan 12, metadata plan 10~12 | 문서와 종료 판정 정렬 |

## 3. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- authority 문서만으로는 해결할 수 없는 C API/ABI 계약 충돌
- 사용자 변경과 직접 충돌하는 워크트리 변경 발견
- `core/`, `core/tests/`, `core/perf/`, `doc/plan/service/gateway/`만으로 해결 불가능한 blocker

위 경우가 아니면:

1. 첫 미완료 항목을 잡는다.
2. 코드 변경과 회귀 검증을 같이 수행한다.
3. 관련 문서를 현재 상태에 맞게 갱신한다.
4. 해당 단계 범위만 묶어 commit 한다.
5. push 한다.
6. guide 상태를 갱신한다.
7. 다음 미완료 항목으로 넘어간다.

단계 완료 후 commit / push 없이 다음 단계로 넘어가지 않는다.

## 4. 기본 실행 명령

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1

./core/tests/run_test_lanes.sh --include-e2e
```

삭제 잔여물 확인 명령:

```bash
rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" \
  core core/tests core/perf doc/plan/service/gateway
```

필수 git 명령:

```bash
git status --short
git add <관련 파일들>
git commit -m "<단계 목적을 드러내는 메시지>"
git push
```

## 5. 남은 작업 체크리스트

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 5.1 authority / preflight 정리

상태: `미착수`

작업:

- execution guide와 상세 plan 사이의 순서/범위 불일치를 먼저 정리
- `gateway` 삭제 대상과 metadata 후속 대상이 충돌 없이 분리됐는지 확인
- `core/tests`, `core/perf` 기준의 삭제 잔여물 검색 baseline을 남긴다

완료 기준:

- 어떤 작업을 먼저 하고 무엇을 나중에 하는지 문서로 더 이상 흔들리지 않는다
- `gateway` 삭제와 metadata 후속 작업의 경계가 설명 가능하다

검증:

- `rg -n "zlink_gateway_|ZLINK_GATEWAY_|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf`

### 5.2 gateway 제거 구현

상태: `미착수`

작업:

- `gateway-removal-plan.ko.md`의 삭제 범위에 맞춰 `core/`, `core/tests/`, `core/perf/`에서 `gateway` 제거
- public/internal/protocol/test/perf 잔여물 제거
- 필요하면 migration 문서/메모를 authority 문서에 반영

완료 기준:

- `gateway-removal-plan.ko.md`의 제거 완료 판정과 최종 검증 항목을 만족한다

검증:

- `cmake --build core/build -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -L integration -j1`
- `rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf`

### 5.3 삭제 직후 POSD 리팩토링

상태: `미착수`

작업:

- `gateway` 제거 뒤 남은 discovery/registry/service/perf 코드에서 hidden coupling 정리
- shallow wrapper, 임시 adapter, 이름만 generic한 helper 제거 또는 통합
- 삭제 plan 문서의 POSD 단계와 검증 메모를 갱신

완료 기준:

- 삭제 후 관련 코드가 POSD 기준으로 더 짧게 설명 가능하다
- `gateway` 제거 때문에 남겨 둔 우회 경로가 사라진다

검증:

- `cmake --build core/build -j"$(nproc)"`
- `./core/tests/run_test_lanes.sh --include-e2e`

### 5.4 metadata 착수 판정

상태: `미착수`

작업:

- `gateway-removal-plan.ko.md`의 Phase 3 기준으로 삭제 후 실제 공백을 다시 판정
- migration guide만으로 끝낼 수 있는지 먼저 결정
- 실제 공백이 남는 경우에만 metadata 작업을 다음 단계로 연다

완료 기준:

- metadata 작업이 정말 필요한지 여부가 문서와 코드 기준으로 설명 가능하다
- 불필요하면 여기서 metadata 단계를 종료하고 바로 `5.8`로 넘어갈 수 있다

검증:

- `rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf`
- 관련 migration 메모 또는 authority 문서 상태 갱신

### 5.5 metadata 모델 / plumbing

상태: `미착수`

작업:

- `5.4`에서 실제 공백이 남는다고 판정된 경우에만
  `socket-metadata-sharing-plan.ko.md`의 Phase 1~3 진행
- `value`, `metadata`, member peer row, ownership, size limit, internal propagation path 구현
- 삭제 이후 공백을 메우는 최소 surface만 유지

완료 기준:

- metadata 모델과 internal plumbing이 authority 문서와 맞는다
- `gateway` 구조를 generic 이름으로 옮겨 적지 않는다

검증:

- `cmake --build core/build -j"$(nproc)"`
- 관련 unittest / integration 회귀

### 5.6 metadata query / consumer 연결

상태: `미착수`

작업:

- metadata plan의 query surface와 policy consumer 연결 진행
- metadata 작업을 실제로 시작한 경우에만 진행
- member peer query와 topology/introspection query 경계를 유지
- 필요하면 `core/tests` 회귀 추가

완료 기준:

- query surface가 동작하고 consumer가 이를 사용한다
- 운영 query와 정책 query의 역할이 다시 섞이지 않는다

검증:

- `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -L integration -j1`

### 5.7 metadata 완료 후 POSD 리팩토링

상태: `미착수`

작업:

- metadata 작업 완료 뒤 registry/discovery/service 코드를 다시 POSD 기준으로 정리
- 중복 query helper, compatibility adapter, shallow wrapper 제거 또는 통합
- metadata plan의 POSD 단계와 리스크/열린 질문 상태 갱신

완료 기준:

- metadata 도입 뒤에도 구조가 시간 순서가 아니라 추상 경계 기준으로 설명된다
- generic query surface가 새 허브나 shallow wrapper를 만들지 않는다

검증:

- `cmake --build core/build -j"$(nproc)"`
- `./core/tests/run_test_lanes.sh --include-e2e`

### 5.8 문서 / 최종 검증

상태: `미착수`

작업:

- README, execution guide, 두 상세 plan의 상태와 완료 판정 정리
- 필요하면 `core/perf` 및 테스트 문서의 잔여 gateway 언급 정리
- 종료 증거와 commit hash를 문서에 남길 수 있으면 남긴다

완료 기준:

- execution guide 기준으로 더 이상 미적용 사항이 없다

검증:

- `cmake --build core/build -j"$(nproc)"`
- `./core/tests/run_test_lanes.sh --include-e2e`
- `rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf doc/plan/service/gateway`

## 6. 랄프 루프 종료 계약

아래 세 문장 외의 변형을 쓰지 않는다.

- 모든 미적용 사항이 끝났을 때만 정확히 `미적용 사항이 없습니다.` 출력
- 사용자 결정이 꼭 필요할 때만 정확히 `사용자 입력 필요: <한 줄 이유>` 출력
- 그 외에는 정확히 `계속 진행 필요` 출력
