# .NET RegistrationCodec E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RC-A1 | 구현 | assembly/module 자동 등록 marker가 있다. |
| RC-A2 | 구현 | attribute 등록 marker가 있다. |
| RC-A3 | 구현 | 수동 handler 등록 marker가 있다. |
| RC-A4 | 구현 | DI lifecycle marker가 있다. |
| RC-A5 | 구현 | filter ordering marker가 있다. |
| RC-A6 | 구현 | invalid registration startup failure marker가 있다. |
| RC-B1 | 구현 | JSON codec marker가 있다. |
| RC-B2 | 구현 | Protobuf codec marker가 있다. |
| RC-B3 | 구현 | MessagePack codec marker가 있다. |
| RC-B4 | 구현 | codec coexistence marker가 있다. |
| RC-B5 | 미구현 | peer codec registry mismatch marker가 없다. |
