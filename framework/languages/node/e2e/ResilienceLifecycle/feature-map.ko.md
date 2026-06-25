# Node ResilienceLifecycle E2E feature map

이 문서는 Config 5 Resilience/Lifecycle 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- 없음.

## public API/harness 대기

- `RL-A1`: consumer 지속 상태에서 같은 endpoint provider restart Node runner와 marker가 아직 없다.
- `RL-A2`: provider scale-out/in 반복 Node harness가 아직 없다.
- `RL-A3`: provider crash/replace Node harness가 아직 없다.
- `RL-A4`: rolling restart Node harness가 아직 없다.
- `RL-A5`: blue/green 교체 Node harness가 아직 없다.
- `RL-B1`: timeout 뒤 late reply 비오염 Node marker가 아직 없다.
- `RL-B2`: retry 정책 Node runner와 marker가 아직 없다.
- `RL-B3`: graceful shutdown routing Node marker가 아직 없다.
- `RL-B4`: runtime drain/restore Node marker가 아직 없다.
- `RL-B5`: drain 중 in-flight request Node marker가 아직 없다.
- `RL-B6`: gray failure Node harness가 아직 없다.
- `RL-C1`: 다수 연결/요청 resource cleanup evidence Node harness가 아직 없다.
- `RL-C2`: shutdown ordering Node harness가 아직 없다.
- `RL-C3`: timer lifecycle Node harness가 아직 없다.
- `RL-C4`: stale registry entry TTL Node harness가 아직 없다.
- `RL-D1`: 고fanout 부하 Node harness가 아직 없다.
- `RL-D2`: observer 실패 격리 Node runner와 marker가 아직 없다.
- `RL-D3`: logging sink marker Node runner와 marker가 아직 없다.
- `RL-D4`: error reply serialization Node harness가 아직 없다.
- `RL-D5`: 지속 혼합 workload Node soak runner가 아직 없다.
