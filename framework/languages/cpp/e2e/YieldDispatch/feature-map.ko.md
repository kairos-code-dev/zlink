# C++ YieldDispatch E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`

이 디렉터리는 Config 8 `YieldDispatch`의 C++ 포팅 위치다. 현재 C++ 트리에는 role target과
Track A YD-A1~YD-A4, Track B YD-B1~YD-B3, Track C YD-C1~YD-C3, Track D YD-D1~YD-D4, Track E YD-E1, YD-E3, YD-E4 정적 검증 코드가 있다. C++ framework에는 channel request,
actor join, timer, bound session send, worker call에 `yield()` 공개 표면이 있지만, Config 8 완료 기준은 실제
stream connector client request가 session gateway를 거쳐 play node의 Spot/Entry Spot handler까지
도달하는 전체 배포 경로다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `YD-A1` | done | `run_e2e.sh`가 stream connector client request -> Session gateway -> Play Spot handler -> Delay service 경로를 통과한다. 로그: `logs/20260630-093239-3426210`, 출력: `scenario YD-A1 passed`. Session host도 spot mesh를 열어 public `route_client_t` spot request overload가 route bridge를 통해 Play Spot handler로 들어간다. |
| `YD-A2` | done | 같은 runner에서 `YieldReq`가 delay outbound call을 `yield()`로 기다리는 동안 별도 stream connector가 release/evidence/probe를 확인한다. 로그: `logs/20260630-093239-3426210`, 출력: `scenario YD-A2 passed`. |
| `YD-A3` | done | `YieldMsg`가 `yield()` 전후에 같은 request id, spot rid, correlation id를 evidence에 남기고, yield continuation marker 순서가 보존되는지 검증한다. 로그: `logs/20260630-182120-718488`, 출력: `scenario YD-A3 passed`. `.NET` feature-map도 stream metadata 직접 노출을 완료 조건에 넣지 않으므로 별도 public contract gap으로 세지 않는다. |
| `YD-A4` | done | `WorkerYieldReq` handler가 public `spot_context_t::run_worker(...)` call object를 `yield()`로 기다리는 동안 별도 stream connector가 같은 Spot에 `ProbeReq`를 보내고, worker continuation이 원래 Spot mailbox에서 재개되는 marker 순서를 검증한다. 로그: `logs/20260630-093239-3426210`, 출력: `scenario YD-A4 passed`. |
| `YD-B1` | done | stream connector request가 Session gateway의 actor binding/relay를 거쳐 Play Entry Spot actor handler로 도달한다. actor A가 delay request를 `yield()`로 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker를 검증한다. 로그: `logs/20260630-101935-3481095`, 출력: `scenario YD-B1 passed`. |
| `YD-B2` | done | actor A가 `yield()` 중일 때 같은 stream connector session에서 같은 actor A의 fast request를 보내고, fast request가 actor yield continuation/completion 뒤에 실행되는 marker를 검증한다. 로그: `logs/20260630-160702-358191`, 출력: `scenario YD-B2 passed`, `yield-dispatch e2e result=passed`. |
| `YD-B3` | done | Entry Spot actor handler가 public `actor_context_t::join_entry_spot(...).yield<T>()`로 actor join 결과를 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker를 검증한다. 공통 문서가 허용한 `JoinEntrySpot` terminator 경로를 사용한다. 로그: `logs/20260630-101935-3481095`, 출력: `scenario YD-B3 passed`. |
| `YD-C1` | done | stream connector request가 Session gateway를 거쳐 같은 Play Spot에 timer A와 timer B를 등록한다. timer A가 delay request를 `yield()`로 기다리는 동안 timer B fast tick이 먼저 완료되는 marker 순서를 검증한다. timer handler coroutine이 지역 `timer_runtime_t`의 `this`를 suspension 뒤에도 읽는 수명 문제와 routed reply context 보관 문제를 정리한 뒤 반복 runner에서 통과했다. 로그: `logs/20260630-114500-3835073`, `logs/20260630-114512-3836412`, `logs/20260630-114544-3838681`, 출력: `scenario YD-C1 passed`. |
| `YD-C2` | done | 같은 timer가 delay request를 `yield()`로 기다리는 동안 다음 tick이 먼저 재진입하지 않고, continuation/completion 뒤에 실행되는 marker 순서를 검증한다. timer async dispatch가 coroutine frame 안에 Spot context를 보관하고 pending fire를 같은 context로 다시 게시하도록 고친 뒤 반복 runner에서 통과했다. 로그: `logs/20260630-114500-3835073`, `logs/20260630-114512-3836412`, `logs/20260630-114544-3838681`, 출력: `scenario YD-C2 passed`. |
| `YD-C3` | partial | actor A가 `yield()` 중일 때 timer fast tick이 완료되는 순서와, timer가 `yield()` 중일 때 actor B fast request가 완료되는 순서를 모두 검증한다. 로그: `logs/20260630-182120-718488`, 출력: `scenario YD-C3 passed`. 다만 actor-yield 중 timer-fast half는 C++ stream connector의 pending request 직렬화 한계를 피하려고 같은 session에 actor binding을 공유한 observer connector에서 timer command를 보낸다. `.NET`의 같은 stream session 증거와 완전히 같지는 않으므로 partial로 둔다. |
| `YD-D1` | done | runner가 full client 실행 뒤 `play-a.evidence.log`에서 local topology marker를 직접 확인한다. `hold-completed`, `yield-completed`, `worker-yield-completed`, `actor-yield-completed`, `timer-yield-completed`, `timeout-yield-completed`가 모두 `rid=play-a`로 남아야 통과한다. 로그: `logs/20260630-160702-358191`, 출력: `scenario YD-D1 passed`, `yield-dispatch e2e result=passed`. |
| `YD-D2` | done | `RemoteSpotYieldReq`가 `play-a` owner Spot에서 `play-b` target Spot으로 public `spot_context_t::request_to(...).yield()`를 보내고, caller continuation marker가 `play-a`에 남는지 검증한다. spot-route async reply가 full `received_t` 대신 routing id, spot id, request seq만 보관하도록 고친 뒤 반복 runner에서 통과했다. 로그: `logs/20260630-114500-3835073`, `logs/20260630-114512-3836412`, `logs/20260630-114544-3838681`, 출력: `scenario YD-D2 passed`, `yield-dispatch track-a-d result=passed`. |
| `YD-D3` | done | stream connector command가 Session gateway route bridge를 거쳐 `play-b` target Spot handler로 들어가고, 해당 Spot handler가 `yield()` 중 probe command를 먼저 처리한 뒤 원래 continuation을 재개하는 marker 순서를 검증한다. 로그: `logs/20260630-144300-193620`, 출력: `scenario YD-D3 passed`, `yield-dispatch track-a-d result=passed`. |
| `YD-D4` | done | stream connector request가 Session gateway의 actor binding/relay를 거쳐 Play Entry Spot actor handler로 도달하고, actor handler가 `yield()` 뒤 `actor.context.bound_session().send(...)`로 push를 보낸다. session-a의 bound connector만 `ActorPushNotify`를 받고 session-b의 unbound connector는 받지 않는 범위를 검증한다. bound session typed send가 erased serializer 경로로 빠지던 문제를 typed JSON serializer 경로로 고친 뒤 통과했다. 로그: `logs/20260630-151241-249658`, 출력: `scenario YD-D4 passed`, `yield-dispatch track-a-d result=passed`. |
| `YD-E1` | done | `YieldTimeoutCommand`가 Session gateway route bridge를 거쳐 Spot handler로 들어가고, delay service reply보다 짧은 timeout으로 `yield()`가 public timeout error를 관찰한 뒤 같은 Spot이 `ProbeCommand`를 처리하는 marker 순서를 검증한다. 메시지별 codec 등록 없이 typed JSON serializer 경로와 session relay 분기로 처리한다. 로그: `logs/20260630-152535-269343`, 출력: `scenario YD-E1 passed`, `yield-dispatch track-a-e1 result=passed`. |
| `YD-E2` | gap | `.NET` 기준은 `Yield<DelayReply>(CancellationToken)`으로 handler 내부 cancellation token을 넘겨 yield 대기를 중단한다. 현재 C++ public `request_call_t::yield()`와 actor/Spot yield 표면에는 cancellation token 인자가 없고 내부 pending cancellation API만 있으므로, handler-local timeout이나 private cancel 우회로 구현하지 않는다. |
| `YD-E3` | done | `shutdown-wait` client scenario가 stream connector request로 long yield를 시작하고, runner가 `play-a.evidence.log`의 `yield-released` marker를 확인한 뒤 play-a에 SIGTERM을 보내 정상 종료한다. client는 request timeout이 아니라 connector closed/disconnected 계열 public error를 받아야 통과한다. 같은 spot rid로 play-a를 재시작한 뒤 `shutdown-recovery` scenario가 recovery probe를 보내 routing id 재사용과 mailbox cleanup을 검증한다. 로그: `logs/20260630-160702-358191`, 출력: `yield-dispatch shutdown wait result=passed`, `yield-dispatch shutdown recovery result=passed`, `yield-dispatch e2e result=passed`. |
| `YD-E4` | done | `run_e2e.sh` static gate가 `/yield` HTTP trigger/client 사용, Play Spot/Entry Spot handler 밖 `yield()` 사용, client scenario thin helper 사용을 금지하고, full client가 실제 `connector_factory_t::create`로 stream connector를 만들며 각 `yd_*.hpp` scenario가 connector 참조를 직접 받는지 검사한다. 로그: `logs/20260630-160702-358191`, 출력: `yield-dispatch e2e result=passed`. |
| `YD-E5` | gap | cross-language report 집계 단계는 C++ 단독 포팅 밖의 후속 parity 작업이다. `.NET`도 부분 구현으로 남긴다. |

## 다음 구현 기준

- 첫 구현 slice는 `.NET` 구조처럼 Registry, Delay, Play, Session, Client target을 나누고,
  `run_e2e.sh`가 실제 stream connector client request로 scenario를 시작한다. 현재 Track A의
  YD-A1~YD-A4, Track B의 YD-B1~YD-B3, Track C의 YD-C1~YD-C3, Track D의 YD-D1~YD-D4, Track E의
  YD-E1, YD-E3, YD-E4 static gate는 runner 증거가 있다. 다음 slice는 남은 public contract gap 정리다.
- yield 검증을 HTTP endpoint나 direct route/Spot test driver로 시작하지 않는다.
- C++ public API로 아직 확인되지 않은 항목은 내부 helper나 raw frame 우회로 메우지 않고
  별도 public contract gap으로 유지한다.
