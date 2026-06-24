# Java RegistrationCodec E2E feature map

이 문서는 Config 4 Registration/Codec 공통 시나리오 중 Java framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다.

## 구현됨

- `RC-A1`: `addHandlersFromPackageOf`와 handler group으로 interface handler를 스캔해 request/send를
  처리한다.
- `RC-A2`: Java `@ZLinkHandlerGroup`, `@ZLinkRequest`, `@ZLinkSend`로 request/send를 처리한다.
- `RC-A3`: builder의 `addRequestHandler(..., packetName)`와 `addSendHandler(..., packetName)`을
  사용한다.
- `RC-A5`: `useFilter`로 등록한 두 handler filter가 request에서 before 등록 순서와 after 역순을
  evidence에 남기는지 확인한다.
- `RC-A6`: 중복 packet 등록 server role이 시작 단계에서 실패하는지 runner가 확인한다.
- `RC-B1`: JSON DTO request/send가 왕복하고 evidence에 남는다.
- `RC-B2`: `StringValue` DTO가 Protobuf codec으로 왕복한다.
- `RC-B3`: typed MessagePack codec factory로 지정한 DTO가 왕복한다.
- `RC-B4`: JSON fallback, Protobuf predicate codec, typed MessagePack codec을 한 host에 같이
  등록해 섞어 보낸다.

## public API/harness 대기

- `RC-A4`: request별 scoped dependency instance와 dispose count를 evidence로 고정하는 DI lifecycle
  scenario가 아직 없다.
- `RC-B5`: client/server가 서로 다른 reply codec을 등록했을 때 typed request가 decode failure로
  끝나는 peer mismatch scenario가 아직 없다.
