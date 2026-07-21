# Node.js ObservabilityOps 검증표

이 fixture는 Config 11 전용 `Session` 1개, `Play` 2개, `OrderWorkflow` 2개와 브라우저 trigger
client를 실행한다. 모든 역할은 한 Redis location store를 공유하며, 다른 E2E runner나 in-process
contract test의 결과를 대신 사용하지 않는다.

| 시나리오 | 실제 검증 내용 | 통과 로그 |
|----------|----------------|-----------|
| OBS-A1 | STREAM 요청의 flow가 Session과 Play actor 경계를 같은 UUIDv7로 통과 | `log/20260715-145426-3875923` |
| OBS-A2 | 알 수 없는 packet의 dispatch error에도 flow 기록 | `log/20260715-145500-3877445` |
| OBS-A3 | flow 기록을 끈 Session을 지나도 하류 Play에 flow 전파 | `log/20260715-145505-3878067` |
| OBS-A4 | 두 Workflow subscriber의 같은 fanout flow와 timer 발원 flow | `log/20260715-145540-3879939` |
| OBS-B1 | 실제 STREAM 세션 3개의 active/opened/closed와 connector reconnect 계기 | `log/20260715-145545-3880678` |
| OBS-B2 | 실제 actor/spot queue와 transfer 계기 | `log/20260715-145551-3881433` |
| OBS-B3 | fanout 1:N 계기와 Redis 지연으로 만든 owner lease lateness | `log/20260715-145625-3883168` |
| OBS-B4 | meter provider를 끈 Play의 정상 messaging과 빈 metric snapshot | `log/20260715-145633-3883819` |
| OBS-C1 | draining MeshNode descriptor, 신규 배치 제외, 진행 중 bound request 보존 | `log/20260715-145707-3885120` |
| OBS-C2 | play-a에서 play-b로 actor handoff 후 기존 STREAM으로 bound push 전달 | `log/20260715-145741-3886446` |
| OBS-C3 | 10.0.0 전환 대상 — 정상 request 뒤 Spot 유지, drain admission seal, accepted turn·actor·STREAM barrier 뒤 local Spot close·row 제거, stale handle의 숨은 원격 생성 금지와 명시적 local `GetOrCreate` 뒤 새 generation을 검증해야 한다. 현재 fixture는 제거 대상인 기존 분기 시나리오를 실행한다. | 없음 — 기존 로그는 고정 drain 회귀의 완료 증거가 아님 |
| OBS-C4 | deadline 강제 종료, ServerDrain 종료 사유와 forced metric | `log/20260715-145821-3888940` |
| OBS-C5 | serving target 순차 rollout과 zero-target 동시 drain | `log/20260715-145826-3889638`, `log/20260715-145900-3890992` |

`run_e2e.sh all`은 각 시나리오마다 Redis와 다섯 역할을 새로 실행한다. 모든 시나리오와 C5의 두
rollout 형태가 통과한 뒤에만 `observability-ops e2e result=passed`를 출력한다.
