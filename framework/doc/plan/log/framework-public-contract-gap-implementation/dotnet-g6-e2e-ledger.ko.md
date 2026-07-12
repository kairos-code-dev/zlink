# .NET G6 E2E 시나리오 ledger

이 문서는 공통 E2E spec의 모든 시나리오를 .NET fixture와 실행 증거에 대응시킨다.
계획 문서에는 gate 상태만 유지하고, 세부 실행 결과는 이 ledger에서 관리한다.

## 실행 기준

- 실행 명령: `cd framework/languages/dotnet && ./e2e/run_e2e_all.sh`
- 실행 시각: 2026-07-12 21:20:25 KST ~ 21:48:43 KST
- 결과: exit code 0, `[dotnet-e2e] total PASS (1698s)`
- 범위: config 1~11, 공통 시나리오 181개
- selector: 각 config의 `all`; 아래 모든 행은 aggregate runner에 포함됐다.
- 비적용: 없음
- cross-language: .NET은 첫 번째 구현 언어이므로 이전 G7 완료 언어가 없다. §8.7의 현재 언어 대 이전 완료 언어 양방향 행은 공집합이며, Java G6부터 .NET과의 양방향 검증을 추가한다.

## Config 완료 요약

| config | 공통 문서 | 시나리오 수 | fixture | 결과 | 로그 |
|--------|-----------|------------:|---------|------|------|
| 1 | `config-1-location-messaging.ko.md` | 14 | `e2e/LocationMessaging` | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272` |
| 2 | `config-2-spot-service.ko.md` | 51 | `e2e/SpotService` | PASS | `e2e/SpotService/logs/20260712-213524-2731605` 및 같은 aggregate의 child log |
| 3 | `config-3-pubsub.ko.md` | 7 | `e2e/PubSub` | PASS | `e2e/PubSub/logs/20260712-212436-2721534` |
| 4 | `config-4-registration-codec.ko.md` | 11 | `e2e/RegistrationCodec` | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212` |
| 5 | `config-5-resilience-lifecycle.ko.md` | 20 | `e2e/ResilienceLifecycle` | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817` |
| 6 | `config-6-store-failure-recovery.ko.md` | 10 | `e2e/StoreFailure` | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801` |
| 7 | `config-7-monitoring.ko.md` | 9 | `e2e/RuntimeMonitoring` | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393` |
| 8 | `config-8-automatic-turn-dispatch.ko.md` | 19 | `e2e/AutomaticTurnDispatch` | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257` |
| 9 | `config-9-to-actor-messaging.ko.md` | 7 | `e2e/ToActorMessaging` | PASS | `e2e/ToActorMessaging/logs/20260712-214352-2742431` |
| 10 | `config-10-spot-actor-transfer.ko.md` | 20 | `e2e/SpotActorTransfer` | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512` |
| 11 | `config-11-observability-ops.ko.md` | 13 | `e2e/ObservabilityOps` | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004` |

합계는 181개다. 각 행의 marker는 시나리오 내부 assertion이 모두 끝난 뒤 출력되며,
aggregate runner의 config PASS와 최종 exit code 0을 함께 완료 증거로 사용한다.

## 시나리오 대응표

| config/scenario ID | 언어 | runner selector | 구현/fixture | all runner 포함 | 결과 | 로그/marker | 비적용 승인 근거 |
|--------------------|------|-----------------|--------------|-----------------|------|-------------|------------------|
| 1/RM-A1 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-A1 PASS` | - |
| 1/RM-A2 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-A2 PASS` | - |
| 1/RM-A4 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-A4 PASS` | - |
| 1/RM-A6 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-A6 PASS` | - |
| 1/RM-B1 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-B1 PASS` | - |
| 1/RM-B2 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-B2 PASS` | - |
| 1/RM-C1 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-C1 PASS` | - |
| 1/RM-C2 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-C2 PASS` | - |
| 1/RM-C3 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-C3 PASS` | - |
| 1/RM-C4 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-C4 PASS` | - |
| 1/RM-C5 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-C5 PASS` | - |
| 1/RM-C7 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-C7 PASS` | - |
| 1/RM-C8 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-C8 PASS` | - |
| 1/RM-C9 | .NET | `all` | `e2e/LocationMessaging` | 예 | PASS | `e2e/LocationMessaging/logs/20260712-212025-2717272`; client.stdout.log의 `[LocationMessaging] RM-C9 PASS` | - |
| 2/SM-A1 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-a1 passed` | - |
| 2/SM-A2 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-a2 passed` | - |
| 2/SM-A3 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-a3 passed` | - |
| 2/SM-A4 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-a4 passed` | - |
| 2/SM-A5 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-a5 passed` | - |
| 2/SM-A6 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-a6 passed` | - |
| 2/SM-A7 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-a7 passed` | - |
| 2/SM-A8 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-a8 passed` | - |
| 2/SM-B1 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-b1 passed` | - |
| 2/SM-B2 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-b2 passed` | - |
| 2/SM-B3 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-b3 passed` | - |
| 2/SM-B4 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-b4 passed` | - |
| 2/SM-B5 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-b5 passed` | - |
| 2/SM-B6 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-b6 passed` | - |
| 2/SM-B7 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-b7 passed` | - |
| 2/SM-B8 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-b8 passed` | - |
| 2/SM-B9 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-b9 passed` | - |
| 2/SM-C1 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-c1 passed` | - |
| 2/SM-C2 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-c2 passed` | - |
| 2/SM-C3 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-c3 passed` | - |
| 2/SM-C4 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-c4 passed` | - |
| 2/SM-C5 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-c5 passed` | - |
| 2/SM-D1 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d1 passed` | - |
| 2/SM-D2 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d2 passed` | - |
| 2/SM-D3 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d3 passed` | - |
| 2/SM-D4 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d4 passed` | - |
| 2/SM-D5 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d5 passed` | - |
| 2/SM-D6 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d6 passed` | - |
| 2/SM-D7 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d7 passed` | - |
| 2/SM-D8 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d8 passed` | - |
| 2/SM-D9 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d9 passed` | - |
| 2/SM-D10 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d10 passed` | - |
| 2/SM-D11 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d11 passed` | - |
| 2/SM-D12 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d12 passed` | - |
| 2/SM-D13 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d13 passed` | - |
| 2/SM-D14 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d14 passed` | - |
| 2/SM-D15 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-d15 passed` | - |
| 2/SM-E1 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-e1 passed` | - |
| 2/SM-E2 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-e2 passed` | - |
| 2/SM-E3 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-e3 passed` | - |
| 2/SM-E4 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-e4 passed` | - |
| 2/SM-F1 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-f1 passed` | - |
| 2/SM-F2 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-f2 passed` | - |
| 2/SM-F3 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-f3 passed` | - |
| 2/SM-F4 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-f4 passed` | - |
| 2/SM-F5 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-f5 passed` | - |
| 2/SM-F6 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-f6 passed` | - |
| 2/SM-G1 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-g1 passed` | - |
| 2/SM-G2 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-g2 passed` | - |
| 2/SM-G3 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-g3 passed` | - |
| 2/SM-G4 | .NET | `all` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260712-213524-2731605 및 같은 aggregate의 child log`; client stdout의 `operation SpotService.sm-g4 passed` | - |
| 3/PS-A1 | .NET | `all` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260712-212436-2721534`; client.stdout.log의 `scenario PS-A1 passed` | - |
| 3/PS-A2 | .NET | `all` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260712-212436-2721534`; client.stdout.log의 `scenario PS-A2 passed` | - |
| 3/PS-A3 | .NET | `all` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260712-212436-2721534`; client.stdout.log의 `scenario PS-A3 passed` | - |
| 3/PS-A4 | .NET | `all` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260712-212436-2721534`; client.stdout.log의 `scenario PS-A4 passed` | - |
| 3/PS-B1 | .NET | `all` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260712-212436-2721534`; client.stdout.log의 `scenario PS-B1 passed` | - |
| 3/PS-B2 | .NET | `all` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260712-212436-2721534`; client.stdout.log의 `scenario PS-B2 passed` | - |
| 3/PS-C1 | .NET | `all` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260712-212436-2721534`; client.stdout.log의 `scenario PS-C1 passed` | - |
| 4/RC-A1 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-A1 passed` | - |
| 4/RC-A2 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-A2 passed` | - |
| 4/RC-A3 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-A3 passed` | - |
| 4/RC-A4 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-A4 passed` | - |
| 4/RC-A5 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-A5 passed` | - |
| 4/RC-A6 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-A6 passed` | - |
| 4/RC-B1 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-B1 passed` | - |
| 4/RC-B2 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-B2 passed` | - |
| 4/RC-B3 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-B3 passed` | - |
| 4/RC-B4 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-B4 passed` | - |
| 4/RC-B5 | .NET | `all` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260712-212526-2723212`; client.stdout.log의 `scenario RC-B5 passed` | - |
| 5/RL-A1 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-A1 passed` | - |
| 5/RL-A2 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-A2 passed` | - |
| 5/RL-A3 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-A3 passed` | - |
| 5/RL-A4 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-A4 passed` | - |
| 5/RL-A5 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-A5 passed` | - |
| 5/RL-B1 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-B1 passed` | - |
| 5/RL-B2 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-B2 passed` | - |
| 5/RL-B3 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-B3 passed` | - |
| 5/RL-B4 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-B4 passed` | - |
| 5/RL-B5 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-B5 passed` | - |
| 5/RL-B6 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-B6 passed` | - |
| 5/RL-C1 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-C1 passed` | - |
| 5/RL-C2 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-C2 passed` | - |
| 5/RL-C3 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-C3 passed` | - |
| 5/RL-C4 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-C4 passed` | - |
| 5/RL-D1 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-D1 passed` | - |
| 5/RL-D2 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-D2 passed` | - |
| 5/RL-D3 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-D3 passed` | - |
| 5/RL-D4 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-D4 passed` | - |
| 5/RL-D5 | .NET | `all` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260712-212543-2723817`; client.stdout.log의 `scenario RL-D5 passed` | - |
| 6/SF-A1 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-A1 passed` | - |
| 6/SF-A2 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-A2 passed` | - |
| 6/SF-B1 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-B1 passed` | - |
| 6/SF-B2 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-B2 passed` | - |
| 6/SF-C1 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-C1 passed` | - |
| 6/SF-C2 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-C2 passed` | - |
| 6/SF-D1 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-D1 passed` | - |
| 6/SF-D2 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-D2 passed` | - |
| 6/SF-D3 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-D3 passed` | - |
| 6/SF-E1 | .NET | `all` | `e2e/StoreFailure` | 예 | PASS | `e2e/StoreFailure/logs/20260712-214212-2740801`; client.stdout.log의 `scenario SF-E1 passed` | - |
| 7/MON-A1 | .NET | `all` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393`; client.stdout.log의 `scenario MON-A1 passed` | - |
| 7/MON-A2 | .NET | `all` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393`; client.stdout.log의 `scenario MON-A2 passed` | - |
| 7/MON-A3 | .NET | `all` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393`; client.stdout.log의 `scenario MON-A3 passed` | - |
| 7/MON-A4 | .NET | `all` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393`; client.stdout.log의 `scenario MON-A4 passed` | - |
| 7/MON-A5 | .NET | `all` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393`; client.stdout.log의 `scenario MON-A5 passed` | - |
| 7/MON-B1 | .NET | `all` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393`; client.stdout.log의 `scenario MON-B1 passed` | - |
| 7/MON-B2 | .NET | `all` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393`; client.stdout.log의 `scenario MON-B2 passed` | - |
| 7/MON-C1 | .NET | `all` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393`; client.stdout.log의 `scenario MON-C1 passed` | - |
| 7/MON-D1 | .NET | `all` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260712-213436-2730393`; client.stdout.log의 `scenario MON-D1 passed` | - |
| 8/ATD-A1 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-A1` assertion 및 aggregate PASS | - |
| 8/ATD-A2 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-A2` assertion 및 aggregate PASS | - |
| 8/ATD-A3 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-A3` assertion 및 aggregate PASS | - |
| 8/ATD-A4 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-A4` assertion 및 aggregate PASS | - |
| 8/ATD-B1 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-B1` assertion 및 aggregate PASS | - |
| 8/ATD-B2 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-B2` assertion 및 aggregate PASS | - |
| 8/ATD-B3 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-B3` assertion 및 aggregate PASS | - |
| 8/ATD-C1 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-C1` assertion 및 aggregate PASS | - |
| 8/ATD-C2 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-C2` assertion 및 aggregate PASS | - |
| 8/ATD-C3 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-C3` assertion 및 aggregate PASS | - |
| 8/ATD-D1 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-D1` assertion 및 aggregate PASS | - |
| 8/ATD-D2 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-D2` assertion 및 aggregate PASS | - |
| 8/ATD-D3 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-D3` assertion 및 aggregate PASS | - |
| 8/ATD-D4 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-D4` assertion 및 aggregate PASS | - |
| 8/ATD-E1 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-E1` assertion 및 aggregate PASS | - |
| 8/ATD-E2 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-E2` assertion 및 aggregate PASS | - |
| 8/ATD-E3 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-E3` assertion 및 aggregate PASS | - |
| 8/ATD-E4 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-E4` assertion 및 aggregate PASS | - |
| 8/ATD-E5 | .NET | `all` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260712-214416-2743257`; client/runner의 `ATD-E5` assertion 및 aggregate PASS | - |
| 9/TA-A1 | .NET | `all` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260712-214352-2742431`; Client/Program.cs의 `TA-A1` evidence assertion 및 runner PASS | - |
| 9/TA-A2 | .NET | `all` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260712-214352-2742431`; Client/Program.cs의 `TA-A2` evidence assertion 및 runner PASS | - |
| 9/TA-A3 | .NET | `all` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260712-214352-2742431`; Client/Program.cs의 `TA-A3` evidence assertion 및 runner PASS | - |
| 9/TA-A4 | .NET | `all` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260712-214352-2742431`; Client/Program.cs의 `TA-A4` evidence assertion 및 runner PASS | - |
| 9/TA-B1 | .NET | `all` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260712-214352-2742431`; Client/Program.cs의 `TA-B1` evidence assertion 및 runner PASS | - |
| 9/TA-B2 | .NET | `all` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260712-214352-2742431`; Client/Program.cs의 `TA-B2` evidence assertion 및 runner PASS | - |
| 9/TA-B3 | .NET | `all` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260712-214352-2742431`; Client/Program.cs의 `TA-B3` evidence assertion 및 runner PASS | - |
| 10/ST-A1 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-A1 passed` | - |
| 10/ST-A2 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-A2 passed` | - |
| 10/ST-A3 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-A3 passed` | - |
| 10/ST-B1 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-B1 passed` | - |
| 10/ST-B2 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-B2 passed` | - |
| 10/ST-B3 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-B3 passed` | - |
| 10/ST-B4 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-B4 passed` | - |
| 10/ST-C1 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-C1 passed` | - |
| 10/ST-C2 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-C2 passed` | - |
| 10/ST-C3 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-C3 passed` | - |
| 10/ST-D1 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-D1 passed` | - |
| 10/ST-D2 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-D2 passed` | - |
| 10/ST-E1 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-E1 passed` | - |
| 10/ST-E2 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-E2 passed` | - |
| 10/ST-F1 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-F1 passed` | - |
| 10/ST-F2 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-F2 passed` | - |
| 10/ST-F3 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-F3 passed` | - |
| 10/ST-F4 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-F4 passed` | - |
| 10/ST-F5 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-F5 passed` | - |
| 10/ST-F6 | .NET | `all` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/logs/20260712-214035-2738512`; client stdout의 `operation SpotActorTransfer.ST-F6 passed` | - |
| 11/OBS-A1 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-A1 PASS`와 fixture evidence | - |
| 11/OBS-A2 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-A2 PASS`와 fixture evidence | - |
| 11/OBS-A3 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-A3 PASS`와 fixture evidence | - |
| 11/OBS-A4 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-A4 PASS`와 fixture evidence | - |
| 11/OBS-B1 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-B1 PASS`와 fixture evidence | - |
| 11/OBS-B2 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-B2 PASS`와 fixture evidence | - |
| 11/OBS-B3 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-B3 PASS`와 fixture evidence | - |
| 11/OBS-B4 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-B4 PASS`와 fixture evidence | - |
| 11/OBS-C1 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-C1 PASS`와 fixture evidence | - |
| 11/OBS-C2 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-C2 PASS`와 fixture evidence | - |
| 11/OBS-C3 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-C3 PASS`와 fixture evidence | - |
| 11/OBS-C4 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-C4 PASS`와 fixture evidence | - |
| 11/OBS-C5 | .NET | `all` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260712-214601-2746004`; run_e2e.sh의 `OBS-C5 PASS`와 fixture evidence | - |

## 완결성 검증

- 공통 문서 heading에서 추출한 고유 scenario ID: 181개
- ledger scenario 행: 181개
- 중복 ID: 없음
- 누락 fixture/selector/log/marker: 없음
- aggregate에 포함되지 않은 실행 가능 fixture: 없음
- retry로 성공 처리된 config: 없음
