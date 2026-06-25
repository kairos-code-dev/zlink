# Node ResilienceLifecycle E2E feature map

이 문서는 Config 5 Resilience/Lifecycle 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `RL-B1`: 처리 중인 request가 timeout으로 실패한 뒤, 같은 client의 후속 request가 정상 reply를
  받고 늦은 server 완료가 다음 request를 오염시키지 않는지 확인한다.
- `RL-D2`: public message-flow observer가 dispatch error event에서 예외를 던져도 request 실패는
  원래 error reply로 끝나고, observer 예외가 runtime error sink의 `dispatch-error-observer` task로
  보고되며 후속 정상 request가 계속 성공하는지 확인한다.
- `RL-D3`: public message-flow observer를 명시적 logging sink로 등록하고, dispatch error event의
  `reason`·`action`·`packetName` marker가 파일 evidence에 남는지 확인한다.

## public API/harness 대기

- `RL-A1`: consumer 지속 상태에서 같은 endpoint provider restart Node runner와 marker가 아직 없다.
- `RL-A2`: 같은 rid와 새 endpoint로 pod 재스케줄을 검증하는 Node harness가 아직 없다.
- `RL-A3`: client reconnect storm Node harness가 아직 없다.
- `RL-A4`: rolling restart Node harness가 아직 없다.
- `RL-A5`: provider flapping Node harness가 아직 없다.
- `RL-B2`: in-flight request 중 provider crash Node runner와 marker가 아직 없다.
- `RL-B3`: graceful shutdown routing Node marker가 아직 없다.
- `RL-B4`: runtime drain/restore Node marker가 아직 없다.
- `RL-B5`: drain 중 in-flight request Node marker가 아직 없다.
- `RL-B6`: gray failure Node harness가 아직 없다.
- `RL-C1`: 다수 연결/요청 resource cleanup evidence Node harness가 아직 없다.
- `RL-C2`: registry stale data cleanup Node harness가 아직 없다.
- `RL-C3`: node disconnect/recovery Node harness가 아직 없다.
- `RL-C4`: registry restart/outage recovery Node harness가 아직 없다.
- `RL-D1`: 고fanout 부하 Node harness가 아직 없다.
- `RL-D4`: error reply serialization Node harness가 아직 없다.
- `RL-D5`: 지속 혼합 workload Node soak runner가 아직 없다.
