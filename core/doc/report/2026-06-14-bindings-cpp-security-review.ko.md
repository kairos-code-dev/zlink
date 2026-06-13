# C++ 바인딩 보안 검토 보고서

- 작성일: 2026-06-14
- 대상 범위: `bindings/cpp/src/Runtime/Native/native_message_parts.hpp`
- 검토 방식: 메시지 part 이동, 실패 시 복구, close 경로를 코드 기준으로 확인했다.
- 상태: 2026-06-14 추가 수정 없음. Codex 에이전트 리뷰 통과.

## 요약

C++ 바인딩은 core C API의 `zlink_msg_t`를 RAII 객체로 감싼다. 이번 검토에서는 multipart 송수신 과정에서 메시지 소유권을 native 배열로 넘겼다가 실패 시 다시 복구하는 경로를 중점적으로 확인했다.

현재 확인한 범위에서는 C++ 바인딩 자체의 use-after-free, double close, 명백한 누수는 확인되지 않았다.

## 확인 결과

- `bindings/cpp/src/Runtime/Native/native_message_parts.hpp:24-48`은 native part 배열을 닫는 helper를 한 곳에 모아 둔다.
- `bindings/cpp/src/Runtime/Native/native_message_parts.hpp:50-78`은 `std::vector<message_t>`를 native vector로 옮기는 중 실패하면 이미 이동한 part를 다시 복구한다.
- `bindings/cpp/src/Runtime/Native/native_message_parts.hpp:102-127`은 stack 배열 경로에서도 native 배열 크기와 message part 수가 다르면 바로 실패한다.
- `bindings/cpp/src/Runtime/Native/native_message_parts.hpp:121-126`은 일부 이동 후 실패한 경우에도 이동된 메시지를 복구한다.

## 기능 영향 검토

현재 구현은 실패 시 호출자가 넘긴 메시지를 최대한 원래 소유권으로 되돌리는 방향이다. 따라서 송신 실패가 곧바로 호출자 메시지 손실로 이어지는 문제는 확인되지 않았다.

`native_part_stack_capacity`가 `8`로 정의되어 있지만, 이는 작은 part 수를 빠르게 처리하기 위한 stack 기반 경로로 보인다. 더 큰 multipart는 vector 경로로 처리되는 구조라면 기능 제한으로 보지 않는다.

## 성능 영향 검토

작은 multipart는 stack 기반 경로를 사용할 수 있게 되어 할당 비용을 줄이는 구조다. 실패 복구 로직은 정상 경로 비용을 크게 늘리지 않는다.

성능상 새로 확인된 병목은 없다. 다만 core C API의 메시지 복사·이동 비용은 C++ 바인딩이 그대로 물려받는다.

검증:

- `bindings/cpp/tests/run_tests.sh` 통과. C++ contract 15개와 sample smoke 19개가 통과했다.
- Codex 에이전트 리뷰에서 "추가 이슈 없음" 판정을 받았다.

## 결론

검토한 C++ 바인딩 메시지 소유권 경로에서는 추가 수정할 보안·기능·성능 이슈를 확인하지 못했다. 남은 위험은 core C API의 메시지 계약과 실제 구현에 종속된다.
