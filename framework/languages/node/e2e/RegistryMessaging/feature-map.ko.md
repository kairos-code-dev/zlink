# Node Location Messaging E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RM-A1 | done | Redis location store의 live peer row와 두 provider가 실제로 처리한 request evidence를 함께 검증한다. |
| RM-A2 | done | 수동 endpoint request를 검증한다. |
| RM-A4 | done | 같은 rid provider 교체를 검증한다. |
| RM-A6 | done | profile/workflow channel 독립성을 검증한다. |
| RM-B1 | done | provider scale-out 후 양쪽 provider 처리 evidence를 검증한다. |
| RM-B2 | done | provider scale-in 후 남은 provider만 처리하는지 검증한다. |
| RM-C1 | done | request/send happy path를 검증한다. |
| RM-C2 | done | route mesh target rid와 missing rid 실패를 검증한다. |
| RM-C3 | done | 수동 multi-endpoint 분산을 검증한다. |
| RM-C4 | done | timeout 뒤 후속 request 비오염을 검증한다. |
| RM-C5 | done | 미등록 packet request/send와 dispatch error evidence를 검증한다. |
| RM-C7 | done | public `configureServerSocket().weight`로 build-time provider weight 75/25를 설정하고 high-weight provider가 더 많이 처리하는지 검증한다. |
| RM-C8 | done | payload length/hash 왕복을 검증하고, server socket `maxMessageSize`를 넘는 payload가 실패한 뒤 정상 request가 다시 성공하는지 확인한다. |
| RM-C9 | done | 느린 provider에 다량 one-way send를 제출하고, public bounded-failure oracle 없이 backlog 해소 뒤 후속 request와 evidence가 회복되는지 검증한다. |

검증:

- `E2E_START_ORDER=reverse ./run_e2e.sh RM-A1`과
  `E2E_START_ORDER=shuffle:20260715 ./run_e2e.sh RM-A1`
  - 결과: 두 실행 모두 `scenario RM-A1 passed`, `registry-messaging e2e result=passed`

- `timeout 720s framework/languages/node/e2e/RegistryMessaging/run_e2e.sh all`
  - 결과: `registry-messaging e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260703-221206-46409`
  - 통과 scenario: `RM-A1`, `RM-A2`, `RM-A4`, `RM-A6`, `RM-B1`, `RM-B2`, `RM-C1`, `RM-C2`, `RM-C3`, `RM-C4`, `RM-C5`, `RM-C7`, `RM-C8`, `RM-C9`
- `timeout 420s framework/languages/node/e2e/RegistryMessaging/run_e2e.sh RM-C9`
  - 결과: `scenario RM-C9 passed`, `registry-messaging e2e result=passed`
  - 로그: `logs/20260701-040650-15231`

후속 계약 판정:

- `RM-C9`: public one-way send 제출과 recovery를 검증한다. send submit은 완료 객체나 bounded-failure
  oracle을 노출하지 않으므로, 직접적인 HWM 오류 결과 검증은 binding/runtime 내부 테스트 범위로 둔다.
