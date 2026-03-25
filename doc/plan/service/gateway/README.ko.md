# Gateway 후속 계획 문서

기존 `gateway 제거 및 socket metadata 공유` 논의를
상세 plan 2개와 execution guide / 실행 스크립트로 정리했다.
현재 작업 순서는 `gateway` 삭제 선행으로 고정한다.

## 권장 작업 순서

1. [`gateway-removal-plan.ko.md`](./gateway-removal-plan.ko.md)
   - `gateway` family를 먼저 source tree에서 제거하기 위한 실행 계획 문서
   - public/internal/protocol/test/doc/core-perf/bindings 정리 범위와 완료 판정 포함
2. [`socket-metadata-sharing-plan.ko.md`](./socket-metadata-sharing-plan.ko.md)
   - `gateway` 제거 이후에도 필요한 공통 `value` / `metadata` 공유 모델을 재도입하기 위한 후속 설계 문서
   - raw socket/service profile이 공통 metadata distribution을 소비하는 방향으로 정리
3. [`gateway-removal-metadata-execution-guide.ko.md`](./gateway-removal-metadata-execution-guide.ko.md)
   - 실제 작업 순서, 검증, commit/push 기준을 고정하는 execution guide
4. [`run_gateway_removal_metadata_execution.sh`](./run_gateway_removal_metadata_execution.sh)
   - 공통 supervisor를 감싸는 gateway 전용 랄프 루프 실행 스크립트

## 선행 결정

- 이번 묶음 작업에서는 `gateway` 유지 여부를 다시 열어두지 않는다.
- 먼저 `gateway` 삭제로 개념과 코드를 줄이고,
  남는 요구만 generic metadata/member query contract로 다시 설계한다.
- 따라서 metadata 공유 문서는 `gateway` 존치를 위한 준비 문서가 아니라
  삭제 이후 대체 contract를 정의하는 문서로 읽어야 한다.

## 자동 실행

- 랄프 루프로 작업을 밀고 싶으면
  [`run_gateway_removal_metadata_execution.sh`](./run_gateway_removal_metadata_execution.sh)를 사용한다.
- 자동 실행의 authority는
  [`gateway-removal-metadata-execution-guide.ko.md`](./gateway-removal-metadata-execution-guide.ko.md) 하나로 본다.
