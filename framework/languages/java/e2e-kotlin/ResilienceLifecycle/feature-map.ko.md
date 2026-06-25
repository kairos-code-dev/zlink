# Kotlin ResilienceLifecycle E2E feature map

이 문서는 Config 5 Resilience/Lifecycle 공통 시나리오 중 Kotlin E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다. 실행 시나리오는 public Spring starter,
`ZLinkClient`, `ZLinkChannelRuntimeOptions`, registry discovery, registry query client만 사용한다.

## 구현됨

- `RL-A1`: 같은 client 프로세스가 provider-b를 drain한 상태에서 provider-a 종료 구간의 public 실패를 관찰하고, provider-a를 같은 endpoint로 재시작한 뒤 follow-up request가 다시 성공하는지 확인한다.
- `RL-A2`: provider-a를 같은 routing id의 다른 endpoint로 재기동하고, 같은 client 프로세스가 registry topology의 endpoint 갱신과 follow-up request 성공을 확인한다.
- `RL-A3`: 동시에 여러 client 프로세스를 두 차례 띄워 server에 재접속 폭주를 만들고, 각 client의 public request가 정상 reply를 받는지 확인한다.
- `RL-A5`: provider-a가 짧은 간격으로 down/up을 반복하는 동안 같은 client 프로세스가 지속 request를 보내고, 살아 있는 provider-b로 수렴해 timeout 없이 follow-up까지 성공하는지 확인한다.
- `RL-B1`: 처리 중인 request를 client timeout으로 끝낸 뒤 같은 client의 후속 request가 정상 reply를 받아 late reply가 pending을 오염시키지 않는지 확인한다.
- `RL-B3`: provider 정상 종료 뒤 registry topology에서 빠지고 같은 client의 후속 request가 남은 provider로만 가는지 확인한다.
- `RL-B4`: provider admin 경로가 `clientServerChannel(name).configureServerSocket().weight(0/100)`을 호출해 runtime drain/restore를 검증한다.
- `RL-B5`: 느린 handler가 이미 받은 request는 drain 뒤에도 정상 reply하고, drain 이후 새 request는 다른 provider로 가는지 검증한다.
- `RL-B6`: provider-a에 public admin fault를 주입해 일부 request가 public 실패로 끝나는 동안, provider-b의 정상 reply가 계속 유지되고 follow-up request가 성공하는지 확인한다.
- `RL-C1`: 다량의 request와 send를 처리한 client가 정상 종료하고 runner가 프로세스 종료를 확인해 public 경로의 cleanup을 관측한다.
- `RL-C3`: provider-a 정지 구간의 public 실패와 재기동 후 topology 회복, 후속 request 성공을 같은 restart orchestration에서 확인한다.
- `RL-D1`: 다수 client 프로세스가 동시에 request를 보내는 high fanout burst에서 정상 reply를 유지하는지 확인한다.
- `RL-D3`: 명시 `ZLinkMessageDispatchErrorObserver`가 미등록 request의 reason/action/packetName marker를 evidence에 남기고, 이후 request가 정상 동작하는지 확인한다.
- `RL-D5`: 같은 실행 안에서 request와 send를 섞어 여러 window로 지속 주입하고, 처리 성공과 단순 latency drift 한계를 관측한다.

## public API/harness 대기

- `RL-A4`: rolling restart를 provider 그룹 단위로 수행하는 orchestration이 아직 없다.
- `RL-B2`: provider 강제 종료 중 in-flight request를 관측하는 runner는 시도됐지만, Java/Kotlin client가 종료 시 native context close에서 멈추는 경로가 있어 완료 처리하지 않는다.
- `RL-C2`: registry heartbeat timeout을 짧게 조정하는 public embedded-registry 옵션이 없어, provider 강제 종료 뒤 TTL stale entry 제거를 빠르고 결정적으로 고정하는 runner가 아직 없다.
- `RL-C4`: registry 중단 중 이미 연결된 channel request는 확인됐지만, registry 재기동 뒤 새 client의 follow-up request가 `NOT_ADMITTED`로 남는 경로가 있어 완료 처리하지 않는다.
- `RL-D2`: observer 실패 격리를 runtime error sink와 함께 단언하는 scenario가 아직 없다.
- `RL-D4`: error reply wire header의 code/message roundtrip을 raw envelope로 확인하는 harness가 아직 없다.
