# Kotlin SpotService E2E feature map

이 문서는 Config 2 SpotService 공통 시나리오 중 Kotlin E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다. server role은 public Spring starter,
`ZLinkSpotManager`, `ZLinkRouteClient`, `ZLinkSpotOutbound`, client/server channel builder, route mesh
channel builder, SpotNode builder, stream connector, `ZLinkSpotPublisherClient`를 사용한다. Client role은
framework runtime을 띄우지 않고 HTTP endpoint와 public stream connector만 사용한다.

공통 E2E 문서와 다른 언어의 구현에 존재하는 public 기능이 Kotlin에 없으면 단순 미구현으로 완료
처리하지 않는다. 다만 다른 언어 구현만으로 Kotlin public contract를 새로 추가하지 않는다. spec 또는
공통 framework spec/guide에 근거가 있는 항목은 parity gap으로 관리하고, 근거가 부족한 항목은
draft/spec 검토 대상으로 분리한다. 공통 E2E 문서는 누락을 찾는 기준이지만, 새 public API를 추가할
근거로만 쓰지 않는다.

## 현재 live 검증 상태

- `logs/20260704-044437-55250` focused runner에서는 `SM-B1` 매핑의 actor-session 묶음이 통과했다.
  이 묶음은 `SM-B1`, `SM-B3`, `SM-B5`, `SM-B6`, `SM-B7`, `SM-B8`, `SM-D1`, `SM-D3`,
  `SM-D4`, `SM-D5`, `SM-D6`, `SM-D7`, `SM-D8`, `SM-D9`, `SM-D10`, `SM-D11`, `SM-D13`과
  `spot-service kotlin e2e focused modes=actor-session result=passed`를 확인한다.
- `logs/20260704-045245-65740` focused runner에서는 `SM-C4`가 `gateway-publish` mode로 통과했다.
  이 검증은 Java SpotService와 같은 수준으로 Gateway role의 public `ZLinkSpotPublisherClient.publishSpot`
  호출과 gateway publish evidence를 확인한다.
- `logs/20260704-045322-67016` full runner에서는 registry role 없이 Play/Gateway/MultiNode/Session role이
  공식 Redis location store extension을 같은 endpoint와 실행별 key prefix로 공유했고, 구현된 모든 mode와
  최종 `spot-service kotlin e2e result=passed` marker가 통과했다.
- actor-session topology는 Play가 소유하는 일반 `room-a`/`room-b`와 충돌하지 않도록 전용
  `actor-room-a`/`actor-room-b` spot을 사용한다. 같은 actor-session client와 Session role은 같은 Redis
  location store prefix를 공유한다.

## 구현됨

- `SM-A1`: public `ZLinkSpotManager.getOrCreate`로 user spot을 생성하고 evidence로 확인한다.
- `SM-A2`: Client가 Play HTTP endpoint를 호출하고, Play가 public `ZLinkRouteClient.requestToSpot`으로
  user spot state mutation을 검증한다.
- `SM-A3`: `room-a` 요청이 해당 spot의 owner인 `play-a`에서 처리되는지 확인한다.
- `SM-A4`: 같은 `RoutingId`인 `room-a`로 반복 보낸 요청이 같은 owner 노드에 유지되는지 확인한다.
- `SM-A6`: Play-B HTTP admin endpoint가 public `ZLinkSpotManager.getOrCreate`로 만든 user spot을
  public `ZLinkSpotManager.close`로 명시 close했을 때 closing evidence가 남는지 확인한다.
- `SM-A7`: 이미 만든 spot rid를 다른 spot 타입으로 `getOrCreate`할 때 public configuration error로
  거부되고, 기존 spot이 같은 타입으로 계속 조회되는지 확인한다.
- `SM-A8`: public `context.runWorker`로 긴 작업을 spot 직렬 루프 밖에서 실행하는 동안 같은 spot의
  후속 request가 막히지 않고, 완료 callback이 spot state/evidence를 안전하게 갱신하는지 확인한다.
- `SM-B1`: stream session에서 만든 local actor가 entry spot request를 처리하고, public
  `ZLinkActorContext.joinSpot`으로 user spot에 join한 뒤 user spot actor request를 처리하는지 확인한다.
- `SM-B3`: actor create/join/request payload의 profile, level, tag 값이 handler까지 유지되고 reply에
  같은 값이 반영되는지 확인한다.
- `SM-B5`: handler 없는 actor packet name인 `MissingActorReq`를 public stream connector request로 보냈을
  때 request가 실패하고, message-flow observer가 `SPOT_ACTOR` surface의 `HANDLER_MISSING` /
  `REPLY_ERROR` evidence를 남기는지 확인한다. `logs/20260630-035320-2565912` full runner에서
  `SM-B5` marker와 `SPOT_ACTOR/HANDLER_MISSING/REPLY_ERROR/MissingActorReq` flow evidence를 확인했다.
- `SM-B6`: user spot에 join한 actor가 public `ZLinkSpotContext.leaveActor(actor)`로 명시 leave할 때
  user spot leave evidence만 남고, stream disconnect 때는 entry spot disconnect evidence만 남는지
  확인한다. `logs/focused-actor-session-20260630-023713-2377275`에서 `SM-B6` marker와
  actor-session 통과 marker를 확인했다.
- `SM-B7`: actor create, entry request, join, user request callback/handler가 evidence marker를 남겨
  lifecycle과 packet handler 실행 순서를 확인할 수 있는지 검증한다. Session role의 `/evidence/wait`
  endpoint로 필요한 evidence marker가 모두 기록될 때까지 기다린다.
- `SM-B8`: stream session으로 bind한 actor를 public `ZLinkEntrySpotContext.destroyActor(actor)`로
  명시 파괴하고, 같은 actor로 다시 보내는 request가 실패하는지 확인한다.
  `logs/focused-actor-session-20260630-023307-2370308`에서 `SM-B8` marker와 actor-session 통과
  marker를 확인했다.
- `SM-C1`: Client가 Play HTTP endpoint를 호출하고, Play가 public `ZLinkRouteClient`로 request, send,
  timeout, 미등록 packet negative path를 검증한다.
- `SM-C2`: spot handler 안에서 public `ZLinkSpotOutbound.requestToChannel` /
  `sendToChannel`로 외부 channel에 request/send를 내보내고, 같은 handler에서 SPOT mesh publish를
  수행해 구독 spot의 evidence를 확인한다.
- `SM-C3`: Client가 Play HTTP endpoint를 호출하고, source spot handler가 public
  `ZLinkSpotOutbound.sendToSpot`으로 target user spot에 command를 보내는지 확인한다. SPOT mesh publish
  evidence도 함께 확인한다.
- `SM-C4`: Gateway role이 public `ZLinkSpotPublisherClient.publishSpot`으로 SPOT mesh에 publish하고,
  gateway publish 응답과 evidence를 확인한다. Java 기준 구현도 publisher role의 publish 호출을 검증하며
  Play role의 target spot 수신까지 C4 완료 조건으로 삼지 않는다.
- `SM-D1`: public stream connector와 framework `ZLinkSessionActors.bind` / `ZLinkSessionActor.relay` /
  actor `boundSession().send`로 local stream session auth, actor relay, actor push를 검증한다. Session
  evidence wait endpoint도 actor-session mode 안에서 함께 검증한다.
- `SM-D3`: 같은 stream actor가 entry spot request와 public `ZLinkActorContext.joinSpot`으로 join한
  user spot request에서 모두 relay/push를 수행하는지 비교한다. `actor-session` mode는
  `logs/focused-actor-session-20260630-020720-2316864`에서 `SM-D3` marker와 `ActorEntryRequest`,
  `ActorUserJoined`, `ActorUserRequest` evidence를 확인했다.
- `SM-D4`: 한 stream session에 두 actor를 bind하고 `actor-id` metadata로 각 actor request와 push가
  분기되는지 확인한다. `actor-id` 없이 보내는 request는 다중 bind 상태에서 실패해야 한다.
  `logs/focused-actor-session-20260630-021110-2323861`에서 `SM-D4` marker, 두 actor의
  `ActorSessionBound`, `ActorEntryRequest` evidence를 확인했다.
- `SM-D5`: stream disconnect 때 session handler가 선택한 bound actor에만 public
  `ZLinkSessionActor.notifyDisconnected`를 호출하고, entry spot의 disconnect callback evidence가
  남는지 확인한다. `logs/focused-actor-session-20260630-021110-2323861`에서 `SM-D5` marker,
  `ActorDisconnectNotified`, `ActorEntryDisconnected` evidence를 확인했다.
- `SM-D6`: `session-a`와 `session-b`에 각각 연결한 stream session 중 request를 보낸 actor의 bound
  session에만 public `ZLinkSessionActor.boundSession().send` push가 전달되고, 다른 gateway의 session에는
  `ActorPushNotify`가 전달되지 않는지 확인한다. `logs/focused-actor-session-20260630-031506-2451994`에서
  `SM-D6` marker와 actor-session 통과 marker를 확인했다.
- `SM-D7`: auth 전에 actor packet을 보내면 public stream connector request가 실패하고, 새 stream
  session에서 `ActorAuthReq`로 actor를 bind한 뒤에는 같은 actor request와 push가 정상 dispatch되는지
  확인한다. `logs/focused-actor-session-20260630-022502-2354570`에서 `SM-D7` marker와
  actor-session 통과 marker를 확인했다.
- `SM-D8`: stream reconnect 중 끊긴 stream의 pending request가 실패하고, disconnect callback evidence가
  남은 뒤 같은 actor가 새 stream에서 재auth/rebind되어 request를 처리하는지 확인한다.
  `logs/focused-actor-session-20260630-025247-2408499`에서 `SM-D8` marker와 actor-session 통과
  marker를 확인했다.
- `SM-D9`: public stream connector의 `observeInbound` observer가 stream reply packet name을 관측하는지
  확인한다. `logs/focused-actor-session-20260630-021110-2323861`에서 `SM-D9` marker와 Session
  `StreamInbound` evidence를 함께 확인했다.
- `SM-D10`: `maxReceivedMessages = 1`과 manual dispatch를 사용하는 public stream connector가 오래된
  actor push를 버리고 최신 push만 유지하는지 확인한다. 같은 Session endpoint의 다른 stream session은
  push를 정상 수신해 backpressure가 session별로 격리되는지도 확인한다.
  `logs/focused-actor-session-20260630-031506-2451994`에서 `SM-D10` marker와 actor-session 통과
  marker를 확인했다.
- `SM-D11`: 같은 client process에서 public stream connector로 actor request를 처리한 뒤 Play HTTP
  endpoint를 통해 public `ZLinkRouteClient` route-channel request를 보내 stream 경로와 channel 경로가 함께 동작하는지
  확인한다. `logs/focused-actor-session-20260630-030200-2426602`에서 `SM-D11` marker와 actor-session
  통과 marker를 확인했다.
- `SM-D13`: heartbeat가 켜진 public stream connector가 일정 시간 연결을 유지하고, 같은 stream session에서
  actor request를 계속 처리하는지 확인한다. `logs/focused-actor-session-20260630-025654-2416593`에서
  `SM-D13` marker와 actor-session 통과 marker를 확인했다.
- `SM-E1`: handler 없는 spot route request/send가 error/drop 경로를 타고 dispatch observer evidence를
  남기는지 확인한다.
- `SM-E2`: user spot이 public `context.addTimer`로 등록한 timer를 주기적으로 실행하고 tick evidence를
  남기는지 확인한다.
- `SM-E3`: public `context.addTimer`로 만든 idle timer가 idle spot을 public `context.close`로 닫고,
  계속 열려 있어야 하는 spot은 닫지 않는지 evidence로 확인한다.
- `SM-E4`: public `ZLinkTimerOptions`와 `ZLinkTimerOverrunPolicy`로 skip/catch-up/delay overrun
  policy를 설정하고, `ZLinkTimerTick`의 delivery/skipped evidence가 남는지 확인한다.
- `SM-F1`: 외부 consumer가 RouteMesh 경로로 target spot에 도달하는지 확인한다.
- `SM-F2`: RouteMesh 채널명이 target spot egress의 실제 channel 기준으로 동작하는지 확인한다.
- `SM-F3`: 같은 RouteMesh에서 일반 route-channel request/reply와 target spot request/send가 한
  channel 위에서 함께 구성되고, 일반 packet은 channel handler가 처리하는지 확인한다.

## public contract parity 또는 spec 검토 대기

아래 항목은 공통 E2E에 존재하고 .NET 또는 C++ SpotService E2E에서도 public framework 흐름으로
검증되는 scenario다. Kotlin에서 같은 public 기능을 제공할지는 spec, 공통 framework 문서, guide의
계약 근거를 먼저 확인한다. 근거가 확인된 항목만 Kotlin public contract parity 구현 대상으로 삼고,
다른 언어 구현만 근거인 항목은 draft/spec 검토 대상으로 남긴다.

- `SM-A5`: .NET에는 app-level Stage wrapper 경로가 있지만 Kotlin에는 대응 public wrapper 계층이
  아직 없다.
- `SM-B2`: remote actor join과 cross-node mailbox 실행을 검증하는 scenario가 아직 없다.
- `SM-B4`: remote actor request와 reply를 검증하는 scenario가 아직 없다.
- `SM-D2`: remote stream session bind와 cross-node actor relay/push scenario가 아직 없다.
- `SM-D12`: `session-a`에서 끊고 `session-b`로 재접속하는 topology는 준비했지만, 현재 Kotlin Session
  role은 연결 서버와 actor logic을 분리하지 않아 같은 actor id가 `session-b` 쪽 actor로 bind된다. 공통
  기준처럼 logic actor 상태가 gateway와 무관하게 유지되는 완료 scenario는 아직 없다.
- `SM-D14`: TLS stream endpoint와 certificate 구성이 아직 없다.

## E2E/harness 대기

아래 항목은 public API 자체의 부재로 단정하지 않고, 현재 Kotlin E2E가 필요한 제어와 증거를 아직
갖추지 못한 상태로 관리한다. 구현 과정에서 다른 언어에 public 기능이 확인되면 위
`public contract parity 또는 spec 검토 대기`로 옮긴다.

- `SM-F4`: 존재하지 않는 route target request가 timeout이 아닌 framework error로 실패하는
  missing-route 부분은 runner가 `SM-F4-missing-route`로 확인한다. malformed spot route packet
  주입과 command drop/failure counter 분류는 public typed client로 만들 수 없어 raw-frame harness가
  필요하다.
- `SM-F5`: spot routing 사용/중단과 channel socket lifecycle 독립성을 검증하는 scenario가 아직 없다.
- `SM-G1`: play node crash와 재join/rebind 복구를 검증하는 kill/restart harness가 아직 없다.
- `SM-G2`: scale-out 중 앱 주도 owner remap을 검증하는 harness가 아직 없다.
- `SM-G3`: 동시 join/leave/request 경합을 재현하는 부하 harness가 아직 없다.
- `SM-G4`: 다수 bound session push 부하와 오배달 없음을 확인하는 harness가 아직 없다.

## .NET 추가 검증 파일

아래 항목은 `.NET` tree에는 client scenario file로 존재하지만, 공통 Config 2 문서와 `.NET
feature-map.ko.md`에는 scenario ID로 등록되어 있지 않다. Kotlin에서는 공통 scenario 완료와 분리해
추적한다.

- `SM-Q9`: `.NET` `Client/Scenarios/SmQ9Scenario.cs`는 MultiNode role의 두 노드에서 local spot을
  만들고 route-to-spot request가 각 노드의 spot으로 유지되는지 확인한다. Kotlin에는 MultiNode 전용
  role/project와 같은 local create/state/evidence path가 생겼고, client `multi-node` mode가 같은 흐름을
  실행한다. `logs/20260630-013008-2203737` full runner에서 `scenario SM-Q9 passed`,
  `spot-service kotlin e2e mode=multi-node result=passed`, 두 노드의 `multi-state-request` evidence를
  확인했다.
