# Java ResilienceLifecycle E2E feature map

이 문서는 Config 5 Resilience/Lifecycle 공통 시나리오 중 Java framework E2E가 현재 검증하는
항목과, public API 또는 harness 제어가 더 필요한 항목을 구분한다. 실행 시나리오는 public Spring
starter, `ZLinkClient`, `ZLinkChannelRuntimeOptions`, registry discovery, registry query client만
사용한다.

## 구현됨

- `RL-A1`: 같은 client 프로세스가 provider-b를 drain한 상태에서 provider-a 종료 구간의 public
  실패를 관찰하고, provider-a를 같은 endpoint로 재시작한 뒤 follow-up request가 다시 성공하는지
  확인한다.
- `RL-A2`: provider-a를 같은 routing id의 다른 endpoint로 재기동하고, 같은 client 프로세스가
  registry topology의 endpoint 갱신과 follow-up request 성공을 확인한다.
- `RL-B1`: 처리 중인 request를 client timeout으로 끝낸 뒤 같은 client의 후속 request가 정상 reply를
  받아 late reply가 pending을 오염시키지 않는지 확인한다.
- `RL-B3`: provider 정상 종료 뒤 registry topology에서 빠지고 같은 client의 후속 request가 남은
  provider로만 가는지 확인한다.
- `RL-B4`: provider admin 경로가 `clientServerChannel(name).configureServerSocket().weight(0/100)`을
  호출해 runtime drain/restore를 검증한다.
- `RL-B5`: 느린 handler가 이미 받은 request는 drain 뒤에도 정상 reply하고, drain 이후 새 request는
  다른 provider로 가는지 검증한다.
- `RL-D3`: 명시 `ZLinkMessageDispatchErrorObserver`가 미등록 request의
  reason/action/packetName marker를 evidence에 남기고, 이후 request가 정상 동작하는지 확인한다.

## public API/harness 대기

- `RL-A3`: process crash/replace 중 in-flight 결과를 고정하는 kill/restart harness가 필요하다.
- `RL-A4`: rolling restart를 provider 그룹 단위로 수행하는 orchestration이 아직 없다.
- `RL-A5`: blue/green provider 교체와 registry view 검증을 묶는 harness가 아직 없다.
- `RL-B2`: retry 정책 facade가 Java framework public client에 아직 없다.
- `RL-B6`: packet loss/half-open transport를 만드는 network partition harness가 아직 없다.
- `RL-C1`: resource cleanup을 process 종료 뒤 파일 descriptor/thread 관측으로 고정하는 harness가
  아직 없다.
- `RL-C2`: shutdown ordering을 단계별 evidence로 고정하는 harness가 아직 없다.
- `RL-C3`: timer lifecycle을 monitoring config와 통합한 장시간 harness가 필요하다.
- `RL-C4`: registry TTL 단축과 stale entry 제거를 검증하는 registry control이 아직 없다.
- `RL-D1`: 고fanout 부하를 장시간 주입하는 harness가 아직 없다.
- `RL-D2`: observer 실패 격리를 runtime error sink와 함께 단언하는 scenario가 아직 없다.
- `RL-D4`: error reply wire header의 code/message roundtrip을 raw envelope로 확인하는 harness가
  아직 없다.
- `RL-D5`: 지속 혼합 workload soak runner가 아직 없다.
