# C++ SpotActorTransfer E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-10-spot-actor-transfer.ko.md`

이 문서는 C++ Config 10 E2E의 현재 구현 상태를 공통 시나리오별로 기록한다. 모든 시나리오 ID가
client와 `run_e2e.sh all`에 등록되어 있지만, 현재 배치와 일부 검증은 공통 완료 기준을 충족하지
않는다. 따라서 실행 코드가 있다는 이유만으로 `implemented`로 표시하지 않는다.

현재 runner는 actor 노드 세 개만 시작하고 각 노드가 HTTP 제어 endpoint와 stream endpoint를 함께
제공한다. 공통 문서가 요구하는 actor 노드 두 개, session gateway 두 개, transfer controller 한 개의
역할 분리는 아직 구현되지 않았다(`E2E-CP-56`). 이 배치 차이를 해소하기 전까지 Config 10 전체는
완료로 판정할 수 없다.

| 시나리오 | 상태 | 현재 구현과 남은 gap |
|----------|------|----------------------|
| ST-A1 | `deferred` | local admission부터 성공 응답까지의 순서를 client가 검사한다. 역할별 배치가 공통 구성과 다르다(`E2E-CP-56`). |
| ST-A2 | `deferred` | local admission 거절과 joined side effect 부재를 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-A3 | `deferred` | joined gate가 유지되는 동안 actor packet이 완료되지 않는지 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-B1 | `deferred` | stateful remote transfer와 target state를 검사하지만 `commit_ack`, `source_cleanup` 순서 evidence가 없다(`E2E-CP-51`, `E2E-CP-56`). |
| ST-B2 | `deferred` | transfer가 끝난 뒤 source를 중단하므로 commit과 cleanup 사이의 장애를 만들지 못한다(`E2E-CP-52`, `E2E-CP-56`). |
| ST-B3 | `deferred` | adapter 미등록 시 빈 state transfer를 검사하지만 전체 callback 순서 evidence가 없다(`E2E-CP-51`, `E2E-CP-56`). |
| ST-B4 | `deferred` | 명시적인 빈 state transfer 뒤 target state를 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-C1 | `deferred` | commit 전 source 중단과 target negative를 검사하지만 pending admission timeout cleanup evidence가 없다(`E2E-CP-52`, `E2E-CP-56`). |
| ST-C2 | `deferred` | target commit 뒤 source 중단 후 location과 bound push를 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-C3 | `deferred` | callback failure 네 종류를 실행하지만 joined 실패 뒤 packet negative는 실제 packet을 보내지 않아 판별력이 없다(`E2E-CP-52`, `E2E-CP-56`). |
| ST-D1 | `deferred` | local·remote location은 joined 완료 전 기존 ref를 유지하고 완료 뒤 committed ref로 바뀌며, local 지연 중 packet이 target handler에 먼저 도달하지 않고 commit 뒤 target에서 처리되는지 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-D2 | `deferred` | transfer 뒤 대기와 재조회만 수행하며 stale source cleanup 지연과 실행을 주입하지 않는다(`E2E-CP-52`, `E2E-CP-56`). |
| ST-E1 | `deferred` | transfer 전후 같은 connector의 bound push 수신을 검사한다. 별도 session gateway 역할이 없다(`E2E-CP-56`). |
| ST-E2 | `deferred` | 실패한 transfer의 route 비오염 대신 성공한 transfer 뒤 새 session rebind를 검사한다(`E2E-CP-49`, `E2E-CP-56`). |
| ST-F1 | `deferred` | target 처리 순서는 검사하지만 필수 handoff marker 부재가 실행 실패로 이어지지 않고 stderr 문자열에 의존한다(`E2E-CP-50`, `E2E-CP-57`). |
| ST-F2 | `deferred` | direct packet을 transfer 완료 대기 뒤 보내 실제 추월 경합을 만들지 못하며 필수 marker도 단언하지 않는다(`E2E-CP-50`, `E2E-CP-53`, `E2E-CP-57`). |
| ST-F3 | `deferred` | rebind 이후 packet을 transfer 완료 대기 뒤 보내 cross-move 경합 창을 닫는다(`E2E-CP-53`, `E2E-CP-57`). |
| ST-F4 | `deferred` | window 전후 packet의 message kind가 달라 같은 send 동작의 forwarding과 fail-fast를 비교하지 못한다(`E2E-CP-54`, `E2E-CP-57`). |
| ST-F5 | `deferred` | stale probe로 축출을 추론할 뿐 entry snapshot과 필수 marker를 합격 조건으로 검사하지 않는다(`E2E-CP-50`, `E2E-CP-52`, `E2E-CP-57`). |
| ST-F6 | `deferred` | reply correlation과 timeout 결과는 검사하지만 handoff 과정의 request id와 flags 보존 evidence는 stderr 경로에 의존한다(`E2E-CP-57`). |

## 실행 범위

`run_e2e.sh all`은 ST-A1부터 ST-F6까지 스무 시나리오를 모두 선택한다. source process 중단이 필요한
ST-B2, ST-C1, ST-C2는 다른 시나리오와 분리해 실행하며, 그 사이에 actor-a를 다시 시작한다. 이 목록은
시나리오가 실행 대상으로 등록되어 있음을 뜻할 뿐, 위 표에 적은 gap의 완료 근거는 아니다.

Config 10을 완료하려면 먼저 역할별 process 배치를 공통 구성에 맞추고, 각 `E2E-CP-*` 항목의 실패
게이트를 만든 뒤 시나리오 단언을 보강해야 한다. public framework 계약에 없는 기능이 필요하면
내부 helper나 테스트 전용 API로 우회하지 않고 별도 계약 검토 대상으로 남긴다.
