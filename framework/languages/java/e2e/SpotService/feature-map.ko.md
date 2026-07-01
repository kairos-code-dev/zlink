# Java SpotService E2E feature map

이 문서는 Config 2 SpotService 공통 시나리오 중 Java framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다. 실행 코드는 public Spring starter,
`ZLinkSpotManager`, `ZLinkSpotOutbound`, client/server channel builder, route mesh channel builder,
SpotNode builder, stream connector, `ZLinkSpotPublisherClient`만 사용한다.

공통 E2E 문서와 다른 언어의 구현에 존재하는 public 기능이 Java에 없으면 단순 미구현으로 완료
처리하지 않는다. 다만 다른 언어 구현만으로 Java public contract를 새로 추가하지 않는다. spec 또는
공통 framework spec/guide에 근거가 있는 항목은 parity gap으로 관리하고, 근거가 부족한 항목은
draft/spec 검토 대상으로 분리한다. 공통 E2E 문서는 누락을 찾는 기준이지만, 새 public API를 추가할
근거로만 쓰지 않는다.

## 구현됨

- `SM-A1`: public `ZLinkSpotManager.getOrCreate`로 user spot을 생성하고 evidence로 확인한다.
- `SM-A2`: public `ZLinkSpotOutbound.requestToSpot`으로 user spot state mutation을 검증한다.
- `SM-A3`: `room-a`와 `room-b`가 각각 `play-a`와 `play-b`에서만 처리되는지 확인한다.
- `SM-A4`: 같은 key가 같은 `RoutingId`와 같은 owner 노드로 반복 라우팅되는지 확인한다.
- `SM-A6`: user spot 생성 시 initialize evidence가 남고, public `ZLinkSpotManager.close`로 명시
  close했을 때 closing evidence가 남는지 확인한다.
- `SM-A7`: 이미 만든 spot rid를 다른 spot 타입으로 `getOrCreate`할 때 public configuration error로
  거부되고, 기존 spot이 같은 타입으로 계속 조회되는지 확인한다.
- `SM-A8`: public `context.runWorker`로 긴 작업을 spot 직렬 루프 밖에서 실행하는 동안 같은 spot의
  후속 request가 막히지 않고, 완료 callback이 spot state/evidence를 안전하게 갱신하는지 확인한다.
- `SM-B1`: stream session에서 만든 local actor가 entry spot request를 처리하고, public
  `ZLinkActorContext.joinSpot`으로 user spot에 join한 뒤 user spot actor request를 처리하는지 확인한다.
- `SM-B3`: actor create/join/request payload의 profile, level, tag 값이 handler까지 유지되고 reply에
  같은 값이 반영되는지 확인한다.
- `SM-B6`: public `ZLinkSpotContext.leaveActor(actor)`로 명시 leave를 실행했을 때 user spot leave
  callback만 남고, stream disconnect 때는 actor disconnect callback만 남는지 확인한다.
- `SM-B7`: actor create, entry request, join, user request callback/handler가 evidence marker를 남겨
  lifecycle과 packet handler 실행 순서를 확인할 수 있는지 검증한다.
- `SM-C1`: 외부 consumer의 public `ZLinkSpotOutbound`로 request, send, timeout, 미등록 packet
  negative path를 검증한다.
- `SM-C2`: spot handler 안에서 public `ZLinkSpotOutbound.requestToChannel` /
  `sendToChannel`로 외부 channel에 request/send를 내보내고, 같은 handler에서 SPOT mesh publish를
  수행해 구독 spot의 evidence를 확인한다.
- `SM-C3`: public `ZLinkSpotOutbound.requestToSpot` / `sendToSpot`으로 user spot 사이
  request/send가 노드 경계를 넘어 처리되는지 확인하고, SPOT mesh publish evidence를 함께 확인한다.
- `SM-C4`: local spot factory가 없는 publisher 역할이 public `ZLinkSpotPublisherClient.publishSpot`으로
  SPOT mesh에 publish하고, 구독 spot들이 event evidence를 남기는지 확인한다.
- `SM-D1`: public stream connector와 framework `ZLinkSessionActors.bind` / `ZLinkSessionActor.relay` /
  actor `boundSession().send`로 local stream session auth, actor relay, actor push를 검증한다.
- `SM-D5`: stream session disconnect 뒤 bound actor의 현재 user spot에 disconnect callback이
  통지되는지 확인한다.
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
검증되는 scenario다. Java에서 같은 public 기능을 제공할지는 spec, 공통 framework 문서, guide의
계약 근거를 먼저 확인한다. 근거가 확인된 항목만 Java public contract parity 구현 대상으로 삼고,
다른 언어 구현만 근거인 항목은 draft/spec 검토 대상으로 남긴다.

- `SM-A5`: .NET에는 app-level Stage wrapper 경로가 있지만 Java에는 대응 public wrapper 계층이
  아직 없다.
- `SM-B5`: handler 없는 actor packet negative path를 검증하는 scenario가 아직 없다.
- `SM-B8`: Java public API는 `ZLinkEntrySpotContext.destroyActor(ZLinkActor)` 형태다. 공통 문서의
  id 기반 절차와 같은 의미를 고정하는 Java scenario가 아직 없다.
- `SM-D3`: entry spot actor bind와 user spot actor bind를 비교하는 scenario가 아직 없다.
- `SM-D4`: 한 stream session에 여러 actor를 bind하고 `actor-id` metadata로 분기하는 scenario가
  아직 없다.
- `SM-D6`: bound session push target isolation scenario가 아직 없다.
- `SM-D7`: stream session auth와 auth 전 packet dispatch 실패를 검증하는 scenario가 아직 없다.
- `SM-D8`: stream reconnect 중 pending failure와 재auth/rebind를 검증하는 scenario가 아직 없다.
- `SM-D9`: stream inbound observer evidence를 검증하는 scenario가 아직 없다.
- `SM-D10`: stream backpressure 정책을 public contract로 고정하는 scenario가 아직 없다.
- `SM-D11`: 같은 consumer에서 stream request와 channel request를 섞는 scenario가 아직 없다.
- `SM-D12`: 다른 gateway로 재접속해 actor 상태를 이어받는 scenario가 아직 없다.
- `SM-D13`: stream heartbeat 중단과 disconnect 감지를 검증하는 scenario가 아직 없다.
- `SM-D14`: TLS stream endpoint와 certificate 구성이 아직 없다.

## Java public contract 기반 E2E 미구현

아래 항목은 Java spec에 이미 remote actor/session relay 표면이 있다. 따라서 새 public API 후보로
분류하지 않고, 현재 Java E2E가 아직 검증하지 못한 구현 gap으로 관리한다.

- `SM-B2`: Java spec은 remote `ZLinkActorRef` binding과 SessionRelay 기반 logical actor handle을
  설명하지만, E2E에는 remote actor join과 cross-node mailbox 실행 scenario가 아직 없다.
- `SM-B4`: 같은 public actor/session relay 표면을 사용해 remote actor request와 reply를 검증하는
  scenario가 아직 없다.
- `SM-D2`: `ZLinkSessionActors.bind(ZLinkActorRef)`와 `ZLinkSessionActor.relay(...)`를 사용한
  remote stream session bind, cross-node actor relay, bound session push scenario가 아직 없다.

## E2E/harness 대기

아래 항목은 public API 자체의 부재로 단정하지 않고, 현재 Java E2E가 필요한 제어와 증거를 아직
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
