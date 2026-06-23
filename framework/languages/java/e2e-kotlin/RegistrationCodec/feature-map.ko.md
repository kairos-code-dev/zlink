# Kotlin RegistrationCodec E2E feature map

이 앱은 `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`의 Kotlin 지원 범위를 검증한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RC-A1 자동 등록 | 구현 | `addHandlersFromPackageOf`와 handler group으로 interface handler를 스캔해 request/send를 처리한다. |
| RC-A2 annotation 등록 | 구현 | Kotlin class의 `@ZLinkHandlerGroup`, `@ZLinkRequest`, `@ZLinkSend`로 request/send를 처리한다. |
| RC-A3 수동 등록 | 구현 | builder의 `addRequestHandler(..., packetName)`와 `addSendHandler(..., packetName)`를 사용한다. |
| RC-A6 잘못된 등록 차단 | 구현 | 중복 packet 등록 server role이 시작 단계에서 실패하는지 runner가 확인한다. |
| RC-B1 JSON codec | 구현 | JSON DTO request/send가 왕복하고 evidence에 남는다. 현재 Java/Kotlin channel context는 content-type을 공개하지 않는다. |
| RC-B2 Protobuf codec | 구현 | `StringValue` DTO가 Protobuf codec으로 왕복한다. |
| RC-B3 MessagePack codec | 구현 | typed MessagePack codec factory로 지정한 DTO가 왕복한다. |
| RC-B4 codec 공존 | 구현 | JSON fallback, Protobuf predicate codec, typed MessagePack codec을 한 host에 같이 등록해 섞어 보낸다. |
| RC-A4, RC-A5, RC-B5 | 미구현 | DI lifecycle, filter ordering, peer codec 불일치는 다음 확장 단계에서 별도 harness와 함께 추가한다. |
