# Java ResilienceLifecycle E2E feature map

이 문서는 Config 5 Resilience/Lifecycle 공통 시나리오 중 Java framework E2E가 현재 검증하는
항목과, 추가 public API 또는 harness 제어가 필요한 항목이 남았는지를 구분한다. Client는 HTTP driver이고, 실행
시나리오의 framework 참여는 `Server/Consumer` role이 맡는다. provider/consumer process lifecycle은
Client support가 제어한다. Provider와 Consumer role은 같은 Redis location store endpoint와 실행별
key prefix를 공유한다. Consumer role은 public Spring starter, `ZLinkClient`,
`ZLinkChannelRuntimeOptions`, public location runtime query만 사용한다.

마지막 검증:

- 명령: `nice -n 10 timeout 900s ./run_e2e.sh all`
- 결과: passed
- 로그: `framework/languages/java/e2e/ResilienceLifecycle/logs/20260707-221846-3647137/`
- 명령: `ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:57800 ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX=zlink:e2e:resilience-lifecycle:all-rerun1 timeout 900s ./run_e2e.sh all`
- 결과: passed
- 로그: `framework/languages/java/e2e/ResilienceLifecycle/logs/20260707-134830-2018139/`
- 추가 검증: `ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:57800 ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX=zlink:e2e:resilience-lifecycle:rl-a4-rerun2 timeout 420s ./run_e2e.sh RL-A4`
  - 결과: `scenario RL-A4 passed`, `resilience-lifecycle e2e result=passed`
  - 로그: `framework/languages/java/e2e/ResilienceLifecycle/logs/20260707-134716-2010002/`
- 추가 검증: `ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:62026 ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX=zlink:e2e:resilience-lifecycle:rl-b2-... timeout 420s ./run_e2e.sh RL-B2`
  - 결과: `scenario RL-B2 passed`, `resilience-lifecycle e2e result=passed`
  - 로그: `framework/languages/java/e2e/ResilienceLifecycle/logs/20260707-143234-2199882/`
- 추가 검증: `ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:62027 ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX=zlink:e2e:resilience-lifecycle:rl-c2-... timeout 420s ./run_e2e.sh RL-C2`
  - 결과: `scenario RL-C2 passed`, `resilience-lifecycle e2e result=passed`
  - 로그: `framework/languages/java/e2e/ResilienceLifecycle/logs/20260707-143319-2202901/`
- 추가 검증: `timeout 420s ./run_e2e.sh RL-C4`
  - 결과: `scenario RL-C4 passed`, `resilience-lifecycle e2e result=passed`
  - 로그: `framework/languages/java/e2e/ResilienceLifecycle/logs/20260707-184443-3120118/`

## 구현됨

- `RL-A1`: 같은 client 프로세스가 provider-b를 drain한 상태에서 provider-a 종료 구간의 public
  실패를 관찰하고, provider-a를 같은 endpoint로 재시작한 뒤 follow-up request가 다시 성공하는지
  확인한다.
- `RL-A2`: provider-a를 같은 routing id의 다른 endpoint로 재기동하고, 같은 client 프로세스가
  location peer row의 endpoint 갱신과 follow-up request 성공을 확인한다.
- `RL-A3`: 동시에 여러 client 프로세스를 두 차례 띄워 server에 재접속 폭주를 만들고, 각 client의
  public request가 정상 reply를 받는지 확인한다.
- `RL-A4`: provider-b를 public runtime drain으로 신규 request 대상에서 제외한 뒤 종료하고, 같은
  routing id의 green endpoint로 교체해 request가 green provider evidence에 기록되는지 확인한다.
  이후 green endpoint를 내리고 원래 provider-b endpoint를 복구해 신규 request가 복구된 provider로
  다시 가는지 확인한다.
- `RL-A5`: provider-a가 짧은 간격으로 down/up을 반복하는 동안 같은 client 프로세스가 지속 request를
  보내고, 살아 있는 provider-b로 수렴해 timeout 없이 follow-up까지 성공하는지 확인한다.
- `RL-B1`: 처리 중인 request를 client timeout으로 끝낸 뒤 같은 client의 후속 request가 정상 reply를
  받아 late reply가 pending을 오염시키지 않는지 확인한다.
- `RL-B2`: provider-b가 slow request를 받은 상태에서 강제 종료되면 in-flight request가 public 실패로
  끝나고, owner lease TTL 뒤 stale topology가 제거되어 provider-a로 수렴하는지 확인한다. 이후 같은
  routing id의 provider-b를 다시 올려 topology와 신규 request가 복구되는지도 확인한다.
- `RL-B3`: provider의 slow handler가 이미 받은 request를 정상 reply한 뒤 runtime drain이 terminal
  `Drained`로 끝나는지 확인한다. 이어서 location peer row 제거와 남은 provider의 후속 request 성공을
  검증한다.
- `RL-B4`: provider admin 경로가 `clientServerChannel(name).configureServerSocket().weight(0/100)`을
  호출해 transport 부하 제외와 복원을 검증한다. 이 동작을 graceful drain으로 판정하지 않는다.
- `RL-B5`: 느린 handler가 이미 받은 request는 weight 0 변경 뒤에도 정상 reply하고, 전파 완료 뒤의
  새 request는 다른 provider로 가는지 검증한다. `Draining`이나 actor handoff는 단언하지 않는다.
- `RL-B6`: provider-a에 public admin fault를 주입해 일부 request가 public 실패로 끝나는 동안,
  provider-b의 정상 reply가 계속 유지되고 follow-up request가 성공하는지 확인한다.
- `RL-C1`: 같은 Consumer role이 반복 request 뒤 follow-up request를 보내 public 경로의 client
  lifecycle cleanup을 관측한다.
- `RL-C2`: provider-b 강제 종료 뒤 public location topology에서 stale row가 빠지고, 살아 있는
  provider-a로 request가 수렴하는지 확인한다. provider-b 재기동 뒤 같은 endpoint row와 traffic
  복구도 함께 검증한다.
- `RL-C3`: provider-a 정지 구간의 public 실패와 재기동 후 topology 회복, 후속 request 성공을
  같은 restart orchestration에서 확인한다.
- `RL-C4`: runner가 실행별 Redis location store를 띄우고, Client support가 store를 일시 중지한
  동안 이미 연결된 provider channel의 request가 계속 성공하는지 확인한다. store 복구 뒤에는 public
  location topology와 후속 request가 다시 정상 동작하는지도 확인한다.
- `RL-D1`: 다수 client 프로세스가 동시에 request를 보내는 high fanout burst에서 정상 reply를
  유지하는지 확인한다.
- `RL-D2`: dispatch-error observer가 예외를 던지도록 fault를 켠 뒤 handler 없는 request를 보내고,
  observer failure가 provider process와 messaging 경로를 멈추지 않는지 후속 request와 evidence로
  확인한다.
- `RL-D3`: 명시 `ZLinkMessageFlowObserver`가 미등록 request의 `ERROR` outcome에서
  reason/action/packetName marker를 evidence에 남기고, 이후 request가 정상 동작하는지 확인한다.
- `RL-D4`: 같은 버전 provider/consumer 사이에서 handler 없는 request가 public error reply로
  실패하고, provider message-flow observer evidence에 미등록 packet marker가 남는지 확인한다. Java
  public client 표면은 error code header를 직접 노출하지 않으므로 code round-trip은 evidence와
  정상 follow-up request로 검증한다.
- `RL-D5`: 같은 실행 안에서 request와 send를 섞어 여러 window로 지속 주입하고, 처리 성공과 단순
  latency drift 한계를 관측한다.

## 남은 항목

- 갱신된 `RL-A1`은 terminal `Drained`, old row 제거, down 구간의 정확한 `RouteNotConnected`, 새 owner
  generation과 `ConnectionReady`를 요구한다. 현재 시나리오는 이 완료 조건을 모두 검증하지 않는다.
- 갱신된 `RL-A2`는 slow request handler-start 뒤 `SIGKILL`, owner lease 만료, 다른 endpoint의 새
  generation을 요구한다. 현재 시나리오는 weight 0 제어와 정상 process 교체가 섞여 있어 crash
  replacement 계약을 검증하지 않는다.
- `.NET Client/Scenarios/*.cs`에 대응하는 Java Client scenario 파일은 존재한다. 구현된 scenario는
  `Server/Consumer`의 HTTP endpoint를 호출한다.
