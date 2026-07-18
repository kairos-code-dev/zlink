# .NET ResilienceLifecycle E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RL-A1 | 구현 | 단일 serving provider의 accepted request 완료, SIGTERM 뒤 `Drained` 종료와 row 제거, 정확한 down 오류, 같은 endpoint의 새 generation과 `ConnectionReady`, follow-up 20건을 확인한다. |
| RL-A2 | core 대기 | SIGKILL 뒤 replacement가 잠시 승인된 다음 늦은 stale pipe 종료가 새 lifetime을 강등해 복구가 실패한다. core 재승인 결함 수정 뒤 다시 실행한다. |
| RL-A3 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-A4 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-A5 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-B1 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-B2 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-B3 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-B4 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-B5 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-B6 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-C1 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-C2 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-C3 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-C4 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-D1 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| RL-D2 | 전환 필요 | observer fault 격리에 더해 public `IZLinkRuntimeErrorSink`가 `zlink.runtime_error`/`observer_failed`/`message_flow_observer` event를 한 번 받는지 검증해야 한다. |
| RL-D3 | 전환 필요 | dispatch-error evidence를 `outcome=failed`, `reason=no_handler`, `action=reply_error`, `packet_name`으로 재정렬해야 한다. |
| RL-D4 | 전환 필요 | missing request handler가 wire message kind `Error=5`와 `errorCode`·`errorMessage`를 반환하고 client에서는 정상 message가 아닌 request 예외로 끝나는지 검증해야 한다. 정상 follow-up request는 `Response=2`이며 error field가 없어야 한다. 현재 client 예외와 server dispatch-error evidence만으로는 wire 계약을 충족했다고 판정하지 않는다. |
| RL-D5 | 재검 대기 | 최신 전체 실행이 RL-A2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
