# Java RegistrationCodec E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`

마지막 검증:

- 명령: `timeout 420s ./run_e2e.sh`
- 결과: passed
- 로그: `framework/languages/java/e2e/RegistrationCodec/logs/20260629-130314-673318/`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RC-A1 | 구현 | `addHandlersFromPackageOf`와 handler group으로 자동 등록된 request/send handler를 검증한다. |
| RC-A2 | 구현 | Java `@ZLinkHandlerGroup`, `@ZLinkRequest`, `@ZLinkSend` annotation 등록 handler를 검증한다. |
| RC-A3 | 구현 | builder의 `addRequestHandler(..., packetName)`와 `addSendHandler(..., packetName)` 명시 등록을 검증한다. |
| RC-A4 | 구현 | request마다 새 DI 객체를 만들고 singleton id 유지와 dispose count를 evidence로 확인한다. |
| RC-A5 | 구현 | `useFilter`로 등록한 두 handler filter의 before/after 순서를 evidence로 확인한다. |
| RC-A6 | 구현 | duplicate packet registration server role이 시작 단계에서 실패하는지 runner가 확인한다. |
| RC-B1 | 부분 구현 | JSON DTO request/send 왕복과 evidence를 확인한다. Java public handler context에서 명시 content-type이 노출되지 않아 exact content-type 검증은 gap이다. |
| RC-B2 | 부분 구현 | `StringValue` DTO가 Protobuf codec으로 왕복한다. Java public handler context에서 명시 content-type이 노출되지 않아 exact content-type 검증은 gap이다. |
| RC-B3 | 부분 구현 | typed MessagePack codec factory로 지정한 DTO가 왕복한다. Java public handler context에서 명시 content-type이 노출되지 않아 exact content-type 검증은 gap이다. |
| RC-B4 | 부분 구현 | JSON fallback, Protobuf predicate codec, typed MessagePack codec을 한 host에 같이 등록해 섞어 보낸다. exact content-type 검증은 gap이다. |
| RC-B5 | 구현 | json-only peer에 MessagePack request를 보내 fallback 또는 public error로 끝나는 관측을 고정하고, 이후 정상 JSON traffic이 계속 성공하는지 검증한다. |

## Content-type 검증 gap

공통 Config 4는 codec별 content-type 확인을 요구한다. Java framework의 현재 public client request API는
reply content-type을 반환하지 않고, server handler의 public context도 이 E2E 경로에서 codec별
content-type을 안정적으로 노출하지 않는다. 따라서 Java E2E는 codec별 DTO 왕복, handler evidence,
message flow marker까지 검증하지만 exact content-type oracle은 public contract gap으로 남긴다.

이 gap은 raw frame 해석이나 private runtime 접근으로 메우지 않는다. public API로 content-type을
확인할 수 있는 계약이 정리되면 RC-B1~RC-B4의 부분 구현 상태를 갱신한다.
