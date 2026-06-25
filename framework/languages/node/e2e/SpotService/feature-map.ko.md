# Node SpotService E2E feature map

이 문서는 Config 2 SpotService 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `SM-A1`: public NestJS spot mesh 등록 뒤 spot manager가 user spot을 생성한다.
- `SM-A2`: user spot create request payload가 spot state mutation에 사용된다.
- `SM-A6`: public app lifecycle 안에서 spot manager create/close 경로를 검증한다.

## public API/harness 대기

- `SM-A3`: route resolver 정확성 Node runner와 marker가 아직 없다.
- `SM-A4`: owner routing key mapping Node runner와 marker가 아직 없다.
- `SM-A5`: Stage wrapper Node runner와 marker가 아직 없다.
- `SM-A7`: spot type mismatch Node runner와 marker가 아직 없다.
- `SM-A8`: worker offload Node runner와 marker가 아직 없다.
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
- `SM-E2`: spot timer Node runner와 marker가 아직 없다.
- `SM-E3`: idle timer close Node runner와 marker가 아직 없다.
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
