# Java SpotService E2E feature map

이 문서는 Config 2 SpotService 공통 시나리오 중 Java framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다. 실행 코드는 public Spring starter,
`ZLinkSpotManager`, `ZLinkSpotOutbound`, client/server channel builder, route mesh channel builder,
SpotNode builder만 사용한다.

## 구현됨

- `SM-A1`: public `ZLinkSpotManager.getOrCreate`로 user spot을 생성하고 evidence로 확인한다.
- `SM-A2`: public `ZLinkSpotOutbound.requestToSpot`으로 user spot state mutation을 검증한다.
- `SM-C1`: 외부 consumer의 public `ZLinkSpotOutbound`로 request, send, timeout, 미등록 packet
  negative path를 검증한다.

## public API/harness 대기

- `SM-A3`: 특정 spot id가 해당 owner 노드에서만 처리되는지 확인하는 독립 marker가 아직 없다.
- `SM-A4`: key-to-routing-id owner mapping을 여러 key와 owner로 고정하는 scenario가 아직 없다.
- `SM-A5`: Java E2E에는 Stage wrapper 계층이 아직 없다.
- `SM-A6`: spot initialize/close lifecycle callback을 evidence로 고정하는 scenario가 아직 없다.
- `SM-A7`: 같은 spot rid를 다른 spot type으로 다시 생성할 때의 public error를 단언하는 scenario가
  아직 없다.
- `SM-A8`: worker offload와 spot 직렬성 유지를 함께 단언하는 scenario가 아직 없다.
- `SM-B1`: local actor join과 lifecycle callback을 검증하는 scenario가 아직 없다.
- `SM-B2`: remote actor join과 cross-node mailbox 실행을 검증하는 scenario가 아직 없다.
- `SM-B3`: actor join/request payload fidelity를 검증하는 scenario가 아직 없다.
- `SM-B4`: remote actor request와 reply를 검증하는 scenario가 아직 없다.
- `SM-B5`: handler 없는 actor packet negative path를 검증하는 scenario가 아직 없다.
- `SM-B6`: explicit leave와 disconnect callback 차이를 검증하는 scenario가 아직 없다.
- `SM-B7`: actor lifecycle callback과 packet handler 실행 순서를 검증하는 scenario가 아직 없다.
- `SM-B8`: Java public API는 `ZLinkEntrySpotContext.destroyActor(ZLinkActor)` 형태다. 공통 문서의
  id 기반 절차와 같은 의미를 고정하는 Java scenario가 아직 없다.
- `SM-C2`: spot이 외부 channel로 request/send를 내보내고 SPOT mesh publish를 수행하는 scenario가
  아직 없다.
- `SM-C3`: spot-to-spot request/send/publish와 timeout/negative를 묶은 scenario가 아직 없다.
- `SM-C4`: local spot 없는 노드의 attached publisher client publish scenario가 아직 없다.
- `SM-D1`: local stream session bind와 actor relay/push scenario가 아직 없다.
- `SM-D2`: remote stream session bind와 cross-node actor relay/push scenario가 아직 없다.
- `SM-D3`: entry spot actor bind와 user spot actor bind를 비교하는 scenario가 아직 없다.
- `SM-D4`: 한 stream session에 여러 actor를 bind하고 `actor-id` metadata로 분기하는 scenario가
  아직 없다.
- `SM-D5`: session disconnect 뒤 선택 actor에만 disconnect callback을 통지하는 scenario가 아직 없다.
- `SM-D6`: bound session push target isolation scenario가 아직 없다.
- `SM-D7`: stream session auth와 auth 전 packet dispatch 실패를 검증하는 scenario가 아직 없다.
- `SM-D8`: stream reconnect 중 pending failure와 재auth/rebind를 검증하는 scenario가 아직 없다.
- `SM-D9`: stream inbound observer evidence를 검증하는 scenario가 아직 없다.
- `SM-D10`: stream backpressure 정책을 public contract로 고정하는 scenario가 아직 없다.
- `SM-D11`: 같은 consumer에서 stream request와 channel request를 섞는 scenario가 아직 없다.
- `SM-D12`: 다른 gateway로 재접속해 actor 상태를 이어받는 scenario가 아직 없다.
- `SM-D13`: stream heartbeat 중단과 disconnect 감지를 검증하는 scenario가 아직 없다.
- `SM-D14`: TLS stream endpoint와 certificate 구성이 아직 없다.
- `SM-E1`: handler 없는 spot route request의 observer evidence를 별도 marker로 고정하는 scenario가
  아직 없다.
- `SM-E2`: spot timer 발화와 효과를 검증하는 scenario가 아직 없다.
- `SM-E3`: idle timer 기반 명시 close를 검증하는 scenario가 아직 없다.
- `SM-E4`: timer overrun policy별 tick 처리 evidence를 검증하는 scenario가 아직 없다.
- `SM-F1`: RouteMesh와 SpotMesh 자동 연결은 있지만, client/server channel to target spot egress를
  독립 marker로 단언하는 scenario가 아직 없다.
- `SM-F2`: route mesh channel to target spot egress를 독립 marker로 단언하는 scenario가 아직 없다.
- `SM-F3`: 같은 channel에서 일반 packet과 spot route packet이 공존하는 분기 scenario가 아직 없다.
- `SM-F4`: route 없음, ingress 거부, malformed spot route packet의 error 계약 scenario가 아직 없다.
- `SM-F5`: spot routing 사용/중단과 channel socket lifecycle 독립성을 검증하는 scenario가 아직 없다.
- `SM-G1`: play node crash와 재join/rebind 복구를 검증하는 kill/restart harness가 아직 없다.
- `SM-G2`: scale-out 중 앱 주도 owner remap을 검증하는 harness가 아직 없다.
- `SM-G3`: 동시 join/leave/request 경합을 재현하는 부하 harness가 아직 없다.
- `SM-G4`: 다수 bound session push 부하와 오배달 없음을 확인하는 harness가 아직 없다.
