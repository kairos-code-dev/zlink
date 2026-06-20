# C++ Framework Regression Test Matrix

> 이 문서는 C++ framework 유지보수자를 위한 internals 문서다. 공개 API 계약은
> `framework/doc/framework/cpp/spec/` 문서와 header 를 기준으로 확인한다.

## 1. 테스트 계층

| 계층 | 범위 |
|------|------|
| `contract` | public header, namespace, builder 표면 |
| `unit` | channel, route, dispatch helper, actor/spot runtime 단위 |
| `integration-single-process` | 한 process 안의 host/runtime 조합 |

## 2. Dispatch Error Observer Regression

| ID | 계층 | 테스트 위치 | 통과 기준 |
|----|------|-------------|-----------|
| DERR-001, DERR-007, DERR-011, DERR-014 | `contract`, `unit` | `Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp`, `Zlink.Framework.UnitTests/test_cpp_framework_channel_messaging.cpp` | 전역 observer 등록 표면이 존재하고, channel request handler 없음은 error reply와 observer event, channel send handler 없음은 drop과 observer event로 끝나며 observer 예외는 원래 dispatch 결과를 깨지 않음 |
| DERR-002, DERR-008 | `unit` | `Zlink.Framework.UnitTests/test_cpp_framework_channel_messaging.cpp` | route request handler 없음은 error reply, route send handler 없음은 drop과 observer event로 끝남 |
| DERR-003, DERR-004, DERR-009, DERR-010, DERR-016 | `unit`, `integration-single-process` | `Zlink.Framework.UnitTests` SPOT/actor dispatch 항목 | SPOT route, subscription, actor dispatch 실패가 request면 error reply 또는 caller-visible error, one-way면 drop과 observer event로 끝남 |
| DERR-005, DERR-006, DERR-013, DERR-015 | `unit`, `integration-single-process` | `Zlink.Framework.UnitTests` channel/SPOT dispatch 항목 | decode 실패와 handler 예외는 error reply 또는 관측 가능한 drop으로 끝나며, observer 미등록 시에도 기본 로그와 counter가 남음 |

## 3. Release Gate

C++ framework 변경은 관련 target 을 빌드한 뒤 `ctest` label 체계에 맞춰 실행한다. dispatch error
observer 변경은 최소한 contract header test 와 channel messaging unit test 를 통과해야 한다.
