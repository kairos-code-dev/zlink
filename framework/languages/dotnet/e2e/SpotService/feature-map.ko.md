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
| SM-C5 | 구현 | 10.0.0 MeshNode의 play-a Spot이 발행한 Logical Multicast가 play-b 구독 Spot에 도달하는 evidence를 최신 `default-batch` 실행에서 확인했다. |
| SM-C6 | 구현 | runner가 play-b process group을 정지해 remote ROUTER backpressure를 만들고, non-blocking 즉시 결과와 blocking timeout, target별 부분 수락 개수 및 정확히 한 target의 최종 전달을 검증한다. |
| SM-D1 | 구현 | local actor session bind/relay marker가 있다. |
| SM-D2 | 구현 | remote actor session bind/relay marker가 있다. |
| SM-D3 | 구현 | entry spot bind와 user spot bind를 각각 stream session에 연결하고 relay/push marker를 확인한다. |
| SM-D4 | 구현 | multiple actor bind marker가 있다. |
| SM-D4A | 구현 | `Rebind_Fences_Stale_Relay_And_Late_Disconnect_Without_Affecting_Other_Actors`가 같은 generation의 Session A→B rebind, stale relay의 `ActorSessionNotBound`, late disconnect 무효화와 다른 Actor binding 유지를 검증한다. Focused PASS: 12/12. |
| SM-D4B | 구현 | `Bound_Actor_Relay_Does_Not_Resolve_The_Location_Store_Per_Message`와 exact route test가 bind 뒤 Store read 0과 hidden refresh 부재를 검증한다. `ActorHandoffTests`는 active forwarding과 expiry 뒤 stale 판정을 검증한다. Focused PASS: Session 12/12, handoff 30/30. |
| SM-D5 | 구현 | physical stream disconnect에서 application이 Actor 목록을 순회하지 않아도 Framework가 fixed snapshot 전체에 자동 통지한다. `Physical_Disconnect_Uses_A_Fixed_AllSettled_Snapshot_And_Cleans_Every_Binding`과 cleanup suite가 all-settled cleanup 및 Actor state 유지를 검증한다. Focused PASS: Session 12/12, cleanup 13/13. |
| SM-D5A | 실행 대기 | `sm-d5a` process runner를 추가했다. 같은 connection에 두 Actor를 bind하고 public `NotifyDisconnectedAsync` reply 뒤 선택 Actor callback 1회와 다른 Actor request 생존을 검증한다. 현재 SpotService의 선행 SpotId·terminal API 전환 gap 38개로 compile되지 않아 PASS log는 아직 없다. |
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
| SM-F2 | 구현 | 두 MeshNode를 사용한 remote `SpotHandle` request/send가 target owner Spot에 도달하는 marker를 최신 `default-batch` 실행에서 확인했다. |
| SM-F3 | 구현 | client/server egress와 route-mesh egress를 같은 spot에 혼재해 처리한 marker가 있다. |
| SM-F4 | 구현 | missing target spot route request 실패 marker가 있다. malformed relay packet 주입은 public E2E 표면이 아니므로 직접 scenario로 만들지 않는다. |
| SM-F5 | 구현 | target user Spot을 public manager로 닫은 뒤 해당 Spot 경로만 실패하고 같은 route channel의 일반 request는 계속 성공하는 marker가 있다. |
| SM-F6 | 구현 | `sm-f6` runner에서 같은 MeshName의 두 MeshNode만 사용한 remote `SpotHandle` request/send와 source Actor의 public join이 target owner Spot에 도달하는 marker를 확인했다. |
| SM-G1 | 구현 | play-a를 강제 종료한 뒤 play-b 요청이 계속 성공하고, 같은 endpoint·RID로 play-a를 재기동한 뒤 gateway와 session request/reply가 모두 복구되는 것을 확인했다. 이어 play-a를 다시 종료하고 play-b가 재배치된 actor 요청을 처리하는 경로까지 통과했다. 증거: `logs/20260720-011511-1678310/`. |
| SM-G2 | 구현 | node B 추가 뒤 기존 Spot·actor owner 유지와 신규 Spot·actor의 명시적 node B 배치를 `sm-g2` 실행에서 확인했다. |
| SM-G3 | 구현 | 같은 user spot에 여러 stream session이 동시에 join/request/leave를 수행하고 actor별 join/leave lifecycle evidence가 1회씩 남는지 확인한다. |
| SM-G4 | 구현 | 다수 bound session에 동시에 push를 보내 각 session이 자기 actor push만 받는지 확인한다. |
