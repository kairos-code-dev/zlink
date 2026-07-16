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
| SM-B9 | 구현 | `JoinAdmittedUserSpotActorReq`가 local/remote user spot join admission의 허용과 거부를 확인하고, 거부 actor가 user spot에 join되지 않는 evidence를 검증한다. |
| SM-C1 | 구현 | channel to spot messaging marker가 있다. |
| SM-C2 | 구현 | spot to channel messaging marker가 있다. |
| SM-C3 | 구현 | spot-to-spot request/send/publish와 missing target negative marker가 있다. |
| SM-C4 | 구현 | spot publisher client marker가 있다. |
| SM-C5 | 전환 필요 | play-a Spot의 Logical Multicast가 play-b 구독 Spot에 도달하는 evidence를 확인한다. 현재 성공 기록은 이전 topology의 증거이며 10.0.0 MeshNode 구현 뒤 다시 실행한다. |
| SM-C6 | 전환 필요 | 기본 `ConfigureSpotPublisher().NoDrop=true`에서 remote peer backpressure를 만들고, blocking publish의 전체 대상 원자적 admission과 timeout, non-blocking submit의 즉시 backpressure 결과를 검증해야 한다. 일부 대상 전달을 성공으로 처리하지 않아야 하며 현재 runner에는 이 증거가 없다. |
| SM-D1 | 구현 | local actor session bind/relay marker가 있다. |
| SM-D2 | 구현 | remote actor session bind/relay marker가 있다. |
| SM-D3 | 구현 | entry spot bind와 user spot bind를 각각 stream session에 연결하고 relay/push marker를 확인한다. |
| SM-D4 | 구현 | multiple actor bind marker가 있다. |
| SM-D5 | 구현 | explicit disconnect notification marker가 있다. |
| SM-D6 | 구현 | bound session push targeting marker가 있다. |
| SM-D7 | 구현 | stream auth and dispatch marker가 있다. |
| SM-D8 | 구현 | stream 연결 종료 중 pending request 실패를 확인하고 새 session에서 reauth/rebind 후 messaging 재개 marker를 확인한다. |
| SM-D9 | 구현 | stream inbound observer marker가 있다. |
| SM-D10 | 구현 | 작은 MaxReceivedMessages session에 push를 몰아 bounded drop을 확인하고, 같은 session request와 다른 session push가 계속 정상 동작하는지 검증한다. |
| SM-D11 | 구현 | 같은 run에서 stream actor request와 channel route request를 함께 수행하는 marker가 있다. |
| SM-D12 | 구현 | session reconnect migration marker가 있다. |
| SM-D13 | 구현 | heartbeat-enabled stream이 유지된 뒤 request가 성공하는 marker가 있다. |
| SM-D14 | 구현 | framework stream node의 `SetTlsServer(...)` public API로 TLS stream endpoint를 열고, TLS connector 성공 경로와 strict certificate validation 실패 경로를 함께 확인한다. |
| SM-D15 | 구현 | gateway role의 HTTP request가 `IZLinkActorClient.RequestToActor(...)`로 actor handler에 도달하고, actor가 bound stream session으로 push한 notify를 client가 수신하는지 확인한다. |
| SM-E1 | 구현 | spot route missing request marker가 있다. |
| SM-E2 | 구현 | spot timer tick marker가 있다. |
| SM-E3 | 구현 | idle timer가 spot close를 수행하고 closed spot request가 실패하는 marker가 있다. |
| SM-E4 | 구현 | timer overrun policy marker가 있다. |
| SM-F1 | 구현 | client/server channel to target spot marker가 있다. |
| SM-F2 | 전환 필요 | 두 MeshNode를 사용해 remote `SpotHandle` request/send가 target owner Spot에 도달하는 marker로 다시 검증한다. |
| SM-F3 | 구현 | client/server egress와 route-mesh egress를 같은 spot에 혼재해 처리한 marker가 있다. |
| SM-F4 | 구현 | missing target spot route request 실패 marker가 있다. malformed relay packet 주입은 public E2E 표면이 아니므로 직접 scenario로 만들지 않는다. |
| SM-F5 | 구현 | target user Spot을 public manager로 닫은 뒤 해당 Spot 경로만 실패하고 같은 route channel의 일반 request는 계속 성공하는 marker가 있다. |
| SM-F6 | 전환 필요 | `sm-f6` runner에서 같은 MeshName의 두 MeshNode만 사용해 remote `SpotHandle` request/send와 source Actor의 public join이 target owner Spot에 도달하는지 검증한다. 별도 Spot 전용 ROUTER나 PUB/SUB socket을 구성하지 않는다. |
| SM-G1 | 구현 | client가 play-a crash endpoint로 프로세스를 kill한 뒤 play-a bound actor request 실패, play-b 격리 유지, play-b 재bind 복구를 확인한다. |
| SM-G2 | 전환 필요 | node B를 추가한 뒤 기존 Spot·actor의 owner가 node A에 유지되는지 확인하고, node B의 capability와 Entry Spot readiness를 기다린 다음 신규 Spot·actor만 node B에 명시적으로 배치해야 한다. 자동 remap이나 기존 owner 이동을 완료 근거로 사용하지 않는다. |
| SM-G3 | 구현 | 같은 user spot에 여러 stream session이 동시에 join/request/leave를 수행하고 actor별 join/leave lifecycle evidence가 1회씩 남는지 확인한다. |
| SM-G4 | 구현 | 다수 bound session에 동시에 push를 보내 각 session이 자기 actor push만 받는지 확인한다. |
