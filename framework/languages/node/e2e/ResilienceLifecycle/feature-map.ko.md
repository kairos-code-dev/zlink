# Node ResilienceLifecycle E2E feature map

이 문서는 Config 5 Resilience/Lifecycle 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `RL-A1`: provider를 같은 endpoint와 같은 routing id로 정상 종료 후 재시작하고, consumer 재시작 없이
  다운 구간 public failure와 재시작 뒤 `v2` provider reply 정상화를 확인한다.
- `RL-A2`: provider v1을 같은 routing id와 첫 endpoint에 광고한 뒤 public `app.close()` 정상 종료
  경로로 내리고, 같은 routing id를 새 endpoint에서 provider v2로 다시 광고한다. registry topology가
  v2 endpoint 하나로 수렴한 뒤 기존 consumer가 재시작 없이 후속 request 20개를 모두 v2 reply로
  받는지 확인한다.
- `RL-A3`: public client application 12개가 같은 server endpoint에 요청한 뒤 동시에 종료되고,
  한꺼번에 재기동해도 모든 client의 request가 다시 성공하며 후속 normalized wave도 안정적으로
  처리되는지 확인한다.
- `RL-A5`: 안정 provider를 유지한 채 다른 provider를 세 번 정상 종료/재기동하고, down 구간에는
  안정 provider로만 처리되며 up 후에는 다시 두 provider가 routing 대상이 되는지 확인한다.
- `RL-B1`: 처리 중인 request가 timeout으로 실패한 뒤, 같은 client의 후속 request가 정상 reply를
  받고 늦은 server 완료가 다음 request를 오염시키지 않는지 확인한다.
- `RL-B2`: public channel request가 child provider의 handler에 진입했다는 IPC evidence를 받은 뒤
  해당 provider 프로세스를 `SIGKILL`로 강제 종료하고, in-flight request가 public error/timeout으로
  끝나며 같은 channel의 안정 provider로 보내는 후속 request가 정상 reply를 받는지 확인한다.
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
- `RL-D5`: public channel client 6개가 2초 동안 request와 send를 계속 섞어 보내는 bounded soak를
  실행한다. 모든 request reply와 send delivery가 누락 없이 수집되고, latency sample의 최대값과
  앞/뒤 절반 평균 drift가 bounded threshold 안에 머무는지 관측한다. 리소스 누수의 엄밀한 증명은
  `RL-C1` 범위로 남긴다.

## public API/harness 대기

- `RL-A4`: public client request를 계속 보내는 동안 provider를 한 대씩 정상 종료하는 방식만으로는
  timeout 없이 rolling update를 증명할 수 없다. Node channel public API에는 provider를 종료 전에
  신규 request 대상에서 빼는 drain/weight runtime 계약이 아직 없어 완료 처리하지 않는다.
- `RL-B4`: runtime drain/restore Node marker가 아직 없다.
- `RL-B5`: drain 중 in-flight request Node marker가 아직 없다.
- `RL-B6`: gray failure Node harness가 아직 없다.
- `RL-C1`: 다수 연결/요청 resource cleanup evidence Node harness가 아직 없다.
- `RL-C2`: registry stale data cleanup Node harness가 아직 없다.
- `RL-C3`: node disconnect/recovery Node harness가 아직 없다.
- `RL-C4`: registry restart/outage recovery Node marker가 아직 없다. 현재 public same-process
  restart 시도는 provider 재광고와 restarted registry topology `Ready` evidence를 안정적으로 만들지
  못했으므로 완료 처리하지 않는다. process-isolated registry restart harness로 기존 channel socket
  지속과 복구 후 재조회 evidence를 함께 남길 때 닫는다.
- `RL-D4`: normal public client는 error reply의 message 기반 예외만 노출하고, 공통 시나리오가 요구하는
  `errorCode` raw header evidence를 Node E2E runner가 아직 수집하지 않는다. 내부 codec/helper를
  직접 호출해 완료 처리하지 않고, raw envelope evidence를 public e2e harness로 남길 수 있을 때 닫는다.
