# C++ YieldDispatch E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`

이 디렉터리는 Config 8 `YieldDispatch`의 C++ 포팅 위치다. 현재 C++ 트리에는 role target과
Track A YD-A1~YD-A4, Track B YD-B1~YD-B3, Track C YD-C1~YD-C3, Track D YD-D2 검증 코드가 있다. C++ framework에는 channel request,
actor join, timer, bound session send, worker call에 `yield()` 공개 표면이 있지만, Config 8 완료 기준은 실제
stream connector client request가 session gateway를 거쳐 play node의 Spot/Entry Spot handler까지
도달하는 전체 배포 경로다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `YD-A1` | done | `run_e2e.sh`가 stream connector client request -> Session gateway -> Play Spot handler -> Delay service 경로를 통과한다. 로그: `logs/20260630-093239-3426210`, 출력: `scenario YD-A1 passed`. Session host도 spot mesh를 열어 public `route_client_t` spot request overload가 route bridge를 통해 Play Spot handler로 들어간다. |
| `YD-A2` | done | 같은 runner에서 `YieldReq`가 delay outbound call을 `yield()`로 기다리는 동안 별도 stream connector가 release/evidence/probe를 확인한다. 로그: `logs/20260630-093239-3426210`, 출력: `scenario YD-A2 passed`. |
| `YD-A3` | partial | `YieldCommand`가 `yield()` 전후에 같은 request id, spot rid, correlation id를 evidence에 남기는 범위까지 검증한다. 로그: `logs/20260630-093239-3426210`, 출력: `scenario YD-A3 passed`. Spot request handler public surface가 request metadata를 직접 전달하지 않는 점은 `.NET`과 같은 public contract gap으로 남긴다. |
| `YD-A4` | done | `WorkerYieldReq` handler가 public `spot_context_t::run_worker(...)` call object를 `yield()`로 기다리는 동안 별도 stream connector가 같은 Spot에 `ProbeReq`를 보내고, worker continuation이 원래 Spot mailbox에서 재개되는 marker 순서를 검증한다. 로그: `logs/20260630-093239-3426210`, 출력: `scenario YD-A4 passed`. |
| `YD-B1` | done | stream connector request가 Session gateway의 actor binding/relay를 거쳐 Play Entry Spot actor handler로 도달한다. actor A가 delay request를 `yield()`로 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker를 검증한다. 로그: `logs/20260630-101935-3481095`, 출력: `scenario YD-B1 passed`. |
| `YD-B2` | partial | actor A가 `yield()` 중일 때 같은 actor A의 fast request가 actor continuation/completion 뒤에 실행되는 marker를 검증한다. 로그: `logs/20260630-093239-3426210`, 출력: `scenario YD-B2 passed`. 다만 C++ stream connector의 blocking submit 경로가 같은 connector의 두 번째 request를 먼저 내보내지 못해, fast request는 같은 actor ref에 bound된 별도 stream connector에서 보낸다. `.NET`의 같은 stream session 증거와 완전히 같지는 않으므로 partial로 둔다. |
| `YD-B3` | done | Entry Spot actor handler가 public `actor_context_t::join_entry_spot(...).yield<T>()`로 actor join 결과를 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker를 검증한다. 공통 문서가 허용한 `JoinEntrySpot` terminator 경로를 사용한다. 로그: `logs/20260630-101935-3481095`, 출력: `scenario YD-B3 passed`. |
| `YD-C1` | done | stream connector request가 Session gateway를 거쳐 같은 Play Spot에 timer A와 timer B를 등록한다. timer A가 delay request를 `yield()`로 기다리는 동안 timer B fast tick이 먼저 완료되는 marker 순서를 검증한다. timer handler coroutine이 지역 `timer_runtime_t`의 `this`를 suspension 뒤에도 읽는 수명 문제와 routed reply context 보관 문제를 정리한 뒤 반복 runner에서 통과했다. 로그: `logs/20260630-114500-3835073`, `logs/20260630-114512-3836412`, `logs/20260630-114544-3838681`, 출력: `scenario YD-C1 passed`. |
| `YD-C2` | done | 같은 timer가 delay request를 `yield()`로 기다리는 동안 다음 tick이 먼저 재진입하지 않고, continuation/completion 뒤에 실행되는 marker 순서를 검증한다. timer async dispatch가 coroutine frame 안에 Spot context를 보관하고 pending fire를 같은 context로 다시 게시하도록 고친 뒤 반복 runner에서 통과했다. 로그: `logs/20260630-114500-3835073`, `logs/20260630-114512-3836412`, `logs/20260630-114544-3838681`, 출력: `scenario YD-C2 passed`. |
| `YD-C3` | partial | actor A가 `yield()` 중일 때 timer fast tick이 완료되는 순서와, timer가 `yield()` 중일 때 actor B fast request가 완료되는 순서를 모두 검증한다. 로그: `logs/20260630-103612-3549306`, 출력: `scenario YD-C3 passed`. 다만 actor-yield 중 timer-fast half는 C++ stream connector의 pending request 직렬화 한계를 피하려고 같은 session에 actor binding을 공유한 observer connector에서 timer command를 보낸다. `.NET`의 같은 stream session 증거와 완전히 같지는 않으므로 partial로 둔다. |
| `YD-D1` | 미구현 | local play/delay topology에서 Track A~C marker를 검증하는 C++ runner가 아직 없다. |
| `YD-D2` | done | `RemoteSpotYieldReq`가 `play-a` owner Spot에서 `play-b` target Spot으로 public `spot_context_t::request_to(...).yield()`를 보내고, caller continuation marker가 `play-a`에 남는지 검증한다. spot-route async reply가 full `received_t` 대신 routing id, spot id, request seq만 보관하도록 고친 뒤 반복 runner에서 통과했다. 로그: `logs/20260630-114500-3835073`, `logs/20260630-114512-3836412`, `logs/20260630-114544-3838681`, 출력: `scenario YD-D2 passed`, `yield-dispatch track-a-d result=passed`. |
| `YD-D3` | 미구현 | route bridge를 거쳐 들어온 Spot handler의 yield 동작을 검증하는 C++ scenario가 아직 없다. |
| `YD-D4` | 미구현 | stream session actor relay와 bound session push 격리를 검증하는 C++ scenario가 아직 없다. |
| `YD-E1` | 미구현 | yield timeout 뒤 같은 Spot mailbox cleanup을 검증하는 C++ scenario가 아직 없다. |
| `YD-E2` | 미구현 | yield cancellation 뒤 continuation/catch/cleanup 순서를 검증하는 C++ scenario가 아직 없다. |
| `YD-E3` | 미구현 | yield pending 중 play node shutdown과 recovery를 검증하는 C++ runner가 아직 없다. |
| `YD-E4` | 미구현 | HTTP trigger 금지, handler 밖 `yield()` 금지, connector 직접 사용 같은 C++ 정적 검사가 아직 없다. |
| `YD-E5` | gap | cross-language report 집계 단계는 C++ 단독 포팅 밖의 후속 parity 작업이다. `.NET`도 부분 구현으로 남긴다. |

## 다음 구현 기준

- 첫 구현 slice는 `.NET` 구조처럼 Registry, Delay, Play, Session, Client target을 나누고,
  `run_e2e.sh`가 실제 stream connector client request로 scenario를 시작한다. 현재 Track A의
  YD-A1~YD-A4, Track B의 YD-B1~YD-B3, Track C의 YD-C1~YD-C3, Track D의 YD-D2는 runner 증거가 있다.
  다음 slice는 파일 분리 또는 남은 YD-D/E scenario 추가 작업이다.
- yield 검증을 HTTP endpoint나 direct route/Spot test driver로 시작하지 않는다.
- C++ public API로 아직 확인되지 않은 metadata/context 항목은 내부 helper나 raw frame 우회로 메우지 않고
  별도 public contract gap으로 유지한다.
