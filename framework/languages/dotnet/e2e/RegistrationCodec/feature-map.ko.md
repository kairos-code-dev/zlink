# .NET RegistrationCodec E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RC-A1 | 구현 | assembly/module 자동 등록 marker가 있다. |
| RC-A2 | 구현 | attribute 등록 marker가 있다. |
| RC-A3 | 구현 | 수동 handler 등록 marker가 있다. |
| RC-A4 | 구현 | dispatch별 async scope에서 scoped id가 갈라지고 singleton id가 유지되며 dispose marker가 요청 수와 일치하는지 검증한다. |
| RC-A5 | 구현 | filter ordering marker가 있다. |
| RC-A6 | 구현 | invalid registration startup failure marker가 있다. |
| RC-B1 | 구현 | JSON codec marker가 있다. |
| RC-B2 | 구현 | Protobuf codec marker가 있다. |
| RC-B3 | 구현 | MessagePack codec marker가 있다. |
| RC-B4 | 구현 | codec coexistence marker가 있다. |
| RC-B5 | 구현 | JSON-only peer에 Protobuf request를 보내 실패를 확인하고, 이후 JSON request가 정상 처리되는지 검증한다. |
| RC-B6 | 미구현 | 별도 process가 같은 typed DTO를 기본 JSON serializer로 왕복하고, 메시지별 codec 등록 없이 동작하는지 검증하는 selector가 없다. |
