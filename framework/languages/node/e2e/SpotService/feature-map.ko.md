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
- `SM-E2`: user Spot이 public `context.addTimer(...)`로 timer를 등록하고, timer handler tick이
  spot 상태를 주기적으로 바꾸며 close 시 마지막 상태가 관측되는지 확인한다.
- `SM-E3`: idle timer handler가 actor join 전에는 닫지 않고, actor가 남아 있으면 public
  `close(...)`가 거부되며, actor leave 뒤 다음 timer tick에서 close와 `onClosing`이 완료되는지
  확인한다.

## public API/harness 대기

- `SM-A1`: entry spot dispatch/request evidence Node runner와 marker가 아직 없다.
- `SM-A2`: 반복 state mutation과 concurrency consistency Node runner와 marker가 아직 없다.
- `SM-A3`: route resolver 정확성 Node runner와 marker가 아직 없다.
- `SM-A4`: owner routing key mapping Node runner와 marker가 아직 없다.
- `SM-A5`: Stage wrapper Node runner와 marker가 아직 없다.
- `SM-A8`: public `context.runWorker(...).onCompleted(...)` self-check는 runner에 있으나, 공통
  시나리오가 요구하는 "worker 실행 중 같은 Spot/노드로 들어오는 다른 request" marker는 아직 없다.
- `SM-B1`: local actor join Node runner와 marker가 아직 없다.
- `SM-B2`: remote actor join Node runner와 marker가 아직 없다.
- `SM-B3`: actor payload fidelity Node runner와 marker가 아직 없다.
- `SM-B4`: remote actor request Node runner와 marker가 아직 없다.
- `SM-B5`: actor 미등록 request negative path Node runner와 marker가 아직 없다.
- `SM-B6`: leave와 disconnect callback 차이 Node runner와 marker가 아직 없다.
- `SM-B7`: actor handler 실행 순서 Node runner와 marker가 아직 없다.
- `SM-B8`: actor explicit destroy Node runner와 marker가 아직 없다.
- `SM-C1`: channel to spot messaging Node runner와 marker가 아직 없다.
- `SM-C2`: spot to channel messaging Node runner와 marker가 아직 없다.
- `SM-C3`: spot to spot messaging Node runner와 marker가 아직 없다.
- `SM-C4`: spot publisher client Node runner와 marker가 아직 없다.
- `SM-D1`: local stream session bind/relay Node runner와 marker가 아직 없다.
- `SM-D2`: remote stream session bind/relay Node runner와 marker가 아직 없다.
- `SM-D3`: entry/user spot actor bind 비교 Node runner와 marker가 아직 없다.
- `SM-D4`: multi actor bind Node runner와 marker가 아직 없다.
- `SM-D5`: session disconnect actor notification Node runner와 marker가 아직 없다.
- `SM-D6`: bound session push isolation Node runner와 marker가 아직 없다.
- `SM-D7`: stream auth와 dispatch Node runner와 marker가 아직 없다.
- `SM-D8`: stream reconnect Node runner와 marker가 아직 없다.
- `SM-D9`: stream inbound observer Node runner와 marker가 아직 없다.
- `SM-D10`: stream backpressure Node runner와 marker가 아직 없다.
- `SM-D11`: stream/channel mixed request Node runner와 marker가 아직 없다.
- `SM-D12`: 다른 gateway 재접속 Node runner와 marker가 아직 없다.
- `SM-D13`: stream heartbeat Node runner와 marker가 아직 없다.
- `SM-D14`: TLS stream Node runner와 marker가 아직 없다.
- `SM-E1`: spot route 미등록 request Node runner와 marker가 아직 없다.
- `SM-E4`: timer overrun policy Node runner와 marker가 아직 없다.
- `SM-F1`: client/server channel to target spot Node runner와 marker가 아직 없다.
- `SM-F2`: route mesh channel to target spot Node runner와 marker가 아직 없다.
- `SM-F3`: 일반 packet과 spot route packet 공존 Node runner와 marker가 아직 없다.
- `SM-F4`: spot route negative 계약 Node runner와 marker가 아직 없다.
- `SM-F5`: channel socket ownership 독립성 Node runner와 marker가 아직 없다.
- `SM-G1`: play node crash와 복구 Node harness가 아직 없다.
- `SM-G2`: owner 이동 Node harness가 아직 없다.
- `SM-G3`: 동시 join/leave 경합 Node harness가 아직 없다.
- `SM-G4`: 다수 bound session push 부하 Node harness가 아직 없다.
