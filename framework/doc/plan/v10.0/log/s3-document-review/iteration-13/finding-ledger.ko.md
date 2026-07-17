# S3 iteration 13 — finding ledger

## 1. 리뷰 결과

| reviewer | 결과 | 채택 여부 |
|---|---|---|
| Codex agent | finding 9건, 시작·종료 202/202 hash 일치 | 채택 |
| Claude Sonnet | 실행 중 scope drift로 중단 | 무효. 다음 iteration에서 다시 실행 |

## 2. 수정 묶음

| ID | 포함 finding | 범위 | 상태 |
|---|---|---|---|
| S3-F13-A | C13-01·02·03·06·07·08 | Actor join commit, Spot generation, Redis transfer key, codec owner, transfer metric, codec mismatch 계약과 모든 언어 exact·fixture·inventory 정렬 | 검증 중 |
| S3-F13-B | C13-04·05·09 | Kotlin ToActor·Kotlin/Node transfer feature map과 Kotlin STREAM guide 정렬 | 검증 중 |

## 3. Finding

| ID | 축 | 심각도 | 위치 | 요약 | 수정 묶음 |
|---|---|---|---|---|---|
| C13-01 | 1차 소스 | high | `server/23-spot-actor.ko.md:35` | source leave가 location CAS보다 앞서 stale 실패의 무변경 계약과 충돌 | S3-F13-A |
| C13-02 | 1차 소스 | high | `server/40-location-runtime.ko.md:71` | Actor location에 Spot lifecycle generation 누락 | S3-F13-A |
| C13-03 | 1차 소스 | high | `server/41-location-store-redis.ko.md:57` | transfer key의 raw colon 연결로 authority key 충돌 가능 | S3-F13-A |
| C13-04 | 1차 소스 | high | Kotlin `ToActorMessaging/feature-map.ko.md:9` | TA-A3·A4가 공통 STREAM lifecycle을 검증하지 않는데 완료로 표시 | S3-F13-B |
| C13-05 | 1차 소스 | high | Node·Kotlin `SpotActorTransfer/feature-map.ko.md:14` | ST-B3 기본 빈 state 성공 계약과 반대인 실패를 완료로 표시 | S3-F13-B |
| C13-06 | 1차 소스 | medium | `server/30-stream-session.ko.md:87` | server·HTTP host·connector registry instance 소유 모델 혼합 | S3-F13-A |
| C13-07 | 1차 소스 | medium | `server/51-runtime-metrics.ko.md:86` | transfer terminal metric이 committed와 activated를 혼합 | S3-F13-A |
| C13-08 | 1차 소스 | medium | `config-4-registration-codec.ko.md:160` | 명시적 codec 불일치의 fallback/error 결과가 닫혀 있지 않음 | S3-F13-A |
| C13-09 | 문서 원칙 | medium | Kotlin `guide/07-stream.ko.md:74` | `close()`를 현재 구현 no-op으로 설명 | S3-F13-B |
