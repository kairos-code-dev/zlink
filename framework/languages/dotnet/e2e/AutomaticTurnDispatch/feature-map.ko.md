# .NET AutomaticTurnDispatch E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-automatic-turn-dispatch.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| ATD-A1 | 구현 | request, actor join, worker call이 `.Async(...)` 하나만 완료 terminator로 제공하는지 contract test로 확인하고, 배포 fixture의 `HoldReq`가 같은 표면으로 정상 완료되는 marker를 검증한다. |
| ATD-A2 | 구현 | stream connector request가 session gateway를 거쳐 play 노드에 도달한다. `AwaitReq`가 `RequestToChannel(...).Async<TRes>()`로 기다리는 동안 같은 Spot의 `ProbeReq`가 먼저 실행되는 marker를 검증한다. |
| ATD-A3 | 구현 | request id, spot rid, correlation id, await continuation marker order를 검증한다. stream metadata 직접 노출은 Spot request handler public surface가 아니므로 이 시나리오의 완료 조건에 넣지 않는다. |
| ATD-A4 | 구현 | stream connector request가 session gateway를 거쳐 play 노드에 도달한다. `RunWorker(...).Async(...)`가 worker 완료를 기다리는 동안 Spot turn을 반납하고, continuation이 원래 Spot mailbox에서 재개되는 marker를 검증한다. |
| ATD-B1 | 구현 | stream connector request가 session gateway에서 actor relay로 이어진다. actor A가 `RequestToChannel(...).Async<TRes>()`로 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker를 검증한다. |
| ATD-B2 | 구현 | 같은 stream session actor relay 경로에서 actor A의 fast request가 actor A의 await continuation과 completion 뒤에 실행되는 marker를 검증한다. entry spot actor request handler가 actor mailbox를 통과하는 runtime 회귀도 함께 검증한다. |
| ATD-B3 | 구현 | actor A handler가 `JoinSpot(...).Async(...)`로 user spot join을 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker를 검증한다. |
| ATD-C1 | 구현 | stream connector request가 timer scenario를 시작한다. timer A가 `RequestToChannel(...).Async<TRes>()`로 기다리는 동안 timer B tick이 먼저 완료되는 marker를 검증한다. |
| ATD-C2 | 구현 | 같은 timer의 다음 tick이 이전 await tick의 continuation과 completion 뒤에 실행되는 marker를 검증한다. |
| ATD-C3 | 구현 | 같은 stream connector session에서 actor bind/relay를 먼저 만든 뒤, actor A가 await 중일 때 같은 Spot의 timer fast tick이 완료되는 순서와 timer await 중 actor B fast request가 완료되는 순서를 모두 검증한다. scenario 시작과 교차 request는 session gateway connector request로 들어간다. |
| ATD-D1 | 구현 | `play-a`와 `delay-a`의 local topology에서 A/B/C/E1 marker를 검증한다. 모든 scenario 시작 packet은 stream connector에서 session gateway로 들어간다. |
| ATD-D2 | 구현 | `play-a` Spot handler가 `RequestToSpot(...).Async<TRes>()`로 `play-b` target Spot을 기다린다. `play-a`에는 owner continuation marker가 남고, `play-b`에는 target Spot handler marker만 남는 것을 검증한다. |
| ATD-D3 | 구현 | stream connector request가 session gateway를 거쳐 `play-b` route mesh control로 relay되고, `play-b`가 target Spot route request handler에서 `RequestToChannel(...).Async<TRes>()`를 수행한다. await 중 같은 target Spot의 probe가 먼저 실행되는 marker도 검증한다. |
| ATD-D4 | 구현 | actor request는 실제 stream session에서 actor bind/relay로 play 노드에 도달한다. actor handler가 await 뒤 bound session으로 push를 보내고, session-b의 bind하지 않은 connector에는 push가 오지 않는 것을 검증한다. |
| ATD-E1 | 구현 | stream connector request가 session gateway를 거쳐 play 노드에 도달한다. `RequestToChannel(...).Timeout(...).Async<TRes>()` timeout 뒤 같은 Spot mailbox가 `ProbeReq`를 처리하는 marker를 검증한다. |
| ATD-E2 | 구현 | server-side cancellation token이 `Await<TRes>(token)` 대기를 취소하고, cancellation catch와 completion marker가 원래 Spot mailbox에서 이어진 뒤 같은 Spot mailbox가 post-cancel `ProbeReq`를 처리하는 marker를 검증한다. 이 scenario가 `ZLinkSerialTurn`의 await cancellation 회귀를 잡는다. |
| ATD-E3 | 구현 | `shutdown-wait`가 long-running request의 `await-released` marker 뒤 play-a를 종료한다. session gateway가 client timeout보다 짧은 downstream deadline으로 public remote error를 반환하며, client-side `RequestTimeout`은 성공으로 인정하지 않는다. play-a 재시작 뒤 같은 Spot handle이 refresh되어 recovery probe가 통과하는지도 검증한다. |
| ATD-E4 | 구현 | runner가 HTTP trigger/client 사용, Play Spot/Entry Spot handler 밖의 `.Await` 사용, connector를 만들지 않는 client scenario 파일을 정적으로 검사한다. |
| ATD-E5 | 구현 | .NET report는 공통 scenario id와 marker 이름을 사용한다. 여러 framework 언어의 Config 8 report를 한 번에 모아 비교하는 집계 단계는 별도 cross-language parity gate에서 수행한다. |
