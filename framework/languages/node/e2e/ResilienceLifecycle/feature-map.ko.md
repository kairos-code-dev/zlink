# Node.js ResilienceLifecycle E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md`

현재 상태: Node.js `ResilienceLifecycle` config는 공통 문서의 현재 scenario를 모두 구현했다. 이 문서는 `.NET`
`framework/languages/dotnet/e2e/ResilienceLifecycle/feature-map.ko.md`와 공통 문서의 scenario ID를 기준으로
포팅 범위를 고정한다. 내부 helper나 raw-frame 우회로 gap을 완료 표시하지 않는다.

| Scenario | 상태 | 근거 |
|----------|------|------|
| RL-A1 | 구현 | provider B를 같은 endpoint로 재시작하고, consumer가 surviving provider와 restarted provider를 모두 통과하는 marker를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-A2 | 구현 | 같은 `api-b` routing id를 다른 endpoint로 재등록한 뒤 원래 endpoint로 복구하고, 두 endpoint의 evidence marker를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-A3 | 구현 | short-lived client host 24개를 순차 생성해 location store 기반 reconnect storm을 만들고, 정상 reply와 provider evidence marker를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-A4 | 구현 | public runtime drain으로 provider B를 drain하고, green provider 전환, original provider shutdown, 같은 endpoint 복구 뒤 `api-b` routing 복귀를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-A5 | 구현 | provider B를 3회 down/up 반복하고, down 구간의 api-a 수렴과 up 구간의 api-b 복귀 evidence를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-B1 | 구현 | slow request를 100ms timeout으로 끝낸 뒤 follow-up request, 늦은 server completion evidence, later request를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-B2 | 구현 | public evidence로 slow request가 `api-b`에서 시작된 것을 확인한 뒤 provider crash를 유발하고, in-flight public failure, surviving provider follow-up, `api-b` 복구 traffic을 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-B3 | 구현 | provider B 정상 종료 뒤 topology 이탈, 후속 request의 `api-a` 수렴, provider B 복구 readiness와 topology 재등록을 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-B4 | 10.0.0 전환 대상 | public `ZLinkRouteMeshRuntimeOptions.channel(meshName, channelName).weight`로 신규 select-one 제외·복원을 검증한다. source·runner 적용은 S9에서 수행한다. |
| RL-B5 | 구현 | public runtime drain 중 새 request는 drained provider로 가지 않고, drain 전에 시작된 slow request는 completion evidence와 reply를 모두 보존하는지 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-B6 | 구현 | provider B에 gray fault를 주입하고, no-retry request에서 public failure와 `api-a` healthy success를 함께 관찰한 뒤 fault 해제 후 follow-up request와 evidence marker를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-C1 | 구현 | short-lived client host request를 반복한 뒤 follow-up request와 provider evidence marker를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-C2 | 구현 | provider B crash 뒤 topology 이탈, new-client follow-up의 `api-a` 수렴, provider B 재시작 후 restored traffic과 evidence marker를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-C3 | 구현 | provider B shutdown으로 단절을 모사하고, down 중 `api-a` 처리와 provider B 재시작 뒤 restored traffic/evidence를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-C4 | 구현 | Redis location store outage 중 기존 channel request가 계속 성공하는지 확인하고, store 복구 뒤 topology 재조회와 follow-up evidence를 검증했다. 로그: `logs/20260703-211853-78546` |
| RL-D1 | 구현 | Redis location row로 연결한 subscriber 8개에 fanout event 120개를 발행하고, 각 subscriber가 같은 순서를 누락·중복 없이 수신하는지 검증했다. 로그: `logs/20260715-075646-2279409` |
| RL-D2 | 전환 필요 | observer fault 격리에 더해 public `ZLinkRuntimeErrorSink`가 `zlink.runtime_error`/`observer_failed`/`message_flow_observer` event를 한 번 받는지 검증해야 한다. |
| RL-D3 | 전환 필요 | provider dispatch-error evidence를 `outcome=failed`, `reason=no_handler`, `action=reply_error`, `packet_name`으로 재정렬해야 한다. |
| RL-D4 | 전환 필요 | raw wire gate는 있지만 server observer evidence의 이전 handler-missing/reply-error 표현을 공통 `no_handler`/`reply_error`로 전환해야 한다. |
| RL-D5 | 구현 | 8개 client가 request/send를 120초 동안 지속하고, 25,748쌍 전부 성공·tail send evidence·short-lived client 정리 후 follow-up을 확인했다. 전반/후반 p95는 모두 25ms였다. 로그: `logs/20260715-082358-2399471` |

검증:

- `framework/languages/node/e2e/ResilienceLifecycle/run_e2e.sh`
  - PASS: `logs/20260703-211853-78546`
  - RL-D1 실제 fanout 단독 PASS: `logs/20260715-075646-2279409`
  - RL-D4 error wire 단독 PASS: `logs/20260715-080129-2299877`
  - RL-D5 120초 soak 단독 PASS: `logs/20260715-082358-2399471`
  - 최신 전체 순서 재검증은 RL-A1에서 별도 topology probe가 `api-b` 제거를 확인한 직후 consumer의
    두 번째 request가 제거 중인 endpoint로 전달되어 실패했다: `logs/20260715-082315-2395393`.
    RL-D5에는 도달하지 않았으며, consumer 자체 peer 수렴을 확인하지 않는 RL-A1 검증 race는 별도로 추적한다.
- 남은 scenario: 없음
- 미착수 scenario: 없음
