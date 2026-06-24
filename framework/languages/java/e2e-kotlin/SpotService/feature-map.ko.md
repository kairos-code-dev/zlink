# Kotlin SpotService E2E feature map

이 문서는 Config 2 SpotService 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. Kotlin wrapper는
Java runtime 의미를 공유하지만, 이 디렉터리에는 아직 추적된 Kotlin SpotService runner/source가 없다.
따라서 현재 Kotlin 전용 stdout marker로 구현 완료를 주장하지 않는다.

## 구현됨

- 없음.

## public API/harness 대기

- `SM-A1`: entry/user spot 생성 Kotlin runner와 marker가 아직 없다.
- `SM-A2`: user spot request와 state mutation Kotlin runner와 marker가 아직 없다.
- `SM-A3`: route resolver 정확성을 Kotlin runner로 확인하는 marker가 아직 없다.
- `SM-A4`: owner routing key mapping을 Kotlin runner로 확인하는 marker가 아직 없다.
- `SM-A5`: Stage wrapper 계층을 Kotlin runner로 확인하는 marker가 아직 없다.
- `SM-A6`: spot initialize/close lifecycle Kotlin runner와 marker가 아직 없다.
- `SM-A7`: spot type mismatch public error Kotlin runner와 marker가 아직 없다.
- `SM-A8`: worker offload와 spot 직렬성 Kotlin runner와 marker가 아직 없다.
- `SM-B1`: local actor join Kotlin runner와 marker가 아직 없다.
- `SM-B2`: remote actor join Kotlin runner와 marker가 아직 없다.
- `SM-B3`: actor join/request payload fidelity Kotlin runner와 marker가 아직 없다.
- `SM-B4`: remote actor request Kotlin runner와 marker가 아직 없다.
- `SM-B5`: actor 미등록 request negative path Kotlin runner와 marker가 아직 없다.
- `SM-B6`: leave와 disconnect callback 차이를 Kotlin runner로 확인하는 marker가 아직 없다.
- `SM-B7`: actor lifecycle과 packet handler 순서 Kotlin runner와 marker가 아직 없다.
- `SM-B8`: actor explicit destroy Kotlin runner와 marker가 아직 없다.
- `SM-C1`: channel to spot messaging Kotlin runner와 marker가 아직 없다.
- `SM-C2`: spot to channel messaging Kotlin runner와 marker가 아직 없다.
- `SM-C3`: spot to spot messaging Kotlin runner와 marker가 아직 없다.
- `SM-C4`: spot publisher client Kotlin runner와 marker가 아직 없다.
- `SM-D1`: local stream session bind/relay Kotlin runner와 marker가 아직 없다.
- `SM-D2`: remote stream session bind/relay Kotlin runner와 marker가 아직 없다.
- `SM-D3`: entry spot actor bind와 user spot actor bind 비교 Kotlin runner와 marker가 아직 없다.
- `SM-D4`: multi actor bind Kotlin runner와 marker가 아직 없다.
- `SM-D5`: session disconnect actor notification Kotlin runner와 marker가 아직 없다.
- `SM-D6`: bound session push target isolation Kotlin runner와 marker가 아직 없다.
- `SM-D7`: stream auth와 packet dispatch Kotlin runner와 marker가 아직 없다.
- `SM-D8`: stream reconnect Kotlin runner와 marker가 아직 없다.
- `SM-D9`: stream inbound observer Kotlin runner와 marker가 아직 없다.
- `SM-D10`: stream backpressure Kotlin runner와 marker가 아직 없다.
- `SM-D11`: stream request와 channel request 혼합 Kotlin runner와 marker가 아직 없다.
- `SM-D12`: 다른 gateway 재접속 Kotlin runner와 marker가 아직 없다.
- `SM-D13`: stream heartbeat Kotlin runner와 marker가 아직 없다.
- `SM-D14`: TLS stream Kotlin runner와 marker가 아직 없다.
- `SM-E1`: spot route 미등록 request Kotlin runner와 marker가 아직 없다.
- `SM-E2`: spot timer Kotlin runner와 marker가 아직 없다.
- `SM-E3`: idle timer 기반 close Kotlin runner와 marker가 아직 없다.
- `SM-E4`: timer overrun policy Kotlin runner와 marker가 아직 없다.
- `SM-F1`: client/server channel to target spot Kotlin runner와 marker가 아직 없다.
- `SM-F2`: route mesh channel to target spot Kotlin runner와 marker가 아직 없다.
- `SM-F3`: 일반 packet과 spot route packet 공존 Kotlin runner와 marker가 아직 없다.
- `SM-F4`: spot route negative 계약 Kotlin runner와 marker가 아직 없다.
- `SM-F5`: channel socket ownership 독립성 Kotlin runner와 marker가 아직 없다.
- `SM-G1`: play node crash와 복구 Kotlin harness가 아직 없다.
- `SM-G2`: owner 이동 Kotlin harness가 아직 없다.
- `SM-G3`: 동시 join/leave 경합 Kotlin harness가 아직 없다.
- `SM-G4`: 다수 bound session push 부하 Kotlin harness가 아직 없다.
