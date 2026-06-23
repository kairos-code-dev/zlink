# Java ResilienceLifecycle E2E feature map

이 디렉터리는 Config 5의 Java framework 검증이다. 실행 시나리오는 public Spring starter,
`ZLinkClient`, `ZLinkChannelRuntimeOptions`, registry discovery, registry query client만 사용한다.

## 구현됨

- RL-B4 runtime drain / restore: provider admin 경로가
  `clientServerChannel(name).configureServerSocket().weight(0/100)`을 호출한다. drain 뒤 새 request는
  drained provider를 피하고, restore 뒤 다시 traffic을 받는다.
- RL-B5 drain 중 in-flight request: 느린 handler가 이미 받은 request는 drain 뒤에도 정상 reply하고,
  drain 이후 새 request는 다른 provider로 간다.

## Java public API 미지원 또는 후속 harness 필요

- RL-A1/RL-A2/RL-A3/RL-A4/RL-A5: restart, duplicate rid handover, stale registry entry,
  process crash/replace 흐름은 별도 프로세스 재시작 orchestration과 registry 상태 관측 harness가 필요하다.
- RL-B1/RL-B2/RL-B3: timeout, retry, circuit-breaker 정책은 현재 Java framework public client에 별도 retry 정책
  facade가 없다.
- RL-B6 gray failure: packet loss/half-open transport를 만드는 네트워크 partition harness가 아직 없다.
- RL-C1/RL-C2/RL-C3: resource cleanup, shutdown ordering, timer lifecycle은 monitoring config와 통합한
  장시간 lifecycle harness에서 다룬다.
