# C++ AutomaticTurnDispatch E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-automatic-turn-dispatch.ko.md`

이 디렉터리는 Config 8 `AutomaticTurnDispatch`의 C++ 포팅 위치다. 현재 C++ 트리에는 role target과
Track A ATD-A1~ATD-A4, Track B ATD-B1~ATD-B3, Track C ATD-C1~ATD-C3, Track D ATD-D1~ATD-D4, Track E ATD-E1~ATD-E5 코드가 있다. C++ framework에는 channel request,
actor join, timer, bound session send, worker call에 `await()` 공개 표면이 있지만, Config 8 완료 기준은 실제
stream connector client request가 session gateway를 거쳐 play node의 Spot/Entry Spot handler까지
도달하는 전체 배포 경로다.

최신 full runner proof는 `logs/20260707-151703-2374204`이다. 이 실행은 Redis location store 기반으로
registry role 없이 ATD-A1~ATD-E5를 통과했다. ATD-E2는 public `cancellation_token_source_t`와
`cancellation_token_t`, 그리고 `await(token)` 표면으로 delay request 대기 취소와 Spot mailbox cleanup을
검증한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `ATD-A1` | done | `run_e2e.sh`가 stream connector client request -> Session gateway -> Play Spot handler -> Delay service 경로를 통과한다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-A1 passed`. Session host도 spot mesh를 열어 public `route_client_t` spot request overload가 route bridge를 통해 Play Spot handler로 들어간다. |
| `ATD-A2` | done | 같은 runner에서 `YieldReq`가 delay outbound call을 `await()`로 기다리는 동안 별도 stream connector가 release/evidence/probe를 확인한다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-A2 passed`. |
| `ATD-A3` | done | `YieldMsg`가 `await()` 전후에 같은 request id, spot rid, correlation id를 evidence에 남기고, await continuation marker 순서가 보존되는지 검증한다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-A3 passed`. `.NET` feature-map도 stream metadata 직접 노출을 완료 조건에 넣지 않으므로 별도 public contract gap으로 세지 않는다. |
| `ATD-A4` | done | `WorkerYieldReq` handler가 public `spot_context_t::run_worker(...)` call object를 `await()`로 기다리는 동안 별도 stream connector가 같은 Spot에 `ProbeReq`를 보내고, worker continuation이 원래 Spot mailbox에서 재개되는 marker 순서를 검증한다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-A4 passed`. |
| `ATD-B1` | done | stream connector request가 Session gateway의 actor binding/relay를 거쳐 Play user Spot actor handler로 도달한다. actor A가 delay request를 `await()`로 기다리는 동안 같은 session-a connector에서 actor B fast request를 보내 먼저 완료되는 marker를 검증한다. 최신 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-B1 passed`. |
| `ATD-B2` | done | actor A가 `await()` 중일 때 같은 stream connector session에서 같은 actor A의 fast request를 보내고, fast request가 actor await continuation/completion 뒤에 실행되는 marker를 검증한다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-B2 passed`, `automatic-turn-dispatch e2e result=passed`. |
| `ATD-B3` | done | Play user Spot actor handler가 public `actor_context_t::join_entry_spot(...).async<T>()`로 actor join 결과를 기다리는 동안 같은 session-a connector에서 actor B fast request를 보내 먼저 완료되는 marker를 검증한다. Entry Spot에는 actor admission을 위해 fast handler만 등록하고, Entry Spot actor handler에서 `await()`를 호출하지 않는다. 최신 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-B3 passed`. |
| `ATD-C1` | done | stream connector request가 Session gateway를 거쳐 같은 Play Spot에 timer A와 timer B를 등록한다. timer A가 delay request를 `await()`로 기다리는 동안 timer B fast tick이 먼저 완료되는 marker 순서를 검증한다. timer handler coroutine이 지역 `timer_runtime_t`의 `this`를 suspension 뒤에도 읽는 수명 문제와 routed reply context 보관 문제를 정리한 뒤 반복 runner에서 통과했다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-C1 passed`. |
| `ATD-C2` | done | 같은 timer가 delay request를 `await()`로 기다리는 동안 다음 tick이 먼저 재진입하지 않고, continuation/completion 뒤에 실행되는 marker 순서를 검증한다. timer async dispatch가 coroutine frame 안에 Spot context를 보관하고 pending fire를 같은 context로 다시 게시하도록 고친 뒤 반복 runner에서 통과했다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-C2 passed`. |
| `ATD-C3` | done | actor A가 `await()` 중일 때 같은 session-a connector가 같은 Spot에 timer fast command/evidence wait를 보내 timer fast tick이 먼저 완료되는 순서와, timer가 `await()` 중일 때 같은 session-a connector에서 actor B fast request가 완료되는 순서를 검증한다. 최신 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-C3 passed`, `automatic-turn-dispatch e2e result=passed`. |
| `ATD-D1` | done | runner가 full client 실행 뒤 `play-a.evidence.log`에서 local topology marker를 직접 확인한다. `hold-completed`, `await-completed`, `worker-await-completed`, `actor-await-completed`, `timer-await-completed`, `timeout-await-completed`가 모두 `rid=play-a`로 남아야 통과한다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-D1 passed`, `automatic-turn-dispatch e2e result=passed`. |
| `ATD-D2` | done | `RemoteSpotYieldReq`가 `play-a` owner Spot에서 `play-b` target Spot으로 public `spot_context_t::request_to(...).await()`를 보내고, caller continuation marker가 `play-a`에 남는지 검증한다. spot-route async reply가 full `received_t` 대신 routing id, spot id, request seq만 보관하도록 고친 뒤 반복 runner에서 통과했다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-D2 passed`, `automatic-turn-dispatch e2e result=passed`. |
| `ATD-D3` | done | stream connector command가 Session gateway route bridge를 거쳐 `play-b` target Spot handler로 들어가고, 해당 Spot handler가 `await()` 중 probe command를 먼저 처리한 뒤 원래 continuation을 재개하는 marker 순서를 검증한다. C++ scenario는 `await-released` evidence를 확인한 뒤 probe를 보내 remote route bridge scheduling 차이로 순서 검증이 흔들리지 않게 한다. 최신 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-D3 passed`, `automatic-turn-dispatch e2e result=passed`. |
| `ATD-D4` | done | stream connector request가 Session gateway의 actor binding/relay를 거쳐 Play Entry Spot actor handler로 도달하고, actor handler가 `await()` 뒤 `actor.context.bound_session().send(...)`로 push를 보낸다. session-a의 bound connector만 `ActorPushNotify`를 받고 session-b의 unbound connector는 받지 않는 범위를 검증한다. bound session typed send가 erased serializer 경로로 빠지던 문제를 typed JSON serializer 경로로 고친 뒤 통과했다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-D4 passed`, `automatic-turn-dispatch e2e result=passed`. |
| `ATD-E1` | done | `YieldTimeoutCommand`가 Session gateway route bridge를 거쳐 Spot handler로 들어가고, delay service reply보다 짧은 timeout으로 `await()`가 public timeout error를 관찰한 뒤 같은 Spot이 `ProbeCommand`를 처리하는 marker 순서를 검증한다. 메시지별 codec 등록 없이 typed JSON serializer 경로와 session relay 분기로 처리한다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-E1 passed`, `automatic-turn-dispatch e2e result=passed`. |
| `ATD-E2` | done | 공통 E2E는 client request cancellation 또는 server-side cancellation이 `await` 대기와 continuation을 정리하고 mailbox를 풀어야 한다고 요구한다. C++는 public `cancellation_token_source_t`에서 만든 token을 `await(token)`에 넘겨 delay reply를 기다리던 작업을 `cancelled` error로 끝내고, 같은 Spot에 보낸 probe가 뒤이어 처리되는지 검증한다. 최신 full 통과 로그: `logs/20260707-151703-2374204`, 출력: `scenario ATD-E2 passed`, `automatic-turn-dispatch e2e result=passed`. |
| `ATD-E3` | done | `shutdown-wait` client scenario가 stream connector request로 long yield를 시작하고, runner가 `play-a.evidence.log`의 `await-released` marker를 확인한 뒤 play-a에 SIGTERM을 보낸다. runner의 관측 deadline은 3초이고, session 내부 spot route request는 2초 안에 public error를 stream reply로 올려 client 자체 request timeout과 경쟁하지 않게 한다. client는 request timeout이 아니라 `remote_error` 같은 public connector error를 받아야 통과한다. 같은 spot rid로 play-a를 재시작한 뒤 `shutdown-recovery` scenario가 recovery probe를 보내 routing id 재사용과 mailbox cleanup을 검증한다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `automatic-turn-dispatch shutdown wait result=passed error=remote_error`, `automatic-turn-dispatch shutdown recovery result=passed`, `automatic-turn-dispatch e2e result=passed`. |
| `ATD-E4` | done | `run_e2e.sh` static gate가 `/await` HTTP trigger/client 사용, Play Spot/Entry Spot handler 밖 `await()` 사용, client scenario thin helper 사용을 금지하고, full client가 실제 `connector_factory_t::create`로 stream connector를 만들며 각 `atd_*.hpp` scenario가 connector 참조를 직접 받는지 검사한다. 최신 full 통과 로그: `logs/20260701-191329-11276`, 출력: `automatic-turn-dispatch e2e result=passed`. |
| `ATD-E5` | done | runner가 `automatic-turn-dispatch-report.json`을 생성하고 ATD-A1~ATD-E5 scenario id와 C++ marker 이름을 검증한다. 최신 로그: `logs/20260701-191329-11276`, 출력: `scenario ATD-E5 passed`, `automatic-turn-dispatch e2e result=passed`. 여러 언어 report를 모아 비교하는 aggregation은 별도 cross-language parity gate에서 수행한다. |

## 다음 구현 기준

- 현재 slice는 Delay, Play, Session, Client target을 Redis location store로 연결하고,
  `run_e2e.sh`가 실제 stream connector client request로 scenario를 시작한다. registry role과
  registry discovery endpoint는 사용하지 않는다. 현재 Track A의
  ATD-A1~ATD-A4, Track B의 ATD-B1~ATD-B3, Track C의 ATD-C1~ATD-C3, Track D의 ATD-D1~ATD-D4, Track E의
  ATD-E1~ATD-E5 report 생성은 runner 증거가 있다.
- await 검증을 HTTP endpoint나 direct route/Spot test driver로 시작하지 않는다.
- C++ public API로 아직 확인되지 않은 항목은 내부 helper나 raw frame 우회로 메우지 않는다. 현재
  ATD-E2는 public cancellation token과 `await(token)`으로 검증되어 별도 public contract gap을 남기지 않는다.
