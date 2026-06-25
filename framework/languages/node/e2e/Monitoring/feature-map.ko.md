# Node Monitoring E2E feature map

이 문서는 Config 7 Runtime Monitoring 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `MON-A1`: public dispatch diagnostics가 channel request 흐름을 trace log file에 남긴다.
- `MON-B1`: public dispatch diagnostics의 message flow mode 설정이 적용된 상태로 event가 기록된다.

## public API/harness 대기

- `MON-A2`: registry 이벤트 관찰 Node runner와 marker가 아직 없다.
- `MON-A3`: spot 이벤트 관찰 Node runner와 marker가 아직 없다.
- `MON-A4`: 가용성 전이 관측 Node harness가 아직 없다.
- `MON-A5`: handshake/status/timer-stopped kind Node trigger가 아직 없다.
- `MON-B2`: invalid monitoring 등록을 public contract로 검증하는 Node marker가 아직 없다.
- `MON-C1`: event dispatch 실패 격리 Node runner와 marker가 아직 없다.
- `MON-D1`: 장애/복구 반복 중 관측 연속성 Node harness가 아직 없다.
