# STREAM 성능개선 계획 문서 맵

이 디렉토리는 `STREAM` 소켓의 전반적인 small-message 성능을 개선하기 위한
계획 문서를 모아 둔 곳이다.

핵심 목적은 다음 두 가지다.

- 현재 `STREAM` data plane의 병목을 어떤 기준으로 판단했는지 고정한다.
- 구현자가 추가 결정을 하지 않고 바로 작업할 수 있는 개선 계획을 남긴다.

## 문서 목록 및 읽기 순서

| # | 파일 | 역할 |
| --- | --- | --- |
| 00 | [stream-socket-performance-improvement-plan.ko.md](stream-socket-performance-improvement-plan.ko.md) | `STREAM` 소켓 성능개선 기준선, 병목 판단, 구현 단계, 수용 기준 |

## 산출물 원칙

- 기준 빌드 디렉토리는 항상 `core/build/`다.
- 성능 평가는 `core/perf/`의 실제 benchmark 결과를 기준으로 한다.
- echo 전용 지름길이 아니라 `STREAM callback + send` 일반 경로 개선만 허용한다.
- route 자료구조 같은 표면적 튜닝보다 data plane ownership, handoff, flush 정책을 우선한다.

## 후속 문서 규칙

- 구현 계획은 이 디렉토리의 plan 문서에 추가한다.
- 실측 결과나 회귀 분석은 별도 report 문서로 분리한다.
- 구현이 완료된 뒤 결과를 정리할 때도 기준 benchmark 이름과 result 파일 경로를
  반드시 함께 남긴다.
