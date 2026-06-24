# Java ResilienceLifecycle E2E feature map

이 문서는 Config 5 Resilience/Lifecycle 공통 시나리오 중 Java framework E2E가 현재 검증하는
항목과, public API 또는 harness 제어가 더 필요한 항목을 구분한다. 실행 시나리오는 public Spring
starter, `ZLinkClient`, `ZLinkChannelRuntimeOptions`, registry discovery, registry query client만
사용한다.

## 구현됨

- `RL-B4`: provider admin 경로가 `clientServerChannel(name).configureServerSocket().weight(0/100)`을
  호출해 runtime drain/restore를 검증한다.
- `RL-B5`: 느린 handler가 이미 받은 request는 drain 뒤에도 정상 reply하고, drain 이후 새 request는
  다른 provider로 가는지 검증한다.

## public API/harness 대기

- `RL-A1`: provider restart 뒤 기존 consumer가 follow-up request를 성공하는지 보는 restart 단계가
  아직 없다.
- `RL-A2`: provider 추가/제거 반복 중 traffic 안정성을 보는 장시간 harness가 필요하다.
- `RL-A3`: process crash/replace 중 in-flight 결과를 고정하는 kill/restart harness가 필요하다.
- `RL-A4`: rolling restart를 provider 그룹 단위로 수행하는 orchestration이 아직 없다.
- `RL-A5`: blue/green provider 교체와 registry view 검증을 묶는 harness가 아직 없다.
- `RL-B1`: timeout 뒤 late reply 비오염은 RegistryMessaging RM-C4에서 일부 확인하지만,
  Config 5 독립 marker는 아직 없다.
- `RL-B2`: retry 정책 facade가 Java framework public client에 아직 없다.
- `RL-B3`: graceful shutdown 뒤 남은 provider로만 routing되는 Config 5 독립 marker가 아직 없다.
- `RL-B6`: packet loss/half-open transport를 만드는 network partition harness가 아직 없다.
- `RL-C1`: resource cleanup을 process 종료 뒤 파일 descriptor/thread 관측으로 고정하는 harness가
  아직 없다.
- `RL-C2`: shutdown ordering을 단계별 evidence로 고정하는 harness가 아직 없다.
- `RL-C3`: timer lifecycle을 monitoring config와 통합한 장시간 harness가 필요하다.
- `RL-C4`: registry TTL 단축과 stale entry 제거를 검증하는 registry control이 아직 없다.
- `RL-D1`: 고fanout 부하를 장시간 주입하는 harness가 아직 없다.
- `RL-D2`: observer 실패 격리를 runtime error sink와 함께 단언하는 scenario가 아직 없다.
- `RL-D3`: 명시 logging sink에서 dispatch 오류 marker를 고정하는 scenario가 아직 없다.
- `RL-D4`: error reply wire header의 code/message roundtrip을 raw envelope로 확인하는 harness가
  아직 없다.
- `RL-D5`: 지속 혼합 workload soak runner가 아직 없다.
