# 5. 실행 모델

> [공통 계약 목차](README.ko.md)

## 5.1 비동기 계약

제출은 언어의 표준 비동기 값을 돌려주며, 네트워크 대기 중 호출자의
스레드/event loop를 점유하지 않는다.

| 언어 | 제출 반환형 | non-blocking 근거 |
| --- | --- | --- |
| cpp | `task_t<T>` (`co_await`) | `.coroutines()` 활성 시 execute scheduler로 오프로드 |
| dotnet | `ValueTask<T>` | `SocketsHttpHandler` epoll/IOCP |
| java | `CompletionStage<T>` | `java.net.http` NIO selector |
| kotlin | `suspend` 함수 | java 런타임 + `CompletionStage.await()` 브리지 |
| node | `Promise<T>` | undici libuv |

## 5.2 blocking 금지 규칙

framework handler/runtime 스레드에서는 blocking 언래핑을 금지한다:
cpp `.result()`/`fetch()`, dotnet `Fetch<T>()`/`.GetAwaiter().GetResult()`,
java `fetch()`/`.join()`/`.get()`. 합성은 `co_await` / `await` /
`thenCompose` / suspend로 한다.

## 5.3 cpp 이중 모델 (언어 편차)

cpp만 기본이 동기 blocking이고 `.coroutines()`가 opt-in이다.

- `coroutines()` — 기본 스케줄러 사용.
- `coroutines(resume_scheduler)` — 재개 위치 주입(예: framework server queue에서
  continuation 재개, `framework_resume_scheduler_t`).
- `coroutines(execute_scheduler, resume_scheduler)` — 실행/재개 모두 주입.

주의(현행 구현 특성, 계약 아님): 기본 스케줄러는 단일 스레드를 execute/resume
공용으로 쓰므로 요청이 직렬화되며, 재개된 continuation에서 같은 스케줄러의
다른 task를 blocking 대기하면 데드락이 가능하다. 개선은 plan 문서의 결함
트랙에서 다룬다.

## 5.4 client 수명과 one-shot 경로

- client는 서비스당 하나를 만들어 재사용한다(pool/keep-alive 이득).
- builder verb 단축(one-shot)은 제출 시 client를 lazy build하고 완료 후 닫는
  **편의 경로**다. 요청마다 전송 스택 초기화 비용을 내므로 반복/고부하
  호출에 쓰지 않는다. one-shot 요청 객체는 재제출할 수 없다(재제출 시
  `requestProtocolError`).

## 5.5 취소

- dotnet은 제출에 `CancellationToken`을 받는다.
- kotlin coroutine 취소의 하부 요청 전파는 현재 미구현이다(개정 후보
  [R5](10-revision-candidates.ko.md)).
- cpp/java/node는 요청 단위 취소 API를 노출하지 않는다(timeout으로 경계).
