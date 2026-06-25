# Kotlin RegistrationCodec E2E feature map

이 문서는 Config 4 Registration/Codec 공통 시나리오 중 Kotlin 전용 E2E 구현 상태를 정리한다.
Kotlin runner는 `run_e2e.sh`이며, Kotlin source가 Java framework public API를 직접 호출한다.

## 구현됨

- `RC-A1`: `EchoAuto` request/send를 자동 스캔 등록 handler로 처리하고 evidence를 확인한다.
- `RC-A2`: `@ZLinkHandlerGroup` + `@ZLinkRequest`/`@ZLinkSend` annotation 등록 handler로 `EchoAttr` request/send를 처리한다.
- `RC-A3`: channel builder의 명시적 request/send handler 등록으로 `EchoManual` request/send를 처리한다.
- `RC-A4`: Spring DI prototype dependency와 singleton dependency를 handler에서 사용해 요청별 scoped id, singleton id, dispose count를 확인한다.
- `RC-A5`: `FirstOrderFilter`와 `SecondOrderFilter`를 등록한 순서대로 before/after evidence가 남는지 확인한다.
- `RC-A6`: duplicate packet registration을 가진 invalid server가 시작 단계에서 실패하는지 runner가 먼저 확인한다.
- `RC-B1`: JSON request/send round-trip과 evidence를 확인한다.
- `RC-B2`: Protobuf `StringValue` request/send round-trip과 evidence를 확인한다.
- `RC-B3`: MessagePack 대상 DTO request/send round-trip과 evidence를 확인한다.
- `RC-B4`: JSON/Protobuf/MessagePack codec을 같은 host registry에 함께 등록한 상태에서 각 packet이 간섭 없이 처리되는지 확인한다.
- `RC-B5`: server를 JSON-only로 띄운 뒤 MessagePack packet mismatch가 public error 또는 fallback으로 관측되고, 정상 JSON traffic이 계속 동작하는지 확인한다.

## public API/harness 대기

- 없음.
