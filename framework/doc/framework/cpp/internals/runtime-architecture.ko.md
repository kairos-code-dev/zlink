<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [다음: Backend Dependency Policy](backend-dependency-policy.ko.md)
<!-- framework-adapter-nav:end -->

[C++ 묶음](../README.ko.md) | [공개 인터페이스](../spec/cpp-framework-interfaces.ko.md)

# ZLink Framework C++ Runtime Architecture

## 1. 목적

이 문서는 C++ framework 유지보수자가 공개 header와 private runtime의 경계, runtime
소유권과 실행 흐름을 코드 전에 파악할 수 있도록 설명한다. 공개 타입과 사용법은
spec과 guide가 소유한다.

## 2. 계층

```text
+--------------------------------------------------------------+
| Application and Samples                                      |
+--------------------------------------------------------------+
| Public Contracts: framework/include/zlink/framework           |
+--------------------------------------------------------------+
| Private Runtime: framework/src/runtime                        |
+--------------------------------------------------------------+
| Backend Adapters and zlink C++ Binding                        |
+--------------------------------------------------------------+
| Core Runtime                                                  |
+--------------------------------------------------------------+
```

설치 대상에는 `framework/include/zlink/framework`의 public header만 포함한다.
`framework/src/runtime`의 클래스, backend socket wrapper와 실행 queue는 설치 header에서
참조하지 않는다.

## 3. runtime 소유권

- `app_t`는 configuration과 host 조립의 진입점이다.
- framework runtime은 backend context, channel bundle, route channel, SpotNode,
  stream node와 location runtime의 수명을 소유한다.
- 각 subsystem은 자신이 만든 listener, pending operation과 callback registration을
  스스로 정리한다.
- sample과 application은 runtime 객체를 직접 만들지 않고 public host와 builder를
  사용한다.

## 4. 실행과 종료

registration validation을 먼저 끝낸 뒤 backend context, location, channel/route,
Spot, stream과 monitoring 순서로 시작한다. 종료할 때는 stop signal을 전달하고
monitoring, Spot, route, stream, channel, location, context 순서로 정리한다.

transport callback은 application handler를 직접 오래 실행하지 않는다. session과
Spot의 serial queue, coroutine executor와 worker pool이 handler 실행을 맡는다.
pending request와 submit은 timeout, cancellation 또는 runtime dispose로 반드시
완료되며 caller thread를 blocking wait로 점유하지 않는다.

## 5. CMake 경계

- public contract compile test는 설치 header만 include해 빌드한다.
- private runtime source는 framework library target에만 연결한다.
- HTTP client, stream connector와 framework extension은 독립 target과 install
  component를 유지한다.
- sample target은 public framework target만 링크하며 private source 경로를 include하지
  않는다.

## 6. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `test_cpp_framework_layout_contract` | public header와 private runtime 경계, 실제 source tree와 sample public API 사용을 검사한다. |
| `test_cpp_framework_contract_headers` | 설치 대상 public header가 독립적으로 compile된다. |
| `test_cpp_framework_runtime` | runtime 시작·종료와 subsystem 소유권을 검증한다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [다음: Backend Dependency Policy](backend-dependency-policy.ko.md)
<!-- framework-adapter-nav:bottom:end -->
