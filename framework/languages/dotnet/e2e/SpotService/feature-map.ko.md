# .NET SpotService E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-2-spot-service.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| SM-A1 | 구현 | entry spot request marker가 있다. |
| SM-A2 | 구현 | user spot state mutation marker가 있다. |
| SM-A3 | 구현 | 특정 spot id request가 owner node evidence에만 남는 route resolver marker가 있다. |
| SM-A4 | 구현 | owner routing marker가 있다. |
| SM-A5 | 구현 | app-level ScenarioStage wrapper를 통해 spot request/timer/lifecycle을 실행하고 marker를 확인한다. |
| SM-A6 | 구현 | spot initialize와 explicit close lifecycle marker가 있다. |
| SM-A7 | 구현 | spot type mismatch marker가 있다. |
| SM-A8 | 구현 | worker offload marker가 있다. |
| SM-B1 | 구현 | local actor join marker가 있다. |
| SM-B2 | 구현 | remote actor join marker가 있다. |
| SM-B3 | 구현 | request message object fidelity marker가 있다. |
| SM-B4 | 구현 | session-a에서 play-b actor로 request 후 play-b reply marker가 있다. |
| SM-B5 | 구현 | missing actor packet marker가 있다. |
| SM-B6 | 구현 | actor leave/disconnect callback marker가 있다. |
| SM-B7 | 구현 | Created → Joined → actor packet 순서 marker가 있다. |
| SM-B8 | 구현 | explicit actor destroy marker가 있다. |
| SM-C1 | 구현 | channel to spot messaging marker가 있다. |
| SM-C2 | 구현 | spot to channel messaging marker가 있다. |
| SM-C3 | 구현 | spot-to-spot request/send/publish와 missing target negative marker가 있다. |
| SM-C4 | 구현 | spot publisher client marker가 있다. |
| SM-D1 | 구현 | local actor session bind/relay marker가 있다. |
| SM-D2 | 구현 | remote actor session bind/relay marker가 있다. |
| SM-D3 | public API/harness 대기 | stream session bind API는 ActorRef 기준이고, entry/user spot 종류를 외부에서 선택해 bind하는 public session API가 없다. |
| SM-D4 | 구현 | multiple actor bind marker가 있다. |
| SM-D5 | 구현 | explicit disconnect notification marker가 있다. |
| SM-D6 | 구현 | bound session push targeting marker가 있다. |
| SM-D7 | 구현 | stream auth and dispatch marker가 있다. |
| SM-D8 | public API/harness 대기 | stream server transport를 E2E 중 강제로 close/restart하는 harness가 없어 connector 자동 reconnect를 같은 run에서 유도할 수 없다. |
| SM-D9 | 구현 | stream inbound observer marker가 있다. |
| SM-D10 | public API/harness 대기 | framework stream gateway public API에 session push backpressure를 결정적으로 포화시키는 테스트 hook이 없다. |
| SM-D11 | 구현 | 같은 run에서 stream actor request와 channel route request를 함께 수행하는 marker가 있다. |
| SM-D12 | 구현 | session reconnect migration marker가 있다. |
| SM-D13 | 구현 | heartbeat-enabled stream이 유지된 뒤 request가 성공하는 marker가 있다. |
| SM-D14 | public API/harness 대기 | framework AddStreamNode().Bind(...)에는 TLS certificate/server option public API가 없어 framework stream TLS E2E를 구성할 수 없다. |
| SM-E1 | 구현 | spot route missing request marker가 있다. |
| SM-E2 | 구현 | spot timer tick marker가 있다. |
| SM-E3 | 구현 | idle timer가 spot close를 수행하고 closed spot request가 실패하는 marker가 있다. |
| SM-E4 | 구현 | timer overrun policy marker가 있다. |
| SM-F1 | 구현 | client/server channel to target spot marker가 있다. |
| SM-F2 | 구현 | route mesh channel to target spot marker가 있다. |
| SM-F3 | 구현 | client/server egress와 route-mesh egress를 같은 spot에 혼재해 처리한 marker가 있다. |
| SM-F4 | 구현 | spot route negative marker가 있다. |
| SM-F5 | 구현 | client/server egress host 종료 후 route-mesh egress가 같은 spot으로 계속 동작하는 marker가 있다. |
| SM-G1 | public API/harness 대기 | play node 프로세스 crash/recovery를 같은 client scenario에서 제어하는 harness가 없다. |
| SM-G2 | public API/harness 대기 | owner 이동/리밸런싱을 트리거하는 framework public API가 없다. |
| SM-G3 | public API/harness 대기 | join/leave race를 결정적으로 동기화하는 actor mailbox 테스트 hook이 없다. |
| SM-G4 | public API/harness 대기 | many bound session push 부하를 안정적으로 계측하는 load harness가 없다. |
