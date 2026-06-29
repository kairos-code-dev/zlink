# Java YieldDispatch E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`

이 문서는 Java Config 8 구현 상태와 남은 gap을 기록한다. 공통 E2E 문서는 새 public API를 바로
추가하는 근거가 아니라, 구현해야 할 public 동작과 누락을 식별하는 기준이다.

## public surface 확인됨

- spot handler 안에서 만든 `ZLinkRequestCall`은 `yield(Class<TReply>)` public terminator를 제공한다.
- actor join call은 `yield()`와 `yield(Class<TReply>)` public terminator를 제공한다.
- `context.runWorker(...)`가 반환하는 `ZLinkWorkerCall<T>`은 `yield()` public terminator를 제공한다.
- route mesh request call에는 `yield`가 없으며, 공통 문서도 route mesh request 자체에 yield wrapper를
  붙이는 흐름을 금지한다.

## 구현 상태

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| YD-A1 | done | stream connector request가 session gateway를 거쳐 play spot으로 들어가고, 일반 `await` 동안 probe가 뒤에 남는 순서를 검증한다. |
| YD-A2 | done | 같은 target spot에서 `yield(Class<TReply>)`를 사용하고, probe가 먼저 처리된 뒤 원래 handler가 이어지는 순서를 검증한다. |
| YD-A3 | partial | stream metadata로 target spot을 고르고, yield 전후 marker가 같은 request id, spot rid, correlation id를 유지하는지 검증한다. 현재 Java spot request handler 공개 계약은 cancellation token 상태를 handler에 노출하지 않아 그 부분은 gap이다. |
| YD-A4 | done | target spot handler가 `context.runWorker(...).yield()`로 worker pool 작업을 기다리는 동안 같은 Spot의 probe가 먼저 처리되는 순서를 검증한다. |
| YD-B1 | done | stream connector request가 session gateway actor relay로 들어간다. actor A와 actor B를 같은 target spot에 join한 뒤, actor A가 `yield(Class<TReply>)`로 delay service를 기다리는 동안 actor B fast request가 actor A continuation보다 먼저 완료되는 것을 검증한다. `logs/20260630-031940-2462845`에서 actor B join, 다른 actor 진행, B2 같은 actor 재진입 금지를 함께 통과했다. |
| YD-B2 | done | target spot에 join한 actor A가 `yield(Class<TReply>)`로 delay service를 기다리는 동안 같은 actor A에 fast request를 보내고, fast request marker가 actor A의 continuation과 completion 뒤에 실행되는 순서를 검증한다. |
| YD-B3 | done | `ActorJoinYieldRequest`와 Entry Spot actor handler, client flow, runner marker를 추가했다. actor binding도 .NET처럼 Play role에서 만든 actor ref를 session에 bind하는 흐름으로 맞췄다. `run_e2e.sh`의 `logs/20260630-031940-2462845`에서 `scenario YD-B3 passed`까지 통과했다. |
| YD-C1 | done | 같은 target spot에서 yield 중인 timer와 빠른 timer를 함께 시작한다. yield 중인 timer가 delay reply를 기다리는 동안 빠른 timer tick이 먼저 완료되고, 그 뒤 yield timer가 resume/completion marker를 남기는 순서를 검증한다. `logs/20260630-031940-2462845`에서 `scenario YD-C1 passed`까지 통과했다. |
| YD-C2 | done | 같은 timer의 첫 tick이 `yield(Class<TReply>)`로 delay service를 기다리는 동안 다음 tick이 재진입하지 않고, 첫 tick continuation과 completion 뒤에 두 번째 tick marker가 남는 순서를 검증한다. `logs/20260630-031940-2462845`에서 `scenario YD-C2 passed`까지 통과했다. |
| YD-C3 | done | actor가 `yield(Class<TReply>)`로 기다리는 동안 timer tick이 완료되고, timer가 `yield(Class<TReply>)`로 기다리는 동안 다른 actor fast request가 완료되는 순서를 검증한다. evidence에는 actor mailbox와 timer mailbox가 분리되어 남는다. `logs/20260630-031940-2462845`에서 `scenario YD-C3 passed`까지 통과했다. |
| YD-D1 | partial | `run_e2e.sh`가 registry, delay, play-a, play-b, session gateway, client process를 실제로 띄우고 YD-A1/YD-A2/YD-A3/YD-A4, YD-B1, YD-B2, YD-B3, YD-C1, YD-C2, YD-C3, YD-D2, YD-D3, YD-D4 marker를 검증한다. `logs/20260630-031940-2462845`에서 이 범위는 통과했다. 전체 Config 8 scenario 묶음은 아직 아니다. |
| YD-D2 | done | `play-a`의 owner spot handler가 `RequestToSpot(...).yield(Class<TReply>)`로 `play-b` target spot reply를 기다린다. `play-a-evidence.json`에는 `remote-yield-resumed`와 `targetNode=play-b`가 남고, `play-b-evidence.json`에는 target spot의 `yield-*` marker만 남는 것을 검증한다. `logs/20260630-031940-2462845`에서 `scenario YD-D2 passed`까지 통과했다. |
| YD-D3 | done | session gateway가 route mesh를 통해 `play-b` target spot으로 `YieldCommand`와 `ProbeCommand`를 보낸다. target spot handler가 delay service reply를 `yield(Class<TReply>)`로 기다리는 동안 같은 target spot의 probe가 먼저 처리되고, 이후 원래 handler가 resume/completion marker를 남기는 순서를 검증한다. `logs/20260630-031940-2462845`에서 `scenario YD-D3 passed`까지 통과했다. |
| YD-D4 | done | stream session relay가 bound actor handler로 보낸 request에서 actor가 `yield(Class<TReply>)`로 delay service reply를 기다린다. actor는 bound session push를 원래 stream connector로 보내고, 같은 시간 다른 actor의 push wait가 진행되지 않는 것을 검증한다. `logs/20260630-031940-2462845`에서 `scenario YD-D4 passed`까지 통과했다. |
| YD-E1 | gap | yield 중 timeout cleanup scenario가 아직 없다. |
| YD-E2 | gap | yield 중 cancellation cleanup scenario가 아직 없다. |
| YD-E3 | gap | runtime shutdown 중 yield cleanup과 restart recovery scenario가 아직 없다. |
| YD-E4 | gap | 금지 표면 정적 검증 runner가 아직 없다. |
| YD-E5 | gap | cross-language marker report 비교는 Java 구현 뒤 별도 집계 단계가 필요하다. |

## 다음 구현 순서

1. `YD-A3`의 cancellation token 상태 검증은 Java spot request handler 공개 계약에서 접근할 방법을 확인한 뒤 별도 설계 gap으로 처리한다.
2. timeout, cancellation, shutdown recovery cleanup scenario를 별도 gate로 확장한다.
