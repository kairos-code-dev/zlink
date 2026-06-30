# .NET YieldDispatch E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| YD-A1 | 구현 | stream connector request가 session gateway를 거쳐 play 노드에 도달한다. `HoldReq`가 delay service request를 기본 terminator로 기다리고, 같은 Spot의 `ProbeReq`가 completion 뒤 실행되는 marker를 검증한다. |
| YD-A2 | 구현 | stream connector request가 session gateway를 거쳐 play 노드에 도달한다. `YieldReq`가 `RequestToChannel(...).Yield<TReply>()`로 기다리는 동안 같은 Spot의 `ProbeReq`가 먼저 실행되는 marker를 검증한다. |
| YD-A3 | 구현 | request id, spot rid, correlation id, yield continuation marker order를 검증한다. stream metadata 직접 노출은 Spot request handler public surface가 아니므로 이 시나리오의 완료 조건에 넣지 않는다. |
| YD-A4 | 구현 | stream connector request가 session gateway를 거쳐 play 노드에 도달한다. `RunWorker(...).Yield(...)`가 worker 완료를 기다리는 동안 Spot turn을 반납하고, continuation이 원래 Spot mailbox에서 재개되는 marker를 검증한다. |
| YD-B1 | 구현 | stream connector request가 session gateway에서 actor relay로 이어진다. actor A가 `RequestToChannel(...).Yield<TReply>()`로 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker를 검증한다. |
| YD-B2 | 구현 | 같은 stream session actor relay 경로에서 actor A의 fast request가 actor A의 yield continuation과 completion 뒤에 실행되는 marker를 검증한다. entry spot actor request handler가 actor mailbox를 통과하는 runtime 회귀도 함께 검증한다. |
| YD-B3 | 구현 | actor A handler가 `JoinSpot(...).Yield(...)`로 user spot join을 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker를 검증한다. |
| YD-C1 | 구현 | stream connector request가 timer scenario를 시작한다. timer A가 `RequestToChannel(...).Yield<TReply>()`로 기다리는 동안 timer B tick이 먼저 완료되는 marker를 검증한다. |
| YD-C2 | 구현 | 같은 timer의 다음 tick이 이전 yield tick의 continuation과 completion 뒤에 실행되는 marker를 검증한다. |
| YD-C3 | 구현 | 같은 stream connector session에서 actor bind/relay를 먼저 만든 뒤, actor A가 yield 중일 때 같은 Spot의 timer fast tick이 완료되는 순서와 timer yield 중 actor B fast request가 완료되는 순서를 모두 검증한다. scenario 시작과 교차 request는 session gateway connector request로 들어간다. |
| YD-D1 | 구현 | `play-a`와 `delay-a`의 local topology에서 A/B/C/E1 marker를 검증한다. 모든 scenario 시작 packet은 stream connector에서 session gateway로 들어간다. |
| YD-D2 | 구현 | `play-a` Spot handler가 `RequestToSpot(...).Yield<TReply>()`로 `play-b` target Spot을 기다린다. `play-a`에는 owner continuation marker가 남고, `play-b`에는 target Spot handler marker만 남는 것을 검증한다. |
| YD-D3 | 구현 | stream connector request가 session gateway를 거쳐 `play-b` route mesh control로 relay되고, `play-b`가 target Spot route request handler에서 `RequestToChannel(...).Yield<TReply>()`를 수행한다. yield 중 같은 target Spot의 probe가 먼저 실행되는 marker도 검증한다. |
| YD-D4 | 구현 | actor request는 실제 stream session에서 actor bind/relay로 play 노드에 도달한다. actor handler가 yield 뒤 bound session으로 push를 보내고, session-b의 bind하지 않은 connector에는 push가 오지 않는 것을 검증한다. |
| YD-E1 | 구현 | stream connector request가 session gateway를 거쳐 play 노드에 도달한다. `RequestToChannel(...).Timeout(...).Yield<TReply>()` timeout 뒤 같은 Spot mailbox가 `ProbeReq`를 처리하는 marker를 검증한다. |
| YD-E2 | 구현 | server-side cancellation token이 `Yield<TReply>(token)` 대기를 취소하고, cancellation catch와 completion marker가 원래 Spot mailbox에서 이어진 뒤 같은 Spot mailbox가 post-cancel `ProbeReq`를 처리하는 marker를 검증한다. 이 scenario가 `ZLinkSerialTurn`의 yield cancellation 회귀를 잡는다. |
| YD-E3 | 구현 | `shutdown-wait` client scenario가 stream connector request로 long yield를 시작하고, runner가 play-a evidence의 `yield-released` marker를 확인한 뒤 play-a를 SIGTERM으로 정상 종료한다. client는 timeout이 아니라 closed/cancelled 계열 public error를 받아야 통과한다. 같은 spot rid로 play-a를 재시작한 뒤 `shutdown-recovery` client scenario가 connector request로 recovery probe를 보내 routing id 재사용과 mailbox cleanup을 검증한다. |
| YD-E4 | 구현 | runner가 HTTP trigger/client 사용, Play Spot/Entry Spot handler 밖의 `.Yield` 사용, connector를 만들지 않는 client scenario 파일을 정적으로 검사한다. |
| YD-E5 | 구현 | .NET report는 공통 scenario id와 marker 이름을 사용한다. 여러 framework 언어의 Config 8 report를 한 번에 모아 비교하는 집계 단계는 별도 cross-language parity gate에서 수행한다. |
