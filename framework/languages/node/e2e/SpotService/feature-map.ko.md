# Node SpotService E2E feature map

이 문서는 Config 2 SpotService 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `SM-A6`: public NestJS `ZLINK_SPOT_MANAGER`로 user Spot을 생성해 `onCreate`와 `onInitialize`
  순서를 확인하고, public actor manager와 actor `context.joinSpot(...)`으로 actor를 join한 뒤
  actor가 남아 있으면 `close(...)`가 거부되는지 확인한다. 이후 public `leaveActor(...)`로 actor를
  제거하고 close 시 `onClosing`이 한 번 호출되는지 확인한다.
- `SM-A7`: 같은 `spotRid`를 `UserSpot`으로 만든 뒤 다른 Spot 타입으로 다시 `getOrCreate(...)`하면
  public `ZLinkFrameworkException.kind == SpotTypeMismatch`로 실패하고, 기존 Spot은 같은 타입으로
  계속 조회되는지 확인한다.
- `SM-A5`: 앱 레벨 Stage wrapper 객체를 Spot 위에 얹고, public `ZLINK_SPOT_MANAGER`로 생성한
  뒤 wrapper 초기값, Spot lifecycle, wrapper request 처리, Spot timer tick, close callback이 같은
  Spot 실행 계약 안에서 순서대로 동작하는지 확인한다.
- `SM-E2`: user Spot이 public `context.addTimer(...)`로 timer를 등록하고, timer handler tick이
  spot 상태를 주기적으로 바꾸며 close 시 마지막 상태가 관측되는지 확인한다.
- `SM-E3`: idle timer handler가 actor join 전에는 닫지 않고, actor가 남아 있으면 public
  `close(...)`가 거부되며, actor leave 뒤 다음 timer tick에서 close와 `onClosing`이 완료되는지
  확인한다.
- `SM-E4`: public `context.addTimer(...)` overrun policy 세 가지(`SkipLateTicks`,
  `CatchUpBounded`, `DelayNextTick`)를 실제 Spot timer로 등록하고, 느린 첫 tick 이후의
  `scheduledIndex`/`skippedTicks` 패턴이 정책별 의미와 맞는지 확인한다.

## public API/harness 대기

- `SM-A1`: same-process public actor manager와 actor `context.joinEntrySpot(...)`, Entry Spot
  `onActorJoin(...)`, public `ZLINK_SPOT_MANAGER` 기반 user Spot 생성 self-check는 runner에 있다.
  그러나 공통 시나리오가 요구하는 consumer의 registry-resolved entry spot join/request 경로 evidence는
  아직 없어 완료 marker로 올리지 않는다.
- `SM-A2`: public `ZLinkSpotRemoteAddressResolver`와 Spot context `outbound.requestToSpot(...)`
  경로를 실제 SpotMesh router로 검증하려면 두 SpotNode 사이 route가 필요하다. 단일 self-route와
  수동 두 노드 연결 모두 Node E2E에서 route-ready evidence를 만들지 못했으므로, registry/discovery
  기반 Spot route harness가 준비될 때까지 완료 marker로 올리지 않는다.
- `SM-A3`: route resolver 정확성 Node runner와 marker가 아직 없다.
- `SM-A4`: 앱이 같은 key를 같은 `spotRid`로, 다른 key를 다른 `spotRid`로 결정적으로 매핑하고
  public `ZLINK_SPOT_MANAGER`가 같은 key의 user Spot을 재사용하는 self-check
  `SM-A4-KEY-ROUTING-MAPPING`은 runner에 있다. 다만 공통 시나리오가 요구하는 cross-node
  spot lookup과 owner 노드 evidence는 아직 없어 완료 marker로 올리지 않는다.
- `SM-A8`: public `context.runWorker(...).onCompleted(...)` self-check는 runner에 있으나, 공통
  시나리오가 요구하는 "worker 실행 중 같은 Spot/노드로 들어오는 다른 request" marker는 아직 없다.
- `SM-B1`: 단일 Node SpotService 앱에서 public actor manager와 actor `context.joinSpot(...)`,
  `onActorJoin(...)`, `onJoinedActor(...)`, actor context `isJoined`/`spotRid`를 확인하는
  self-check는 있으나, 공통 시나리오가 요구하는 두 play 노드 배포, `play-a` local mailbox
  dispatch, 후속 actor request, `play-b` callback 없음 evidence가 아직 없다.
- `SM-B2`: remote actor join Node runner와 marker가 아직 없다.
- `SM-B3`: public `actorManager.getOrCreate(...)`와 actor `context.joinSpot(...)` 경로로
  복합 객체 join payload가 `onActorJoin(...)`과 join reply까지 그대로 왕복하는 self-check
  `SM-B3-JOIN-PAYLOAD-FIDELITY`는 runner에 있다. 다만 공통 시나리오가 요구하는 후속 actor
  request payload fidelity marker는 아직 없어 완료 marker로 올리지 않는다.
- `SM-B4`: 공통 시나리오는 다른 노드 actor로 request를 보내고 reply가 돌아오는지 본다. Node spec에서
  public actor packet ingress는 stream session bind 결과인 `ZLinkSessionActor.relay(...)` 경로다.
  현재 SpotService runner에는 cross-node stream session bind/relay harness가 없어 remote actor request를
  완료 marker로 올리지 않는다.
- `SM-B5`: handler 없는 actor packet request는 Node runtime에서
  `ActorDispatchHandlerNotFound`/`HandlerMissing`으로 매핑되는 경로가 있지만, 공통 시나리오는 public
  actor packet ingress와 message-flow observer evidence를 함께 요구한다. 현재 runner에는
  `ZLinkSessionActor.relay(...)`로 미등록 actor request를 넣고 observer까지 검증하는 harness가 없어
  완료 marker로 올리지 않는다.
- `SM-B6`: 명시적 leave callback은 `SM-A6`/`SM-E3` self-check에서 public `leaveActor(...)`와
  `onLeaveActor(...)`로 관측된다. 하지만 공통 시나리오의 disconnect 절반은 stream session handler가
  bind 결과인 `ZLinkSessionActor.notifyDisconnected(...)`를 호출해 `onDisconnectActor(...)`가
  1회만 발화하는지 봐야 한다. 현재 SpotService runner에는 public stream session bind/relay harness가
  없어 leave와 disconnect를 같은 E2E marker로 비교하지 못하므로 완료 marker로 올리지 않는다.
- `SM-B7`: Node spec은 user Spot 안 actor packet과 lifecycle callback이 같은 Spot 실행 queue에서
  순서대로 처리된다고 설명한다. 하지만 공통 시나리오는 join 직후 여러 actor packet을 실제 public
  ingress로 보내 lifecycle 뒤 packet dispatch 순서를 확인해야 한다. 현재 SpotService runner에는
  stream session bind 뒤 `ZLinkSessionActor.relay(...)`로 actor packet을 넣는 public harness가 없어,
  내부 dispatcher 직접 호출이나 private relay packet 조립으로 완료 처리하지 않는다.
- `SM-B8`: public Entry Spot `context.destroyActor(...)` self-check는 runner에 있다. Entry Spot에
  있는 actor destroy가 actor manager에서 정리되고 두 번째 destroy가 idempotent하게 끝나는지 확인한다.
  다만 공통 시나리오가 요구하는 user Spot join 뒤 Entry Spot 복귀, 파괴 후 actor request의 정해진
  public error, lifecycle callback evidence가 아직 없어 완료 marker로 올리지 않는다.
- `SM-C1`: 공통 시나리오는 외부 channel client가 target Spot으로 request/send/publish를 넣는 ingress를
  요구한다. Node public spec은 외부 `ZLinkRouteClient`를 target node route용으로 두고, target Spot으로
  가는 일반 egress client를 application 표면에 노출하지 않는다. 현재 Node에서 Spot route egress는 current
  Spot callback 안의 `context.outbound.sendToSpot(...)` / `requestToSpot(...)` 경로이므로, 공통
  channel→spot ingress contract가 정리되기 전까지 완료 marker로 올리지 않는다.
- `SM-C2`: spot→channel send/request는 current Spot callback의
  `context.outbound.sendToChannel(...)` / `requestToChannel(...)` public 표면으로 표현된다. 하지만
  공통 시나리오는 reply 반영, timeout, 미등록 negative, SPOT mesh publish delivery까지 한 marker에서
  요구한다. 현재 runner에는 route bridge channel socket과 SpotMesh publish readiness를 함께 안정적으로
  띄우는 harness가 없어 완료 marker로 올리지 않는다.
- `SM-C3`: spot→spot request/send는 current Spot callback의 `context.outbound.sendToSpot(...)` /
  `requestToSpot(...)` public 표면으로 표현된다. 현재 runner에는 두 user Spot을 실제 SpotMesh route로
  연결하고 request/send/publish/timeout/미등록 negative를 모두 검증하는 routed Spot harness가 없어
  완료 marker로 올리지 않는다.
- `SM-C4`: Node spec과 public DI에는 `ZLINK_SPOT_PUBLISHER_CLIENT`가 있지만, publish-only
  앱과 subscriber spot 앱을 public API만으로 구성했을 때 `publishSpot(...).submit()`이 반환하지
  않아 E2E delivery evidence를 완료 marker로 고정하지 못했다. transport readiness/harness를
  분리해 무한 대기 없이 검증할 수 있어야 완료 처리한다.
- `SM-D1`: Node public surface에는 stream node `registerSession(...)`, session
  `context.actors.bind(...)`, `ZLinkSessionActor.relay(...)`, actor
  `context.boundSession.send(...)`가 있다. 하지만 SpotService runner에는 실제
  `@zlink-systems/stream-connector` client를 띄워 local actor bind, client→actor relay,
  actor→client push, 미bind client 미수신을 함께 검증하는 harness가 없어 완료 marker로 올리지 않는다.
- `SM-D2`: remote bind/relay는 `SM-D1`의 stream harness에 더해 gateway와 다른 play node로 actor
  packet이 넘어가는 route evidence가 필요하다. 현재 runner에는 cross-node stream session
  bind/relay harness가 없어 완료 marker로 올리지 않는다.
- `SM-D3`: Entry Spot actor와 user Spot actor 모두 `ZLinkSessionContext.actors.bind(...)`로
  session에 묶을 수 있어야 한다. 현재 runner는 Entry/user Spot 양쪽 actor를 같은 stream session에서
  bind하고 relay/push 의미를 비교하지 않으므로 완료 marker로 올리지 않는다.
- `SM-D4`: 공통 시나리오는 한 stream session에 여러 actor를 bind한 뒤 metadata의 `actor-id`로
  `context.actors.find(...)` 대상만 골라 `ZLinkSessionActor.relay(...)`하는지 본다. 현재 runner에는
  multi-bind stream session과 actor별 push isolation harness가 없어 완료 marker로 올리지 않는다.
- `SM-D5`: Node spec은 session disconnect가 actor에 자동 전파되지 않고, session code가
  `ZLinkSessionActor.notifyDisconnected(...)`를 호출한 actor만 `onDisconnectActor(...)`를 받는다고
  설명한다. 현재 runner에는 stream disconnect 후 선택 actor 통지와 미통지 actor 무발화를 함께 검증하는
  harness가 없어 완료 marker로 올리지 않는다.
- `SM-D6`: bound session push는 actor handler가 `context.boundSession.send(...)`로 현재 actor에 묶인
  client에게만 push하는 경로다. 현재 runner에는 bind된 stream client와 bind되지 않은 client를 동시에
  띄워 push 오배달 없음을 검증하는 harness가 없어 완료 marker로 올리지 않는다.
- `SM-D7`: Node stream server session과 connector는 public 표면이 있지만, 현재 SpotService runner에는
  auth packet, auth 실패 public error, auth 이후 dispatch를 한 흐름으로 검증하는 stream E2E가 없어
  완료 marker로 올리지 않는다.
- `SM-D8`: connector에는 reconnect option이 있지만 공통 시나리오는 pending request 실패, 자동 재전송
  없음, 재auth·rebind 후 정상 재개를 함께 요구한다. 현재 runner에는 연결 중단과 재접속을 통제하는
  stream harness가 없어 완료 marker로 올리지 않는다.
- `SM-D9`: connector는 inbound observer model을 제공하지만, 현재 SpotService runner에는 observer를
  등록한 실제 stream client로 inbound kind/name/seq evidence를 남기는 harness가 없어 완료 marker로
  올리지 않는다.
- `SM-D10`: Node stream spec은 backpressure를 public no-wait 옵션이 아니라 내부 queue와 ready
  notification으로 처리한다고 설명한다. 현재 runner에는 그 정책을 stream session 단위로 압박하고
  다른 session 비오염까지 확인하는 deterministic harness가 없어 완료 marker로 올리지 않는다.
- `SM-D11`: stream request와 channel request 혼합은 stream connector와 `ZLINK_CHANNEL_CLIENT`를 같은
  consumer 흐름에서 함께 사용해야 한다. 현재 runner에는 두 reply 경로가 서로 섞이지 않는지 검증하는
  mixed-path harness가 없어 완료 marker로 올리지 않는다.
- `SM-D12`: 다른 gateway 재접속은 둘 이상의 stream node/gateway와 같은 actor rebind evidence가 필요하다.
  현재 runner에는 gateway 전환, 재auth, rebind, actor 상태 유지까지 확인하는 harness가 없어 완료 marker로
  올리지 않는다.
- `SM-D13`: connector heartbeat option과 server `onDisconnected(...)` callback은 public 표면에 있지만,
  현재 runner에는 heartbeat 중단을 유도하고 session disconnect와 actor disconnect 수동 통지를 분리해
  검증하는 harness가 없어 완료 marker로 올리지 않는다.
- `SM-D14`: connector는 TLS endpoint와 certificate validation option을 갖지만, 현재 runner에는 TLS stream
  server/client 인증서 fixture와 잘못된 인증서 거부 evidence가 없어 완료 marker로 올리지 않는다.
- `SM-E1`: handler 없는 spot route packet request는 Node spec상 current Spot callback의
  `context.outbound.requestToSpot(...)` 경로로 검증해야 한다. 현재 runner에는 routed Spot
  outbound를 실제 route mesh와 함께 안정적으로 구성하는 harness가 없어 완료 marker로 올리지 않는다.
- `SM-F1`: common E2E 문서는 외부 코드가 route client로 target spot RoutingId를 직접 지정하는
  public API를 전제로 한다. Node public spec은 `ZLinkRouteClient`를 target node route용으로만 두고,
  Spot으로 가는 routed transport를 외부 egress client로 노출하지 않는다. Node에서는 current Spot
  callback 안의 `context.outbound.sendToSpot(...)` / `requestToSpot(...)`만 public spot-routed
  egress이므로, 공통 scenario와 Node public contract 차이가 정리될 때까지 완료 marker로 올리지
  않는다.
- `SM-F2`: target node와 target spot을 외부 route client에서 함께 지정하는 public Node API가 없다.
  Node spec은 RouteMesh target node 호출과 Spot outbound 호출을 분리하므로, 현재 public contract만으로
  common scenario의 cross-node target spot egress를 완료 처리하지 않는다.
- `SM-F3`: 일반 route packet과 spot route packet 공존은 runtime 계약 테스트에는 있으나, Node public
  application 표면에는 외부 spot route egress client가 없다. public E2E harness가 current Spot
  outbound와 route mesh를 함께 띄워 양쪽 packet을 검증할 수 있을 때 완료 처리한다.
- `SM-F4`: route 없음·malformed spot route negative는 runtime 내부 계약 테스트에는 있으나, Node public
  표면에서 malformed spot route packet을 직접 만들거나 외부 spot route request를 보내는 API는 없다.
  내부 relay packet을 조립하지 않고 public harness로 error evidence를 남길 수 있을 때 닫는다.
- `SM-F5`: common scenario는 외부 spot route ingress를 얹은 channel에서 SpotNode 종료 뒤 일반
  channel request가 계속 동작하는지 본다. Node public application 표면에는 외부 spot route egress
  client가 없고, 현재 runner도 route mesh와 current Spot outbound를 함께 띄운 ownership harness가
  없어 완료 marker로 올리지 않는다.
- `SM-G1`: play node crash와 복구 Node harness가 아직 없다.
- `SM-G2`: owner 이동 Node harness가 아직 없다.
- `SM-G3`: 동시 join/leave 경합 Node harness가 아직 없다.
- `SM-G4`: 다수 bound session push 부하 Node harness가 아직 없다.
