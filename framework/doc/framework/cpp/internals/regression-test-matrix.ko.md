# C++ Framework Regression Test Matrix

> 이 문서는 C++ framework 유지보수자를 위한 internals 문서다. 공개 API 계약은
> `framework/doc/framework/common/spec/languages/cpp/` 문서와 public header를 기준으로 확인한다.

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

## 4. Spot yield dispatch regression

| ID | 계층 | 테스트 위치 | 통과 기준 |
|----|------|-------------|-----------|
| SYLD-001 | `contract` | `Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp` | request, actor join, bound session send, worker call에 `yield()`가 있고, route request와 일반 send/publish에는 노출되지 않는다. |
| SYLD-002 | `unit` | `Zlink.Framework.UnitTests/test_cpp_framework_spot_runtime.cpp` | 기본 `async()`는 serial gate를 유지하고, request/worker/actor join `yield()`는 다른 mailbox 작업을 실행하게 한 뒤 원래 continuation으로 돌아온다. |
| SYLD-003 | `unit` | `Zlink.Framework.UnitTests/test_cpp_framework_execution.cpp` | serial execution queue가 released turn과 normal completion을 구분한다. |
| SYLD-004 | `contract`, `sample` | `test_cpp_framework_sample_parity`, `test_cpp_framework_layout_contract` | C++ sample layout과 public contract 문서가 일치하고, Entry Spot actor handler 예제는 `yield()`에 의존하지 않는다. |
| SYLD-005 | `unit` | `Zlink.Framework.UnitTests/test_cpp_framework_spot_runtime.cpp` | Entry Spot actor packet은 대상 actor mailbox에서 실행된다. 서로 다른 actor는 Entry Spot 단일 큐에 막히지 않고, 같은 actor의 연속 packet은 순서대로 처리된다. Entry Spot actor handler 안의 `yield()`는 timeout 없이 즉시 계약 오류가 된다. |
