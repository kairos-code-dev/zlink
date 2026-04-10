# Old Core Message API Removal Impact

이 문서는 phase 2 continuation에서 제거한 구형 message-level API가 어떤 바인딩에 영향을 주는지 정리한다.

## 제거된 core API

- `zlink_msg_set_metadata`
- `zlink_msg_get_metadata`
- `zlink_msg_clear_metadata`
- `zlink_msg_set_request`
- `zlink_msg_set_reply`
- `zlink_msg_get_request_info`

위 API는 구형 request-reply/message metadata 경로와 함께 제거되었다. 이제 core는 새 socket-level request-reply API만 유지한다.

## 영향 받는 바인딩

| Binding | 영향 여부 | 현재 확인된 위치 | 필요한 후속 조치 |
| --- | --- | --- | --- |
| C++ | 영향 있음 | `bindings/cpp/include/zlink.h`, `bindings/cpp/include/zlink/message.hpp` | 헤더 복사본과 `Message` wrapper에서 구형 API 선언/메서드를 제거해야 한다. |
| Go | 영향 있음 | `bindings/go/include/zlink.h`, `bindings/go/message.go` | cgo 선언과 `Message` helper를 새 계약에 맞게 정리해야 한다. |
| .NET | 영향 있음 | `bindings/dotnet/src/Zlink/Native/NativeMethods.Core.cs`, `bindings/dotnet/src/Zlink/Message.cs` | P/Invoke 선언과 `Message` method를 제거하거나 새 API로 옮겨야 한다. |
| Rust | 영향 있음 | `bindings/rust/include/zlink.h`, `bindings/rust/src/ffi.rs`, `bindings/rust/src/message.rs`, `bindings/rust/src/request_reply.rs`, `bindings/rust/tests/*`, `bindings/rust/samples/*` | FFI 선언, message helper, request-reply wrapper, 샘플/테스트를 새 socket-level API 기준으로 바꿔야 한다. |
| Java | 영향 있음 | `bindings/java/src/main/java/dev/kairoscode/zlink/Message.java`, `bindings/java/src/main/java/dev/kairoscode/zlink/internal/NativeMsg.java` | FFM downcall과 `Message` surface에서 구형 API를 제거해야 한다. |
| Python | 영향 있음 | `bindings/python/src/zlink/_core.py`, `bindings/python/src/zlink/_request_reply.py` | ctypes 선언과 Python helper 계층을 새 계약으로 교체해야 한다. |
| Node.js | 영향 있음 | `bindings/node/native/src/addon_core.cc` | native addon의 `requestInfo` 직렬화/역직렬화와 구형 setter 호출을 제거해야 한다. |

## 영향이 없는 영역

- 새 request-reply socket API:
  - `zlink_dealer_request`
  - `zlink_router_request`
  - `zlink_router_reply`
  - `zlink_router_handler`
- 일반 `zlink_send` / `zlink_recv`의 payload 전송 의미
- socket-level user metadata가 아닌 기존 ZMP connection metadata 계열 API

## 마이그레이션 메모

- 바인딩은 더 이상 메시지 객체에 request type이나 correlation id를 저장하면 안 된다.
- request-reply 흐름은 새 socket-level API가 돌려주는 request/reply contract로 옮겨야 한다.
- 구형 metadata helper에 의존한 바인딩은 별도 payload field 또는 새 상위 API 계약으로 데이터를 옮겨야 한다.
- 바인딩별 복사 헤더(`bindings/*/include/zlink.h`)는 core public header와 다시 맞춰야 한다.
