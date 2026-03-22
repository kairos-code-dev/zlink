# `core` POSD 리팩토링 문서 맵

이 디렉터리는 `core` 전체 구조를 POSD와 성능 비퇴행 기준으로
리팩토링하기 위한 문서 세트를 모아 둔 곳이다.

## 문서 목록 및 읽기 순서

| # | 파일 | 역할 |
| --- | --- | --- |
| 00 | [00-core-system-posd-refactor-plan.ko.md](00-core-system-posd-refactor-plan.ko.md) | 상위 방향 및 전체 phase 계획 |
| 01 | [01-core-system-phase0-baseline.ko.md](01-core-system-phase0-baseline.ko.md) | 기능/성능 기준선 고정 |
| 02 | [02-core-system-phase1-ownership-map.ko.md](02-core-system-phase1-ownership-map.ko.md) | ownership 원칙 |
| 03 | [03-core-system-phase1-resource-inventory.ko.md](03-core-system-phase1-resource-inventory.ko.md) | ownership 실제 자원 표 |
| 04 | [04-core-system-phase2-socket-runtime-split.ko.md](04-core-system-phase2-socket-runtime-split.ko.md) | socket/runtime 분리 설계 |
| 05 | [05-core-system-phase3-engine-transport-service-plan.ko.md](05-core-system-phase3-engine-transport-service-plan.ko.md) | engine/transport/service 통합 재구성 (상위 Phase 3~5) |
| 06 | [06-core-system-review-log.ko.md](06-core-system-review-log.ko.md) | 리뷰 이력 |
| 07 | [07-core-system-target-source-layout.ko.md](07-core-system-target-source-layout.ko.md) | 최종 소스 디렉터리 구조와 책임 초안 |

## 산출물 원칙

이 문서 세트는 아래 원칙을 따른다.

- 디렉터리 이동보다 ownership 정리가 먼저다.
- 성능 회귀 없는 구조 단순화만 허용한다.
- helper 분해보다 deep module 형성이 우선이다.
- phase별 기능/성능 게이트 없이 완료 판정을 하지 않는다.

## 핵심 결정 문장

- service runtime은 lifecycle coordinator다.
- socket runtime은 concrete close owner다.
- reaper는 finalization executor다.
- `asio_engine_t`는 facade가 되어야 한다.
