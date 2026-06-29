# C++ Resilience/Lifecycle E2E feature map

이 문서는 Config 5 공통 시나리오를 C++ framework 공개 API와 현재 E2E harness로 어디까지
검증하는지 정리한다.

## 구현한 시나리오

- `RL-A1`: provider를 종료한 뒤 같은 endpoint와 같은 rid로 다시 띄우고, 실행 중인 consumer가
  재시작 없이 follow-up request를 성공하는지 검증한다.
- `RL-A2`: provider를 종료한 뒤 다른 endpoint와 같은 rid로 다시 띄우고, 실행 중인 consumer가
  교체 provider의 instance로 전환되는지 검증한다.
- `RL-A3`: 여러 client 프로세스가 짧은 간격으로 재접속하며 quick request를 성공시키는지
  반복 검증한다.
- `RL-A4`: drain/restore 흐름에서 provider set 전환 중 request가 계속 성공하고 restore 뒤
  교체 대상 provider가 다시 트래픽을 받는지 검증한다.
- `RL-A5`: provider 하나를 여러 차례 정지/재시작하며 살아 있는 provider로 request가 계속
  성공하고 복구 뒤에도 request가 정상화되는지 검증한다.
- `RL-B1`: 느린 request를 짧은 timeout으로 실패시킨 뒤, 같은 consumer의 후속 request가 late
  reply에 오염되지 않고 정상 동작하는지 검증한다.
- `RL-B2`: 느린 request 처리 중 provider를 `SIGKILL`로 종료하고, 해당 request가 실패한 뒤
  살아 있는 provider로 follow-up request가 성공하는지 검증한다.
- `RL-B3`: provider 하나를 정상 종료하고, 실행 중인 consumer가 남은 provider로 request를
  계속 성공시키는지 검증한다.
- `RL-B4`: provider 두 대로 분산 중 runtime admin HTTP handler가 public
  `channel_runtime_options_t`로 한 provider의 server peer weight를 `0`으로 drain하고, 신규
  request가 다른 provider로만 간 뒤 `100` restore 후 다시 해당 provider가 트래픽을 받는지
  검증한다.
- `RL-B5`: `api-b`의 느린 request가 처리 중일 때 같은 provider를 drain하고, 이미 in-flight였던
  request reply는 정상 수신되며 drain 이후 신규 request만 다른 provider로 가는지 검증한다.
- `RL-B6`: drain/restore 중 건강한 provider가 계속 request를 처리하고 restore 뒤 전체 request
  흐름이 정상화되는지 검증한다.
- `RL-C1`: 반복 client request 뒤 follow-up request를 성공시켜 정상 종료 전 리소스 정리 경로를
  관측한다.
- `RL-C2`: provider를 `SIGKILL`로 종료한 뒤 살아 있는 provider로 follow-up request가 성공하는지
  검증한다.
- `RL-C3`: provider 정지/복구를 반복한 뒤 request가 정상화되는지 검증한다.
- `RL-D1`: concurrent request fanout을 보내고 모든 request가 성공하는지 검증한다.
- `RL-D3`: missing send가 dispatch error marker를 flow log에 남기는지 검증한다.
- `RL-D4`: missing request error reply가 client에 public error message로 돌아오고 follow-up
  request가 정상화되는지 검증한다.
- `RL-D5`: request와 send를 섞은 concurrent workload 뒤 follow-up request가 성공하는지 검증한다.

## C++에서 제외한 시나리오

- `RL-C4`: registry를 멈춘 동안 같은 client 프로세스의 established channel request가 public
  timeout으로 bounded하게 끝나지 않고 native call이 오래 붙잡히는 경로가 있어 runner에 넣지
  않는다. registry outage 중 established socket 지속성은 C++ public call timeout 보장이 먼저
  정리되어야 한다.
- `RL-D2`: C++ framework product는 dispatch observer failure를 runtime diagnostic path로 분리해
  원래 reply/drop 결과를 바꾸지 않도록 처리한다. 현재 E2E runner는 이 observer 실패 event를
  공통 marker로 수집하고 단언하는 harness 연결이 아직 없다.
