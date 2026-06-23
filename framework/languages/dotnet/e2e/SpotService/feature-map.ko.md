# .NET SpotService E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-2-spot-service.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| SM-A1 | 구현 | entry spot request marker가 있다. |
| SM-A2 | 구현 | user spot state mutation marker가 있다. |
| SM-A3 | 미구현 | route resolver marker가 없다. |
| SM-A4 | 구현 | owner routing marker가 있다. |
| SM-A5 | 미구현 | Stage wrapper marker가 없다. |
| SM-A6 | 미구현 | initialize/close lifecycle marker가 없다. |
| SM-A7 | 구현 | spot type mismatch marker가 있다. |
| SM-A8 | 구현 | worker offload marker가 있다. |
| SM-B1 | 구현 | local actor join marker가 있다. |
| SM-B2 | 구현 | remote actor join marker가 있다. |
| SM-B3 | 구현 | request message object fidelity marker가 있다. |
| SM-B4 | 미구현 | remote actor request marker가 없다. |
| SM-B5 | 구현 | missing actor packet marker가 있다. |
| SM-B6 | 구현 | actor leave/disconnect callback marker가 있다. |
| SM-B7 | 미구현 | actor handler execution order marker가 없다. |
| SM-B8 | 구현 | explicit actor destroy marker가 있다. |
| SM-C1 | 구현 | channel to spot messaging marker가 있다. |
| SM-C2 | 구현 | spot to channel messaging marker가 있다. |
| SM-C3 | 미구현 | spot to spot messaging marker가 없다. |
| SM-C4 | 구현 | spot publisher client marker가 있다. |
| SM-D1 | 구현 | local actor session bind/relay marker가 있다. |
| SM-D2 | 구현 | remote actor session bind/relay marker가 있다. |
| SM-D3 | 미구현 | entry spot vs user spot actor bind marker가 없다. |
| SM-D4 | 구현 | multiple actor bind marker가 있다. |
| SM-D5 | 구현 | explicit disconnect notification marker가 있다. |
| SM-D6 | 구현 | bound session push targeting marker가 있다. |
| SM-D7 | 구현 | stream auth and dispatch marker가 있다. |
| SM-D8 | 미구현 | stream reconnect marker가 없다. |
| SM-D9 | 미구현 | inbound observer marker가 없다. |
| SM-D10 | 미구현 | stream backpressure marker가 없다. |
| SM-D11 | 미구현 | stream request와 channel request 혼합 marker가 없다. |
| SM-D12 | 구현 | session reconnect migration marker가 있다. |
| SM-D13 | 미구현 | stream heartbeat marker가 없다. |
| SM-D14 | 미구현 | stream TLS marker가 없다. |
| SM-E1 | 구현 | spot route missing request marker가 있다. |
| SM-E2 | 미구현 | spot timer marker가 없다. |
| SM-E3 | 미구현 | idle timer close marker가 없다. |
| SM-E4 | 구현 | timer overrun policy marker가 있다. |
| SM-F1 | 구현 | client/server channel to target spot marker가 있다. |
| SM-F2 | 구현 | route mesh channel to target spot marker가 있다. |
| SM-F3 | 미구현 | 일반 packet과 spot route packet 혼재 marker가 없다. |
| SM-F4 | 구현 | spot route negative marker가 있다. |
| SM-F5 | 미구현 | channel socket ownership independence marker가 없다. |
| SM-G1 | 미구현 | play node crash/recovery marker가 없다. |
| SM-G2 | 미구현 | owner 이동 marker가 없다. |
| SM-G3 | 미구현 | concurrent join/leave race marker가 없다. |
| SM-G4 | 미구현 | many bound session push load marker가 없다. |
