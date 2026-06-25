# Node ResilienceLifecycle E2E feature map

이 문서는 Config 5 Resilience/Lifecycle 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `RL-A1`: provider를 같은 endpoint와 같은 routing id로 정상 종료 후 재시작하고, consumer 재시작 없이
  다운 구간 public failure와 재시작 뒤 `v2` provider reply 정상화를 확인한다.
- `RL-A5`: 안정 provider를 유지한 채 다른 provider를 세 번 정상 종료/재기동하고, down 구간에는
  안정 provider로만 처리되며 up 후에는 다시 두 provider가 routing 대상이 되는지 확인한다.
- `RL-B1`: 처리 중인 request가 timeout으로 실패한 뒤, 같은 client의 후속 request가 정상 reply를
  받고 늦은 server 완료가 다음 request를 오염시키지 않는지 확인한다.
- `RL-B3`: provider 두 대를 registry discovery로 붙인 뒤 provider 하나를 public `app.close()` 정상
  종료 경로로 내리고, topology에서 빠진 뒤 consumer 재시작 없이 후속 request가 남은 provider로만
  가는지 확인한다.
- `RL-D1`: 여러 subscriber application이 같은 public fanout channel에 붙어 연속 publish를 받아도
  모든 subscriber evidence가 누락 없이 수집되는지 확인한다.
- `RL-D2`: public message-flow observer가 dispatch error event에서 예외를 던져도 request 실패는
  원래 error reply로 끝나고, observer 예외가 runtime error sink의 `dispatch-error-observer` task로
  보고되며 후속 정상 request가 계속 성공하는지 확인한다.
- `RL-D3`: public message-flow observer를 명시적 logging sink로 등록하고, dispatch error event의
  `reason`·`action`·`packetName` marker가 파일 evidence에 남는지 확인한다.

## public API/harness 대기

- `RL-A2`: 같은 rid와 새 endpoint로 pod 재스케줄을 검증하는 Node harness가 아직 없다.
- `RL-A3`: client reconnect storm Node harness가 아직 없다.
- `RL-A4`: rolling restart Node harness가 아직 없다.
- `RL-B2`: in-flight request 중 provider crash Node runner와 marker가 아직 없다.
- `RL-B4`: runtime drain/restore Node marker가 아직 없다.
- `RL-B5`: drain 중 in-flight request Node marker가 아직 없다.
- `RL-B6`: gray failure Node harness가 아직 없다.
- `RL-C1`: 다수 연결/요청 resource cleanup evidence Node harness가 아직 없다.
- `RL-C2`: registry stale data cleanup Node harness가 아직 없다.
- `RL-C3`: node disconnect/recovery Node harness가 아직 없다.
- `RL-C4`: registry restart/outage recovery Node harness가 아직 없다.
- `RL-D4`: normal public client는 error reply의 message 기반 예외만 노출하고, 공통 시나리오가 요구하는
  `errorCode` raw header evidence를 Node E2E runner가 아직 수집하지 않는다. 내부 codec/helper를
  직접 호출해 완료 처리하지 않고, raw envelope evidence를 public e2e harness로 남길 수 있을 때 닫는다.
- `RL-D5`: 지속 혼합 workload Node soak runner가 아직 없다.
