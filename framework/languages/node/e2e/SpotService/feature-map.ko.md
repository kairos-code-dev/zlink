# Node.js SpotService E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-2-spot-service.ko.md`

현재 상태: Node.js `SpotService` config는 `.NET` runner처럼 `all`을 child group으로 나누어 실행한다.
`default-batch`는 SM-A1, SM-A2, SM-A3, SM-A4, SM-A5, SM-A6, SM-A7, SM-A8, SM-B1, SM-B2, SM-B3, SM-B4, SM-B5,
SM-B6, SM-B7, SM-B8, SM-B9, SM-C1, SM-C2, SM-C3, SM-C4, SM-C5, SM-D1, SM-D2, SM-D3, SM-D4, SM-D5, SM-D6,
SM-D7, SM-D8, SM-D9, SM-D10, SM-D11, SM-D12, SM-D13, SM-D14, SM-D15, SM-E1, SM-E2, SM-E3, SM-E4, SM-F1,
SM-F2, SM-F3, SM-F4, SM-F5를 operation group 단위로 실행하고,
outer `all`은 이어서 SM-F6, SM-G2, SM-G3, SM-G4, SM-G1을 별도 child scenario로 실행한다.
공통 Config 2에 없는 SM-Q9는 보조 operation으로만 선택 실행한다.
SM-F4는 존재하지 않는 location의 request 실패를 선택 scenario로 검증했다. malformed relay packet 주입은 public route-client 표면으로 만들 수 없으므로 public E2E 직접 대상에서 제외한다. 이 문서는 `.NET`
`framework/languages/dotnet/e2e/SpotService/feature-map.ko.md`와 공통 문서의 scenario ID를 기준으로
포팅 범위를 고정한다. 내부 helper나 raw-frame 우회로 gap을 완료 표시하지 않는다.
서버 역할은 `E2E_START_ORDER=reverse`와 고정 seed `shuffle:20260715`로도 시작하며, 선택된 순서로
모든 역할을 시작한 뒤 readiness를 확인한다. 두 변형의 `SM-A1` runner가 통과했다.
`.NET`의 `SmQ9Scenario.cs`는 공통 문서에 없는 보조 operation이므로 scenario 표가 아니라
`porting-inventory.ko.md`의 보조 항목에서 추적한다. Node.js에는 MultiNode role과 선택 operation을
추가했고 public route-to-spot request가 각 local owner spot으로 도달하는지 검증했다. `all` PASS: `logs/20260702-064908-43296`

| Scenario | 상태 | 근거 |
|----------|------|------|
| SM-A1 | 구현 | play-a의 public spot manager endpoint `/spot/create`가 requested spot rid를 생성하고 `create-spot` evidence marker를 남기는지 검증했다. 로그: `logs/20260630-074201-3148526` |
| SM-A2 | 구현 | user spot 생성 뒤 public `/spot/state/request` endpoint가 `ZLinkSpotOutbound.requestToSpot(...)`와 resolver 기반 route로 `StateReq`를 전달하고 state/evidence를 검증한다. 선택 PASS: `logs/20260630-082158-3260793` |
| SM-A3 | 구현 | play-a에 unique user spot을 만들고 public routed `StateReq`가 play-a spot에만 도달하는지 검증한다. 선택 PASS: `logs/20260630-081839-3241783` |
| SM-A4 | 구현 | owner spot을 만든 뒤 같은 key가 가리키는 spot rid로 routed `StateReq` noop request를 보내 owner routing이 play-a에 머무는지 검증한다. 선택 PASS: `logs/20260630-081825-3239853` |
| SM-A5 | 구현 | app-level `ScenarioStage` wrapper, `StageProbeReq`, `StageTimerStartMsg`, stage timer handler가 routed readiness request 뒤 같은 spot serial 흐름에서 동작하는지 검증한다. 선택 PASS: `logs/20260630-081839-3241830` |
| SM-A6 | 구현 | `/spot/create`와 `/spot/close`가 public spot manager create/close 경로를 사용하고 `spot-initialize`/`spot-closing` lifecycle marker를 검증한다. 로그: `logs/20260630-074201-3148526` |
| SM-A7 | 구현 | 같은 spot rid를 `ScenarioUserSpot`으로 만든 뒤 `ScenarioAlternateSpot`으로 다시 `getOrCreate`해 public `SpotTypeMismatch` error와 evidence marker를 검증한다. 로그: `logs/20260630-074201-3148526` |
| SM-A8 | 구현 | public `executeOnSpot(...)` 안에서 `spot.context.runIoWorker(...).yield()`를 시작하고, I/O completion 전 다른 `executeOnSpot(...)` state turn이 처리되는 evidence 순서를 검증한다. 로그: `logs/20260715-092022-2650621` |
| SM-B1 | 구현 | Session role, stream connector `AuthReq`, Play control RouteMesh `EnsureActorReq`, local actor bind, `ActorPingReq` relay, `entry-created` -> `entry-joined` lifecycle evidence를 검증한다. 선택 PASS: `logs/20260630-070738-3054201` |
| SM-B2 | 구현 | Session role에서 `play-b` control route로 actor ensure를 보내고, bound actor request가 `play-b` spot actor까지 cross-node relay되며 `entry-created` -> `entry-joined` lifecycle evidence가 remote node에 남는지 검증한다. 선택 PASS: `logs/20260630-070751-3054866` |
| SM-B3 | 구현 | Session stream auth 뒤 bound actor에 `ComplexActorReq`를 relay하고 scalar, array, dictionary payload가 reply와 `actor-complex` evidence에 그대로 남는지 검증한다. 선택 PASS: `logs/20260629-201946-1303393`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-B4 | 구현 | Session stream auth가 `play-b` actor를 bind한 뒤 `ActorPingReq`가 cross-node로 `play-b` actor에서 처리되고 reply가 돌아오는지 검증한다. 선택 PASS: `logs/20260629-202300-1319449`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-B5 | 구현 | missing actor handler request가 stream error reply로 실패하고 `dispatch-error|surface=spotActor|kind=actorRequest|reason=handlerMissing|action=replyError` evidence를 남기는지 검증한다. 선택 PASS: `logs/20260630-072224-3098758` |
| SM-B6 | 구현 | explicit leave는 `spot-actor-left` evidence만 남기고 disconnect evidence를 남기지 않으며, stream close는 Session `onDisconnected`와 선택 actor `entry-disconnected` evidence를 남기는지 검증한다. 선택 PASS: `logs/20260630-073618-3133487`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-B7 | 구현 | 같은 actor에 `ActorPingReq` 두 개를 연속 전송해 `entry-created` -> `entry-joined` -> packet dispatch 순서와 `seen=1`, `seen=2` 직렬 처리 evidence를 검증한다. 선택 PASS: `logs/20260630-070802-3055574` |
| SM-B8 | 구현 | Entry Spot actor handler에서 public `entrySpot.context.destroyActor(...)`를 호출하고, post-destroy `SnapshotReq`가 stream error reply로 실패하며 `actor-destroyed` evidence가 남는지 검증한다. 선택 PASS: `logs/20260630-072210-3097534` |
| SM-B9 | 구현 | Entry Spot `onActorJoin(...)`이 actor id별 admission 결과를 반환하고 거부 actor는 request가 stream error reply로 실패하며 accepted actor는 정상 reply되는지 검증한다. 선택 PASS: `logs/20260707-195152-3345108` |
| SM-C1 | 구현 | public `ZLinkSpotOutbound.requestToSpot(...)`/`sendToSpot(...)`로 channel -> spot request, command, slow request timeout, timeout 이후 정상 request를 검증한다. 선택 PASS: `logs/20260630-081924-3247169` |
| SM-C2 | 구현 | Spot handler가 public `requestToChannel(...)`, `sendToChannel(...)`, `publish(...)`를 사용해 channel echo, channel notify, spot event publish를 모두 evidence로 남긴다. missing channel request/send negative도 message-flow observer evidence로 검증한다. 선택 PASS: `logs/20260629-205741-1493698`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-C3 | 구현 | spot -> spot request/send/publish/timeout/negative handlers를 public `ZLinkSpotOutbound.requestToSpot(...)`, `sendToSpot(...)`, `publish(...)`로 검증한다. 선택 PASS: `logs/20260630-082100-3253716` |
| SM-C4 | 구현 | local spot factory가 없는 Gateway role이 public `ZLinkSpotPublisherClient.publishSpot(...)`로 publish하고, Play-A subscribed spot이 같은 marker의 `SpotMsg`를 받으며 미구독 alternate spot은 받지 않는지 검증한다. Play-A가 Gateway publish endpoint에 `connectPeerPub(...)`로 붙는 `.NET` 기준 topology를 사용한다. PASS: `logs/20260630-083734-3303700` |
| SM-C5 | 구현 | Play-A와 Play-B가 같은 SpotMesh publish를 받아 cross-node subscribed spot delivery evidence를 남기고 미구독 spot에는 delivery가 없는지 검증한다. 선택 PASS: `logs/20260707-195152-3345108` |
| SM-D1 | 구현 | Session HTTP endpoint가 control RouteMesh로 `play-a` readiness를 확인하고, stream auth로 bind한 local actor에 `ActorPushReq`를 relay한다. actor handler는 public `actor.context.boundSession.send(...)`로 같은 stream client에 `ActorPushNotify`를 보내고 reply와 push payload를 검증한다. 선택 PASS: `logs/20260629-211928-1555127`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D2 | 구현 | Session HTTP endpoint가 control RouteMesh로 `play-b` readiness를 확인하고, stream auth로 bind한 remote actor에 `ActorPushReq`를 relay한다. `play-b` actor handler가 public bound session push로 `ActorPushNotify`를 보내고 reply node가 `play-b`인지 검증한다. 선택 PASS: `logs/20260629-212246-1563843`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D3 | 구현 | entry actor stream bind 뒤 `ActorPushReq` reply와 bound session push를 검증한다. user spot bind는 `UserSpotAuthReq`로 spot/actor join marker를 남기고 `UserActorPingReq`/`UserActorPushReq` relay reply, user spot rid, push payload, `actor-pingMsg` evidence를 검증한다. 선택 PASS: `logs/20260629-212739-1577626`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D4 | 구현 | `MultiBindReq`가 한 stream session에 두 actor를 bind하고, subsequent request가 stream metadata `actor-id`로 대상 actor를 선택한다. 각 actor request/reply, actor push, id 없는 request 실패를 검증한다. 선택 PASS: `logs/20260629-213206-1588322`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D5 | 구현 | `session-a` local actor stream auth와 bind 뒤 stream close가 Session `onDisconnected`를 호출하고, handler가 선택 actor에 `notifyDisconnected()`를 호출해 `entry-disconnected` evidence를 남기는지 검증한다. 선택 PASS: `logs/20260630-073619-3133519`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D6 | 구현 | bound consumer와 별도 consumer를 각각 `session-a`, `session-b` stream session에 연결하고, `ActorPushReq`로 발생한 `ActorPushNotify`가 target actor에 bind된 consumer에게만 전달되는지 검증한다. 별도 consumer는 다른 actor에 bind되어 있으며 target actor push count가 0인지 확인한다. 선택 PASS: `logs/20260629-213945-1613927`; `all` PASS: `logs/20260702-064908-43303` |
| SM-D7 | 구현 | stream connector가 `AuthReq`로 actor bind를 완료하고, 같은 stream의 `ActorPingReq`가 bound actor로 dispatch되어 reply payload가 유지되는지 검증한다. 선택 PASS: `logs/20260629-214310-1624231`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D8 | 구현 | slow actor request가 pending인 상태에서 stream connector를 close하면 pending request가 실패하고 자동 재전송되지 않는지 확인한다. 이후 새 stream connector가 같은 actor id로 다시 auth/rebind하고 `ActorPingReq`가 정상 reply되는지 검증한다. 선택 PASS: `logs/20260629-214843-1639970`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D9 | 구현 | stream connector에 public `observeInbound(...)`를 `connect()` 전에 등록하고, stream auth 뒤 두 번의 `ActorPingReq` reply를 받는 동안 inbound response frame의 kind, request sequence, payload length가 관측되는지 검증한다. 선택 PASS: `logs/20260629-215409-1654253`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D10 | 구현 | `maxReceivedMessages=1` stream client에 느린 `ActorPushNotify` handler를 등록해 bounded queue overflow를 만들고 `ReceivedMessageDropped` error signal, request route 생존, 다른 actor stream의 push 격리를 검증한다. Node stream connector는 queue가 가득 차면 새 send frame을 drop하므로 `.NET`의 newest-retained 단언 대신 공개 Node 계약에 맞춰 drop signal과 격리를 확인한다. 선택 PASS: `logs/20260629-215844-1670329`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D11 | 구현 | 같은 client flow에서 stream `ActorPingReq`와 Session HTTP channel control-pingMsg을 차례로 호출해 stream reply와 channel reply가 서로 간섭 없이 각 경로로 돌아오는지 검증한다. 선택 PASS: `logs/20260629-220146-1678135`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D12 | 구현 | runner가 `session-a`와 별도 `session-b` stream host를 함께 띄우고, client가 `session-a`에서 actor state를 만든 뒤 close하고 `session-b`로 재auth/rebind한다. 이후 `SnapshotReq`와 `ActorPushReq`로 play-a actor state가 보존되고 push가 새 stream으로 돌아오는지 검증한다. 선택 PASS: `logs/20260629-220618-1691419`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D13 | 구현 | stream connector heartbeat를 public option으로 켜고 200ms interval, 2s timeout으로 여러 heartbeat 주기 동안 stream auth 상태가 유지되는지 검증한다. 현재 `.NET` 기준 scenario와 동일하게 정상 heartbeat 유지 경로만 완료로 본다. 선택 PASS: `logs/20260629-221012-1704641`; `all` PASS: `logs/20260630-074201-3148526` |
| SM-D14 | 구현 | Node stream node builder의 public `setTlsServer(...)`가 stream socket bind 전에 server certificate/key를 적용한다. `tls://` stream endpoint에서 strict certificate validation 실패와 `skipServerCertificateValidation` 성공 경로의 auth/request/push를 검증했다. 선택 PASS: `logs/20260630-085904-3356699`; `all` PASS: `logs/20260630-101424-3467655` |
| SM-D15 | 구현 | Session -> actor -> bound session push chain이 cross-role로 이어져 request reply와 push payload가 같은 actor/session evidence로 확인되는지 검증한다. 선택 PASS: `logs/20260707-195152-3345108`; `all` PASS: `logs/20260708-062031-351969` |
| SM-E1 | 구현 | public `setMessageFlowObserver(...)`, `ZLinkSpotOutbound.requestToSpot(...)`, `sendToSpot(...)`로 missing handler request/command 경로를 만들고 SpotRoute `handlerMissing` evidence를 검증한다. 선택 PASS: `logs/20260630-082045-3252646` |
| SM-E2 | 구현 | public `spot.context.addTimer(...)`로 lifecycle timer를 등록하고 `timer-basic` marker가 두 번 이상 발생한 뒤 spot close가 성공하는지 검증한다. 로그: `logs/20260630-074201-3148526` |
| SM-E3 | 구현 | public `spot.context.addTimer(...)`와 `spot.context.close(...)`로 idle timer close, `spot-closing` evidence, close 이후 routed request 실패를 검증한다. 선택 PASS: `logs/20260630-081940-3248210` |
| SM-E4 | 구현 | public `spot.context.addTimer(...)`의 `ZLinkTimerOverrunPolicy` 세 가지를 등록하고 `delivery`/`scheduled`/`skipped` evidence로 skip, bounded catch-up, delay-next-tick 의미를 검증한다. 로그: `logs/20260630-074201-3148526` |
| SM-F1 | 구현 | target spot을 만든 뒤 route client 경로의 `/spot/state/request`와 `/spot/state/command`를 검증한다. 선택 PASS: `logs/20260630-082100-3253769` |
| SM-F2 | 구현 | target spot request/command selectable scenario가 public route-to-spot path로 state reply와 command evidence를 검증한다. 선택 PASS: `logs/20260630-082100-3253756` |
| SM-F3 | 구현 | `play-b`가 `play-a`의 same RouteMesh channel에 public route client로 일반 `ChannelEchoReq`를 보내고, 같은 channel의 target spot route로 `StateReq`를 보낸다. 일반 channel handler와 target spot handler evidence가 모두 `play-a`에 남는지 검증한다. 선택 PASS: `logs/20260630-091213-3386438`; `all` PASS: `logs/20260630-101424-3467655` |
| SM-F4 | 구현 | 존재하지 않는 location의 request 실패를 selectable scenario로 검증했다. malformed relay packet 주입은 public route-client 표면이 아니므로 runtime 내부 검증이나 별도 bridge-level 테스트 대상으로 분리한다. `all` PASS: `logs/20260713-063253-3989258` |
| SM-F5 | 구현 | live `.NET` tree에는 `Client/Scenarios/SmF5Scenario.cs`가 없지만 공통 E2E의 channel socket 소유권 독립 요구를 public spot close 경로로 검증했다. `play-b`가 `play-a`의 same RouteMesh channel로 일반 `ChannelEchoReq`와 target spot `StateReq`를 보낸 뒤, `play-a`에서 해당 spot을 public `ZLinkSpotManager.close(...)`로 닫고 같은 channel의 일반 `ChannelEchoReq`가 계속 성공하는지 확인한다. 선택 PASS: `logs/20260630-091846-3399628`; `all` PASS: `logs/20260630-101424-3467655` |
| SM-F6 | 구현 | RouteMesh 없이 SpotMesh만 구성한 MultiNode role에서 target spot request가 owner spot으로 도달하고 reply/evidence가 남는지 검증한다. 선택 PASS: `logs/20260707-195217-3346296`; `all` PASS: `logs/20260708-062057-353085` |
| SM-G1 | 구현 | stream auth로 `play-a`/`play-b` actor를 각각 bind하고, `play-a` `/crash` endpoint로 프로세스를 종료한 뒤 `play-a` actor request 실패, `play-b` survivor request 유지, `session-b`에서 `play-b`로 재auth/rebind 복구를 검증했다. 선택 PASS: `logs/20260629-223922-1778101`; `all` PASS: `logs/20260708-062230-357711` |
| SM-G2 | 구현 | logical key의 owner spot을 `play-a`와 `play-b`에 각각 만들고 owner remap 전후 routed `StateReq`가 올바른 node로 도달하는지 검증한다. 선택 PASS: `logs/20260708-062006-350816`; `all` PASS: `logs/20260708-062108-353704` |
| SM-G3 | 구현 | 같은 user spot에 두 stream client를 연결해 `UserSpotAuthReq`, concurrent `UserActorPingReq`, `LeaveReq`를 실행하고 actor별 `spot-actor-joined`/`spot-actor-left` evidence가 1회씩 남는지 검증했다. 선택 PASS: `logs/20260629-224535-1792721`; `all` PASS: `logs/20260708-062135-354732` |
| SM-G4 | 구현 | 여섯 stream client를 각각 다른 actor에 bind한 뒤 `.NET` 기준처럼 순차적으로 `ActorPushReq`를 보내 reply와 `ActorPushNotify`가 각 actor/session으로만 돌아오는지 검증했다. 선택 PASS: `logs/20260629-225216-1811400`; `all` PASS: `logs/20260708-062201-355741` |

## 후속 계약 판정

| 묶음 | Scenario | 판정 | 다음 작업 |
|------|----------|------|-----------|
| routed spot request | `SM-A2`, `SM-A3`, `SM-A4`, `SM-A5`, `SM-C1`, `SM-C3`, `SM-E1`, `SM-E3`, `SM-G2`, `sm-q9` | 구현 | Node spot spec의 Spot context outbound와 resolver 기반 route를 기존 public surface로 검증했다. 선택 PASS 로그는 각 scenario 행에 남겼다. |
| route client target spot | `SM-F1`, `SM-F2`, `SM-F3`, `SM-F4`, `SM-F5` | 구현 | `SM-F1`/`SM-F2` target spot request/command, `SM-F3` same RouteMesh 일반 request와 target spot route 혼재, `SM-F4` 존재하지 않는 location의 request 실패, `SM-F5` spot close 뒤 same RouteMesh 일반 channel request 생존을 public route-client와 spot 관리 표면으로 검증했다. |
| stream TLS server | `SM-D14` | 구현 | Node stream node builder의 public `setTlsServer(...)`와 stream connector의 TLS validation option으로 self-signed TLS endpoint의 reject/accept 경로를 검증했다. PASS: `logs/20260630-085904-3356699`; `all` PASS: `logs/20260630-101424-3467655` |

검증:

- `timeout 1200s framework/languages/node/e2e/SpotService/run_e2e.sh`
  - PASS: `logs/20260708-062031-351962` (`default-batch`, `SM-F6`, `SM-G2`, `SM-G3`, `SM-G4`, `SM-G1`, `SM-Q9`; 당시 SM-Q9 포함)
  - `default-batch` child PASS: `logs/20260708-062031-351969` (`SM-A1`, `SM-A2`, `SM-A3`, `SM-A4`, `SM-A5`, `SM-A6`, `SM-A7`, `SM-A8`, `SM-B1`, `SM-B2`, `SM-B3`, `SM-B4`, `SM-B5`, `SM-B6`, `SM-B7`, `SM-B8`, `SM-B9`, `SM-C1`, `SM-C2`, `SM-C3`, `SM-C4`, `SM-C5`, `SM-D1`, `SM-D2`, `SM-D3`, `SM-D4`, `SM-D5`, `SM-D6`, `SM-D7`, `SM-D8`, `SM-D9`, `SM-D10`, `SM-D11`, `SM-D12`, `SM-D13`, `SM-D14`, `SM-D15`, `SM-E1`, `SM-E2`, `SM-E3`, `SM-E4`, `SM-F1`, `SM-F2`, `SM-F4`)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-B1`
  - PASS: `logs/20260630-070738-3054201` (`entry-created`, `entry-joined`, actor pingMsg evidence 확인)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-A3`
  - PASS: `logs/20260630-081839-3241783` (unique user spot routed `StateReq`)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-A4`
  - PASS: `logs/20260630-081825-3239853` (owner spot routed `StateReq`)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-A5`
  - PASS: `logs/20260630-081839-3241830` (stage request/timer)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-F1`
  - PASS: `logs/20260630-082100-3253769` (target spot request/command)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-F2`
  - PASS: `logs/20260630-082100-3253756` (target spot request/command)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-F4`
  - PASS: `logs/20260630-101412-3466073` (missing target request failure와 send drop evidence 확인)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-G1`
  - PASS: `logs/20260629-223922-1778101` (play-a crash isolation, play-b survivor, play-b rebind recovery)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-G2`
  - PASS: `logs/20260630-082118-3256210` (owner remap routed `StateReq`)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-G3`
  - PASS: `logs/20260629-224535-1792721` (concurrent user spot join/request/leave evidence)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-G4`
  - PASS: `logs/20260629-225216-1811400` (many bound session push isolation)
- 보조 operation: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh sm-q9`
  - PASS: `logs/20260630-082118-3256244` (MultiNode local owner route-to-spot)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-B2`
  - PASS: `logs/20260630-070751-3054866` (remote `play-b` `entry-created`, `entry-joined`, actor pingMsg evidence 확인)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-B3`
  - PASS: `logs/20260629-201946-1303393` (complex actor request payload fidelity)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-B4`
  - PASS: `logs/20260629-202300-1319449` (remote actor request)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-B5`
  - PASS: `logs/20260630-072224-3098758` (`handlerMissing` evidence와 stream error reply 확인)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-B6`
  - PASS: `logs/20260630-073618-3133487` (explicit leave와 stream close disconnect callback evidence 구분)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-B7`
  - PASS: `logs/20260630-070802-3055574` (`entry-created` -> `entry-joined` -> actor packet 순서 evidence 확인)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-B8`
  - PASS: `logs/20260630-072210-3097534` (`actor-destroyed` evidence와 post-destroy stream error reply 확인)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-C1`
  - PASS: `logs/20260630-081924-3247169` (request/send/slow timeout/post-timeout request)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-C3`
  - PASS: `logs/20260630-082100-3253716` (spot-to-spot request/send/publish/timeout/negative)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-C4`
  - PASS: `logs/20260630-083734-3303700` (Gateway publish evidence와 Play subscribed spot event evidence 확인)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-D1`
  - PASS: `logs/20260629-211928-1555127` (local stream auth, actor request relay, bound session push)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-D5`
  - PASS: `logs/20260630-073619-3133519` (stream close 후 `session-disconnected`/`entry-disconnected` evidence 확인)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-D9`
  - PASS: `logs/20260629-215409-1654253` (stream inbound observer response frame 관측)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-D10`
  - PASS: `logs/20260629-215844-1670329` (bounded received-message drop, request route 생존, 다른 stream push 격리)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-D11`
  - PASS: `logs/20260629-220146-1678135` (stream actor request와 channel control request 혼합)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-D12`
  - PASS: `logs/20260629-220618-1691419` (`session-a`에서 `session-b`로 재auth/rebind 후 actor state 보존)
- 선택 scenario: `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh SM-D13`
  - PASS: `logs/20260629-221012-1704641` (heartbeat-enabled stream 유지)
- 선택 scenario: `timeout 360s framework/languages/node/e2e/SpotService/run_e2e.sh SM-E3`
  - PASS: `logs/20260630-081940-3248210` (idle close와 closed target route failure)
