# Cross-Language API Alignment

## 목표
모든 바인딩(C++, .NET, Java, Node.js)이 **동일한 개념/메서드 이름/에러 규칙**을 공유하도록
코어 API를 기준으로 공통 계약을 정의한다.

---

## 공통 클래스/타입
- Context
- Socket
- Message
- Poller (가능하면 동일)

---

## 공통 메서드 (이름 매핑)
| 개념 | C++ | .NET | Java | Node.js |
|---|---|---|---|---|
| Context 생성 | `context_t()` | `Context()` | `new Context()` | `new Context()` |
| Context 종료 | `~context_t()` | `Dispose()` | `close()` | `close()` |
| Socket 생성 | `socket_t(ctx, type)` | `new Socket(ctx, type)` | `ctx.createSocket(type)` | `ctx.socket(type)` |
| bind | `bind()` | `Bind()` | `bind()` | `bind()` |
| connect | `connect()` | `Connect()` | `connect()` | `connect()` |
| close | `close()` | `Close()`/`Dispose()` | `close()` | `close()` |
| send (buffer) | `send(buf)` | `Send(byte[])` | `send(byte[])` | `send(Buffer)` |
| recv (buffer) | `recv(buf)` | `Recv(byte[])` | `recv(byte[])` | `recv()` |
| send (message) | `send(message_t&)` | `Send(Message)` | `send(Message)` | `send(Message)` |
| recv (message) | `recv(message_t&)` | `Recv(Message)` | `recv(Message)` | `recv(Message)` |
| set option | `set()` | `SetOption()` | `setOption()` | `setOption()` |
| get option | `get()` | `GetOption()` | `getOption()` | `getOption()` |

---

## 에러 처리 규칙
- 기본은 **에러 코드 반환** (C++ 기준)
- .NET/Java/Node는 **예외 기반**으로 노출하되, 내부적으로 errno 매핑 유지
- 타임아웃/재시도/종료는 공통 에러 코드로 통일

---

## 메시지 소유권 규칙
- `Message` send 성공 시 소유권 이동
- send 실패 시 호출자 책임
- recv는 내부 버퍼 채우기/객체 반환 중 언어 관례에 맞는 방식으로 제공

---

## 스레드 안전성 규칙
- 소켓은 non-thread-safe
- Context는 thread-safe
- 바인딩은 코어의 스레드 모델을 그대로 유지

---

## 버전/호환성 정책
- 바인딩 버전은 코어 `VERSION`과 동일하게 유지
- 바인딩 릴리즈는 코어 태그와 함께 갱신
- C API 변경 시 바인딩도 동시 업데이트

---

## 옵션/상수 노출 기준
- C API의 `ZLINK_*` 상수를 그대로 노출
- 문서화되지 않은/실험적 옵션은 숨김 처리

---

## Message 규칙
- 메시지는 **소유권이 명확**해야 한다.
- send 성공 시 메시지 소유권 이동, 실패 시 호출자 책임
- recv는 내부 버퍼를 채우는 방식 또는 Message 객체를 반환하는 방식 중 한 가지로 통일

---

## 네이밍 규칙
- Socket 타입은 C API 상수와 동일 명칭 사용
- 옵션은 `ZLINK_*` 상수와 동일하게 노출
- 메서드명은 언어 관례(파스칼/카멜)만 반영

---

## 향후 바인딩별 문서
- `bindings/cpp/` : C++ API 초안 및 구현 계획
- `bindings/dotnet/` : .NET 설계 문서 (추가 예정)
- `bindings/java/` : Java 설계 문서 (추가 예정)
- `bindings/node/` : Node.js 설계 문서 (추가 예정)

---

## 공통 테스트 시나리오
- Context 생성/종료
- Socket 생성/종료
- 기본 send/recv (PAIR, DEALER/ROUTER)
- 옵션 set/get
- 메시지 move/ownership 검증
- poller 기본 동작
- TLS 옵션 적용 (가능한 플랫폼)

---

## 샘플/테스트 작성 계획
- `bindings/*/examples/`에 각 언어별 최소 샘플 제공
  - PAIR 기본 send/recv
  - DEALER/ROUTER 라우팅 예시
  - TLS connect/bind 예시 (가능한 플랫폼)
- `bindings/*/tests/`에 공통 시나리오 기반 테스트 작성
  - smoke test (context/socket/message)
  - send/recv 기본
  - 옵션 set/get
  - poller
