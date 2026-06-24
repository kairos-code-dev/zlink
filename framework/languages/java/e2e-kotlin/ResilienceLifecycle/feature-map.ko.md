# Kotlin ResilienceLifecycle E2E feature map

이 문서는 Config 5 Resilience/Lifecycle 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. 현재
추적된 Kotlin ResilienceLifecycle runner/source가 없으므로 Kotlin 전용 stdout marker로 구현 완료를
주장하지 않는다.

## 구현됨

- 없음.

## public API/harness 대기

- `RL-A1`: provider restart Kotlin runner와 marker가 아직 없다.
- `RL-A2`: provider scale-out/in 반복 Kotlin harness가 아직 없다.
- `RL-A3`: provider crash/replace Kotlin harness가 아직 없다.
- `RL-A4`: rolling restart Kotlin harness가 아직 없다.
- `RL-A5`: blue/green 교체 Kotlin harness가 아직 없다.
- `RL-B1`: timeout 뒤 late reply 비오염 Kotlin marker가 아직 없다.
- `RL-B2`: retry 정책 Kotlin runner와 marker가 아직 없다.
- `RL-B3`: graceful shutdown 뒤 남은 provider routing Kotlin marker가 아직 없다.
- `RL-B4`: runtime drain/restore Kotlin marker가 아직 없다.
- `RL-B5`: drain 중 in-flight request Kotlin marker가 아직 없다.
- `RL-B6`: gray failure Kotlin harness가 아직 없다.
- `RL-C1`: resource cleanup Kotlin harness가 아직 없다.
- `RL-C2`: shutdown ordering Kotlin harness가 아직 없다.
- `RL-C3`: timer lifecycle Kotlin harness가 아직 없다.
- `RL-C4`: stale registry entry TTL Kotlin harness가 아직 없다.
- `RL-D1`: 고fanout 부하 Kotlin harness가 아직 없다.
- `RL-D2`: observer 실패 격리 Kotlin runner와 marker가 아직 없다.
- `RL-D3`: logging sink marker Kotlin runner와 marker가 아직 없다.
- `RL-D4`: error reply serialization Kotlin harness가 아직 없다.
- `RL-D5`: 지속 혼합 workload Kotlin soak runner가 아직 없다.
