# Java YieldDispatch E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`

이 문서는 Java Config 8 구현 상태와 남은 gap을 기록한다. 공통 E2E 문서는 새 public API를 바로
추가하는 근거가 아니라, 구현해야 할 public 동작과 누락을 식별하는 기준이다.

## public surface 확인됨

- spot handler 안에서 만든 `ZLinkRequestCall`은 `yield(Class<TReply>)` public terminator를 제공한다.
- actor join call은 `yield()`와 `yield(Class<TReply>)` public terminator를 제공한다.
- `context.runWorker(...)`가 반환하는 `ZLinkWorkerCall<T>`은 `yield()` public terminator를 제공한다.
- request, actor join, worker yield는 `CancellationToken`을 받는 cancellation-aware overload를 제공한다.
- route mesh request call에는 `yield`가 없으며, 공통 문서도 route mesh request 자체에 yield wrapper를
  붙이는 흐름을 금지한다.

## 구현 상태

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| YD-A1 | done | stream connector request가 session gateway를 거쳐 play spot으로 들어가고, 일반 `await` 동안 probe가 뒤에 남는 순서를 검증한다. |
| YD-A2 | done | 같은 target spot에서 `yield(Class<TReply>)`를 사용하고, probe가 먼저 처리된 뒤 원래 handler가 이어지는 순서를 검증한다. |
| YD-A3 | done | stream metadata로 target spot을 고르고, yield 전후 marker가 같은 request id, spot rid, correlation id를 유지하는지 검증한다. 공통 문서는 stream metadata나 cancellation token 상태를 Spot request handler public context에서 직접 읽는 검증을 YD-A3에 넣지 않는다. |
| YD-A4 | done | target spot handler가 `context.runWorker(...).yield()`로 worker pool 작업을 기다리는 동안 같은 Spot의 probe가 먼저 처리되는 순서를 검증한다. |
| YD-B1 | done | stream connector request가 session gateway actor relay로 들어가고 actor A와 actor B를 같은 target spot에 join한다. actor A가 `yield(Class<TReply>)`로 delay service를 기다리는 동안 actor B fast request가 먼저 완료되는 순서를 검증한다. `logs/20260702-070504-3148`에서 `scenario YD-B1 passed`를 확인했다. |
| YD-B2 | done | target spot에 join한 actor A가 `yield(Class<TReply>)`로 delay service를 기다리는 동안 같은 actor A에 fast request를 보내고, fast request marker가 actor A의 continuation과 completion 뒤에 실행되는 순서를 검증한다. |
| YD-B3 | done | `ActorJoinYieldReq`와 Entry Spot actor handler, client flow, runner marker를 추가했다. actor binding도 .NET처럼 Play role에서 만든 actor ref를 session에 bind하는 흐름으로 맞췄다. `logs/20260702-070504-3148`에서 `scenario YD-B3 passed`를 확인했다. |
| YD-C1 | done | 같은 target spot에서 yield 중인 timer와 빠른 timer를 함께 시작한다. yield 중인 timer가 delay reply를 기다리는 동안 빠른 timer tick이 먼저 완료되고, 그 뒤 yield timer가 resume/completion marker를 남기는 순서를 검증한다. `logs/20260702-070504-3148`에서 `scenario YD-C1 passed`를 확인했다. |
| YD-C2 | done | 같은 timer의 첫 tick이 `yield(Class<TReply>)`로 delay service를 기다리는 동안 다음 tick이 재진입하지 않고, 첫 tick continuation과 completion 뒤에 두 번째 tick marker가 남는 순서를 검증한다. `logs/20260702-070504-3148`에서 `scenario YD-C2 passed`를 확인했다. |
| YD-C3 | done | actor가 `yield(Class<TReply>)`로 기다리는 동안 timer tick이 완료되고, timer가 `yield(Class<TReply>)`로 기다리는 동안 다른 actor fast request가 완료되는 순서를 검증한다. evidence에는 actor mailbox와 timer mailbox가 분리되어 남는다. `logs/20260702-070504-3148`에서 `scenario YD-C3 passed`를 확인했다. |
| YD-D1 | done | `run_e2e.sh`가 registry, delay, play-a, play-b, session gateway, client process를 실제로 띄우고, 현재 Java가 구현한 A/B/C/D/E scenario를 같은 multi-process topology에서 통과시킨다. `logs/20260702-070504-3148`에서 `yield-dispatch e2e result=passed`를 확인했다. |
| YD-D2 | done | `play-a`의 owner spot handler가 `RequestToSpot(...).yield(Class<TReply>)`로 `play-b` target spot reply를 기다린다. `play-a-evidence.json`에는 `remote-yield-resumed`와 `targetNode=play-b`가 남고, `play-b-evidence.json`에는 target spot의 `yield-*` marker만 남는 것을 검증한다. `logs/20260702-070504-3148`에서 `scenario YD-D2 passed`를 확인했다. |
| YD-D3 | done | session gateway가 route mesh를 통해 `play-b` target spot으로 `YieldMsg`와 `ProbeMsg`를 보낸다. target spot handler가 delay service reply를 `yield(Class<TReply>)`로 기다리는 동안 같은 target spot의 probe가 먼저 처리되고, 이후 원래 handler가 resume/completion marker를 남기는 순서를 검증한다. `logs/20260702-070504-3148`에서 `scenario YD-D3 passed`를 확인했다. |
| YD-D4 | done | stream session relay가 bound actor handler로 보낸 request에서 actor가 `yield(Class<TReply>)`로 delay service reply를 기다린다. actor는 bound session push를 원래 stream connector로 보내고, 같은 시간 다른 actor의 push wait가 진행되지 않는 것을 검증한다. `logs/20260702-070504-3148`에서 `scenario YD-D4 passed`를 확인했다. |
| YD-E1 | done | `YieldTimeoutMsg`가 timeout 뒤 `timeout-yield-completed`를 남기고, 같은 Spot mailbox가 post-timeout `ProbeMsg`를 처리하는 순서를 검증한다. `logs/20260702-070504-3148`에서 `scenario YD-E1 passed`를 확인했다. |
| YD-E2 | done | request, actor join, worker yield public surface에 `CancellationToken` overload를 추가했다. `YieldCancelMsg`는 delay service request를 cancellation-aware `yield(...)`로 기다리다가 server-side token 취소를 관찰하고 `cancel-yield-completed`를 남긴다. 같은 Spot mailbox의 post-cancel `ProbeMsg`가 이어서 처리되고 `cancel-yield-unexpected-resumed`가 없음을 검증한다. `logs/20260702-070504-3148`에서 `scenario YD-E2 passed`를 확인했다. |
| YD-E3 | done | `ZLINK_JAVA_E2E_RUN_E3_SHUTDOWN=1 ./run_e2e.sh`가 pending yield 중 `play-a`에 SIGTERM을 보내고, client가 `.NET`처럼 stream error 또는 request timeout을 종료 신호로 받은 뒤 `play-a`를 같은 endpoint로 재시작해 recovery request를 통과시키는지 검증한다. `logs/20260702-070504-3148`에서 `yield-dispatch shutdown wait result=passed`, `yield-dispatch shutdown recovery result=passed`, `scenario YD-E3 passed`를 확인했다. |
| YD-E4 | done | `run_e2e.sh`가 HTTP scenario trigger, handler 밖 `.yield(`, real stream connector 생성 여부를 정적으로 검사한 뒤 full runner를 실행한다. `logs/20260702-070504-3148`에서 정적 검증과 runner marker gate가 모두 통과했다. |
| YD-E5 | done | `run_e2e.sh`가 `yield-dispatch-marker-report.json`을 생성하고, Java가 구현한 YD-A/B/C/D/E scenario id별 marker 이름이 공통 정의와 맞는지 검증한다. `logs/20260702-070504-3148/yield-dispatch-marker-report.json`에서 확인했다. 여러 언어 report를 모으는 aggregation은 공통 문서처럼 별도 parity gate가 담당한다. |

## 다음 구현 순서

현재 Java Config 8 안에서 남은 scenario 누락은 없다.
