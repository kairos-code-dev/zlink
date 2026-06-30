# Kotlin RegistrationCodec E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`

Kotlin runner는 Java framework public API를 Kotlin code path에서 호출한다. 현재 구현은 `Shared`,
`Client`, `Server/Main`, `Server/CodecRequester`, `Server/JsonOnlyPeer`, `Server/InvalidDuplicate`
Gradle project로 실행 process를 나눴다. client scenario/support 파일과 server role package도
`.NET` 기준 책임별로 분리했고, role별 실행 option은 CLI argument로 받는다. 파일별 재분류 상태는
`porting-inventory.ko.md`에 기록한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RC-A1 | 구현 완료 | `AutoRegistrationScenario`가 `EchoAuto` request/send를 자동 스캔 등록 handler로 처리하고 evidence를 확인한다. |
| RC-A2 | 구현 완료 | `AttributeRegistrationScenario`가 `@ZLinkHandlerGroup` + `@ZLinkRequest`/`@ZLinkSend` annotation 등록 handler로 `EchoAttr` request/send를 처리한다. |
| RC-A3 | 구현 완료 | `ManualRegistrationScenario`가 channel builder의 명시적 request/send handler 등록으로 `EchoManual` request/send를 처리한다. |
| RC-A4 | 구현 완료 | `RcA4DiLifecycleScenario`가 Spring DI prototype dependency와 singleton dependency evidence를 확인한다. |
| RC-A5 | 구현 완료 | `RcA5FilterOrderingScenario`가 `FirstOrderFilter`와 `SecondOrderFilter` 순서 evidence를 확인한다. |
| RC-A6 | 구현 완료 | `Server/InvalidDuplicate` process가 duplicate packet registration startup failure를 내는지 runner가 확인한다. |
| RC-B1 | 구현 완료 | `RcB1JsonCodecScenario`가 JSON request/send round-trip과 evidence를 확인한다. |
| RC-B2 | 구현 완료 | `RcB2ProtobufCodecScenario`가 Protobuf `StringValue` request/send round-trip과 evidence를 확인한다. |
| RC-B3 | 구현 완료 | `RcB3MessagePackCodecScenario`가 MessagePack 대상 DTO request/send round-trip과 evidence를 확인한다. |
| RC-B4 | 구현 완료 | `RcB4CodecCoexistenceScenario`가 JSON/Protobuf/MessagePack codec 공존 처리를 확인한다. |
| RC-B5 | 구현 완료 | `Server/JsonOnlyPeer`와 `Server/CodecRequester` process가 codec mismatch error와 이후 정상 JSON traffic 회복을 확인한다. |

## 포팅 구조 상태

현재 Kotlin RegistrationCodec E2E는 `.NET` 기준 role/project 분리와 scenario/support 파일 분리를
끝낸 구현이다. `porting-inventory.ko.md`에 남은 `pending`/`gap` 항목이 없으면 완료 config로 본다.

## 검증 결과

- `logs/20260629-165219-563294`: `timeout 420s framework/languages/java/e2e-kotlin/RegistrationCodec/run_e2e.sh`
  실행 결과 role별 Gradle project runner, client scenario/support 분리 구조, `Server/Main` package
  분리와 CLI option parser, `Server/JsonOnlyPeer` package 분리와 CLI option parser,
  `Server/CodecRequester` role이 통과했다.
- 통과 scenario: `RC-A1`, `RC-A2`, `RC-A3`, `RC-A4`, `RC-A5`, `RC-A6`, `RC-B1`, `RC-B2`,
  `RC-B3`, `RC-B4`, `RC-B5`.
- `RC-A6`은 `invalid-server.stdout.log`의 duplicate packet registration startup failure로 확인하고,
  `RC-B5`는 `mismatch-client.stdout.log`의 `scenario RC-B5 passed` marker로 확인한다.
- 이 결과는 현재 구현의 동작 기준선, process 분리, client scenario/support 분리, server role 파일
  책임 분리 증거다.
