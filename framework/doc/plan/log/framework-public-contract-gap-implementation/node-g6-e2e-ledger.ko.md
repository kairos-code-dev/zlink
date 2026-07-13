# Node.js G6 E2E 시나리오 ledger

검증일은 2026-07-14이다. 공통 E2E 문서의 scenario heading 181개를 Node.js fixture와
`./e2e/run_e2e_all.sh`의 실제 실행 결과에 한 행씩 연결한다. 최종 aggregate 실행은 11개 config를
포함한 모든 family를 1,218초 동안 순서대로 실행했고 `[node-e2e] total PASS (1218s)`로 끝났다.
이 실행에서 `ObservabilityOps`는 분리 실행된 모든 `log_dir`의 flow 증거를 합산해 OBS-A1~C5를
검증했고 `observability-ops e2e result=passed`를 기록했다.

| config/scenario ID | runner selector | 구현/fixture | all runner 포함 | 결과 | 로그/marker |
|---|---|---|---|---|---|
| 1/RM-A1 | `RM-A1` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-A1 passed` |
| 1/RM-A2 | `RM-A2` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-A2 passed` |
| 1/RM-A4 | `RM-A4` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-A4 passed` |
| 1/RM-A6 | `RM-A6` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-A6 passed` |
| 1/RM-B1 | `RM-B1` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-B1 passed` |
| 1/RM-B2 | `RM-B2` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-B2 passed` |
| 1/RM-C1 | `RM-C1` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-C1 passed` |
| 1/RM-C2 | `RM-C2` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-C2 passed` |
| 1/RM-C3 | `RM-C3` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-C3 passed` |
| 1/RM-C4 | `RM-C4` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-C4 passed` |
| 1/RM-C5 | `RM-C5` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-C5 passed` |
| 1/RM-C7 | `RM-C7` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-C7 passed` |
| 1/RM-C8 | `RM-C8` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-C8 passed` |
| 1/RM-C9 | `RM-C9` | `e2e/RegistryMessaging` | 예 | PASS | `e2e/RegistryMessaging/logs/20260713-112357-367763`; `scenario RM-C9 passed` |
| 2/SM-A1 | `SM-A1` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-A1 passed` |
| 2/SM-A2 | `SM-A2` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-A2 passed` |
| 2/SM-A3 | `SM-A3` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-A3 passed` |
| 2/SM-A4 | `SM-A4` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-A4 passed` |
| 2/SM-A5 | `SM-A5` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-A5 passed` |
| 2/SM-A6 | `SM-A6` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-A6 passed` |
| 2/SM-A7 | `SM-A7` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-A7 passed` |
| 2/SM-A8 | `SM-A8` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-A8 passed` |
| 2/SM-B1 | `SM-B1` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-B1 passed` |
| 2/SM-B2 | `SM-B2` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-B2 passed` |
| 2/SM-B3 | `SM-B3` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-B3 passed` |
| 2/SM-B4 | `SM-B4` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-B4 passed` |
| 2/SM-B5 | `SM-B5` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-B5 passed` |
| 2/SM-B6 | `SM-B6` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-B6 passed` |
| 2/SM-B7 | `SM-B7` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-B7 passed` |
| 2/SM-B8 | `SM-B8` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-B8 passed` |
| 2/SM-B9 | `SM-B9` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-B9 passed` |
| 2/SM-C1 | `SM-C1` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-C1 passed` |
| 2/SM-C2 | `SM-C2` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-C2 passed` |
| 2/SM-C3 | `SM-C3` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-C3 passed` |
| 2/SM-C4 | `SM-C4` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-C4 passed` |
| 2/SM-C5 | `SM-C5` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-C5 passed` |
| 2/SM-D1 | `SM-D1` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D1 passed` |
| 2/SM-D2 | `SM-D2` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D2 passed` |
| 2/SM-D3 | `SM-D3` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D3 passed` |
| 2/SM-D4 | `SM-D4` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D4 passed` |
| 2/SM-D5 | `SM-D5` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D5 passed` |
| 2/SM-D6 | `SM-D6` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D6 passed` |
| 2/SM-D7 | `SM-D7` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D7 passed` |
| 2/SM-D8 | `SM-D8` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D8 passed` |
| 2/SM-D9 | `SM-D9` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D9 passed` |
| 2/SM-D10 | `SM-D10` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D10 passed` |
| 2/SM-D11 | `SM-D11` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D11 passed` |
| 2/SM-D12 | `SM-D12` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D12 passed` |
| 2/SM-D13 | `SM-D13` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D13 passed` |
| 2/SM-D14 | `SM-D14` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D14 passed` |
| 2/SM-D15 | `SM-D15` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-D15 passed` |
| 2/SM-E1 | `SM-E1` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-E1 passed` |
| 2/SM-E2 | `SM-E2` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-E2 passed` |
| 2/SM-E3 | `SM-E3` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-E3 passed` |
| 2/SM-E4 | `SM-E4` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-E4 passed` |
| 2/SM-F1 | `SM-F1` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-F1 passed` |
| 2/SM-F2 | `SM-F2` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-F2 passed` |
| 2/SM-F3 | `SM-F3` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-F3 passed` |
| 2/SM-F4 | `SM-F4` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-F4 passed` |
| 2/SM-F5 | `SM-F5` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-F5 passed` |
| 2/SM-F6 | `SM-F6` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-F6 passed` |
| 2/SM-G1 | `SM-G1` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-G1 passed` |
| 2/SM-G2 | `SM-G2` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-G2 passed` |
| 2/SM-G3 | `SM-G3` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-G3 passed` |
| 2/SM-G4 | `SM-G4` | `e2e/SpotService` | 예 | PASS | `e2e/SpotService/logs/20260713-112519-375192 및 child log`; `scenario SM-G4 passed` |
| 3/PS-A1 | `PS-A1` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260713-112507-374509`; `scenario PS-A1 passed` |
| 3/PS-A2 | `PS-A2` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260713-112507-374509`; `scenario PS-A2 passed` |
| 3/PS-A3 | `PS-A3` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260713-112507-374509`; `scenario PS-A3 passed` |
| 3/PS-A4 | `PS-A4` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260713-112507-374509`; `scenario PS-A4 passed` |
| 3/PS-B1 | `PS-B1` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260713-112507-374509`; `scenario PS-B1 passed` |
| 3/PS-B2 | `PS-B2` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260713-112507-374509`; `scenario PS-B2 passed` |
| 3/PS-C1 | `PS-C1` | `e2e/PubSub` | 예 | PASS | `e2e/PubSub/logs/20260713-112507-374509`; `scenario PS-C1 passed` |
| 4/RC-A1 | `RC-A1` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-A1 passed` |
| 4/RC-A2 | `RC-A2` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-A2 passed` |
| 4/RC-A3 | `RC-A3` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-A3 passed` |
| 4/RC-A4 | `RC-A4` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-A4 passed` |
| 4/RC-A5 | `RC-A5` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-A5 passed` |
| 4/RC-A6 | `RC-A6` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-A6 passed` |
| 4/RC-B1 | `RC-B1` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-B1 passed` |
| 4/RC-B2 | `RC-B2` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-B2 passed` |
| 4/RC-B3 | `RC-B3` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-B3 passed` |
| 4/RC-B4 | `RC-B4` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-B4 passed` |
| 4/RC-B5 | `RC-B5` | `e2e/RegistrationCodec` | 예 | PASS | `e2e/RegistrationCodec/logs/20260713-112351-367400`; `scenario RC-B5 passed` |
| 5/RL-A1 | `RL-A1` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-A1 passed` |
| 5/RL-A2 | `RL-A2` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-A2 passed` |
| 5/RL-A3 | `RL-A3` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-A3 passed` |
| 5/RL-A4 | `RL-A4` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-A4 passed` |
| 5/RL-A5 | `RL-A5` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-A5 passed` |
| 5/RL-B1 | `RL-B1` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-B1 passed` |
| 5/RL-B2 | `RL-B2` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-B2 passed` |
| 5/RL-B3 | `RL-B3` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-B3 passed` |
| 5/RL-B4 | `RL-B4` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-B4 passed` |
| 5/RL-B5 | `RL-B5` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-B5 passed` |
| 5/RL-B6 | `RL-B6` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-B6 passed` |
| 5/RL-C1 | `RL-C1` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-C1 passed` |
| 5/RL-C2 | `RL-C2` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-C2 passed` |
| 5/RL-C3 | `RL-C3` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-C3 passed` |
| 5/RL-C4 | `RL-C4` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-C4 passed` |
| 5/RL-D1 | `RL-D1` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-D1 passed` |
| 5/RL-D2 | `RL-D2` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-D2 passed` |
| 5/RL-D3 | `RL-D3` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-D3 passed` |
| 5/RL-D4 | `RL-D4` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-D4 passed` |
| 5/RL-D5 | `RL-D5` | `e2e/ResilienceLifecycle` | 예 | PASS | `e2e/ResilienceLifecycle/logs/20260713-112705-385327`; `scenario RL-D5 passed` |
| 6/SF-A1 | `SF-A1` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-A1 passed` |
| 6/SF-A2 | `SF-A2` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-A2 passed` |
| 6/SF-B1 | `SF-B1` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-B1 passed` |
| 6/SF-B2 | `SF-B2` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-B2 passed` |
| 6/SF-C1 | `SF-C1` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-C1 passed` |
| 6/SF-C2 | `SF-C2` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-C2 passed` |
| 6/SF-D1 | `SF-D1` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-D1 passed` |
| 6/SF-D2 | `SF-D2` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-D2 passed` |
| 6/SF-D3 | `SF-D3` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-D3 passed` |
| 6/SF-E1 | `SF-E1` | `e2e/DiscoveryRegistryHa` | 예 | PASS | `e2e/DiscoveryRegistryHa/logs/20260713-112218-360426 및 child log`; `scenario SF-E1 passed` |
| 7/MON-A1 | `MON-A1` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260713-112648-384170`; `scenario MON-A1 passed` |
| 7/MON-A2 | `MON-A2` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260713-112648-384170`; `scenario MON-A2 passed` |
| 7/MON-A3 | `MON-A3` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260713-112648-384170`; `scenario MON-A3 passed` |
| 7/MON-A4 | `MON-A4` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260713-112648-384170`; `scenario MON-A4 passed` |
| 7/MON-A5 | `MON-A5` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260713-112648-384170`; `scenario MON-A5 passed` |
| 7/MON-B1 | `MON-B1` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260713-112648-384170`; `scenario MON-B1 passed` |
| 7/MON-B2 | `MON-B2` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260713-112648-384170`; `scenario MON-B2 passed` |
| 7/MON-C1 | `MON-C1` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260713-112648-384170`; `scenario MON-C1 passed` |
| 7/MON-D1 | `MON-D1` | `e2e/RuntimeMonitoring` | 예 | PASS | `e2e/RuntimeMonitoring/logs/20260713-112648-384170`; `scenario MON-D1 passed` |
| 8/ATD-A1 | `ATD-A1` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-A1 passed` |
| 8/ATD-A2 | `ATD-A2` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-A2 passed` |
| 8/ATD-A3 | `ATD-A3` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-A3 passed` |
| 8/ATD-A4 | `ATD-A4` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-A4 passed` |
| 8/ATD-B1 | `ATD-B1` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-B1 passed` |
| 8/ATD-B2 | `ATD-B2` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-B2 passed` |
| 8/ATD-B3 | `ATD-B3` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-B3 passed` |
| 8/ATD-C1 | `ATD-C1` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-C1 passed` |
| 8/ATD-C2 | `ATD-C2` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-C2 passed` |
| 8/ATD-C3 | `ATD-C3` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-C3 passed` |
| 8/ATD-D1 | `ATD-D1` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-D1 passed` |
| 8/ATD-D2 | `ATD-D2` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-D2 passed` |
| 8/ATD-D3 | `ATD-D3` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-D3 passed` |
| 8/ATD-D4 | `ATD-D4` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-D4 passed` |
| 8/ATD-E1 | `ATD-E1` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-E1 passed` |
| 8/ATD-E2 | `ATD-E2` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-E2 passed` |
| 8/ATD-E3 | `ATD-E3` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-E3 passed` |
| 8/ATD-E4 | `ATD-E4` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-E4 passed` |
| 8/ATD-E5 | `ATD-E5` | `e2e/AutomaticTurnDispatch` | 예 | PASS | `e2e/AutomaticTurnDispatch/logs/20260713-112831-390260`; `scenario ATD-E5 passed` |
| 9/TA-A1 | `TA-A1` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260713-113219-400669`; `scenario TA-A1 passed` |
| 9/TA-A2 | `TA-A2` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260713-113219-400669`; `scenario TA-A2 passed` |
| 9/TA-A3 | `TA-A3` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260713-113219-400669`; `scenario TA-A3 passed` |
| 9/TA-A4 | `TA-A4` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260713-113219-400669`; `scenario TA-A4 passed` |
| 9/TA-B1 | `TA-B1` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260713-113219-400669`; `scenario TA-B1 passed` |
| 9/TA-B2 | `TA-B2` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260713-113219-400669`; `scenario TA-B2 passed` |
| 9/TA-B3 | `TA-B3` | `e2e/ToActorMessaging` | 예 | PASS | `e2e/ToActorMessaging/logs/20260713-113219-400669`; `scenario TA-B3 passed` |
| 10/ST-A1 | `ST-A1` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-A1 passed` |
| 10/ST-A2 | `ST-A2` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-A2 passed` |
| 10/ST-A3 | `ST-A3` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-A3 passed` |
| 10/ST-B1 | `ST-B1` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-B1 passed` |
| 10/ST-B2 | `ST-B2` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-B2 passed` |
| 10/ST-B3 | `ST-B3` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-B3 passed` |
| 10/ST-B4 | `ST-B4` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-B4 passed` |
| 10/ST-C1 | `ST-C1` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-C1 passed` |
| 10/ST-C2 | `ST-C2` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-C2 passed` |
| 10/ST-C3 | `ST-C3` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-C3 passed` |
| 10/ST-D1 | `ST-D1` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-D1 passed` |
| 10/ST-D2 | `ST-D2` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-D2 passed` |
| 10/ST-E1 | `ST-E1` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-E1 passed` |
| 10/ST-E2 | `ST-E2` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-E2 passed` |
| 10/ST-F1 | `ST-F1` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-F1 passed` |
| 10/ST-F2 | `ST-F2` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-F2 passed` |
| 10/ST-F3 | `ST-F3` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-F3 passed` |
| 10/ST-F4 | `ST-F4` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-F4 passed` |
| 10/ST-F5 | `ST-F5` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-F5 passed` |
| 10/ST-F6 | `ST-F6` | `e2e/SpotActorTransfer` | 예 | PASS | `e2e/SpotActorTransfer/log/20260713-113225-401068`; `scenario ST-F6 passed` |
| 11/OBS-A1 | `OBS-A1` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-A1 ... PASS` |
| 11/OBS-A2 | `OBS-A2` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-A2 ... PASS` |
| 11/OBS-A3 | `OBS-A3` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-A3 ... PASS` |
| 11/OBS-A4 | `OBS-A4` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-A4 ... PASS` |
| 11/OBS-B1 | `OBS-B1` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-B1 ... PASS` |
| 11/OBS-B2 | `OBS-B2` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-B2 ... PASS` |
| 11/OBS-B3 | `OBS-B3` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-B3 ... PASS` |
| 11/OBS-B4 | `OBS-B4` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-B4 ... PASS` |
| 11/OBS-C1 | `OBS-C1` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-C1 ... PASS` |
| 11/OBS-C2 | `OBS-C2` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-C2 ... PASS` |
| 11/OBS-C3 | `OBS-C3` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-C3 ... PASS` |
| 11/OBS-C4 | `OBS-C4` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-C4 ... PASS` |
| 11/OBS-C5 | `OBS-C5` | `e2e/ObservabilityOps` | 예 | PASS | `e2e/ObservabilityOps/logs/20260713-112944-394511`; `OBS-C5 ... PASS` |

## Cross-language matrix

| 기능 | producer | consumer | topology | 결과 | marker |
|---|---|---|---|---|---|
| messaging request/send | Node.js | .NET | client-server channel | PASS | Node client -> dotnet channel server request/reply, one-way send |
| messaging request | .NET | Node.js | client-server channel | PASS | dotnet client -> Node channel server |
| messaging publish | Node.js | .NET | fanout | PASS | Node publisher -> dotnet fanout subscriber |
| messaging publish | .NET | Node.js | fanout | PASS | dotnet publisher -> Node fanout subscriber |
| messaging request | Node.js | .NET | direct route-mesh | PASS | Node route client -> dotnet route server request/reply |
| messaging request | .NET | Node.js | direct route-mesh | PASS | dotnet route client -> Node route server request/reply |
| codec/flow-wire | Node.js | .NET | STREAM JSON request | PASS | UUIDv7 flow와 shared origin wire 값, JSON reply |
| codec/flow-wire | .NET | Node.js | STREAM JSON request | PASS | UUIDv7 flow와 shared origin wire 값, JSON reply |
| session-closing | .NET | Node.js | STREAM server drain | PASS | `ServerDrain` 수신 뒤 disconnect |
| session-closing | Node.js | .NET | STREAM server drain | PASS | `ServerDrain` 수신 뒤 disconnect |
| store/draining-row | Node.js | .NET | 전용 Redis location store | PASS | typed `Draining=true` row 소비 |
| store/draining-row | .NET | Node.js | 전용 Redis location store | PASS | typed `draining=true` row 소비 |

실행 명령은 `npm run verify:cross-language`이며 모든 topology는 배포 package의 public
surface만 사용한다. runner는 시작할 때 Node framework/binding과 `.NET` framework version,
direct channel, fanout, route-mesh, STREAM, Redis store topology, payload identity, codec과 실행별
Redis key prefix를 `cross-manifest` 한 줄로 기록한다. Node 단계보다 앞선 언어 중 G7을 통과한
언어는 .NET뿐이다. Java와 Kotlin은
현재 G7 이전이므로 계획 §8.7의 순서 규칙에 따라 Node G6의 양방향 실행 분모에 포함하지 않는다.
