# 바인딩 공통 개요

## 1. 개요

zlink는 C API를 기반으로 5개 언어 바인딩을 제공한다. 모든 바인딩은 동일한 개념/메서드 이름/에러 규칙을 공유한다.

## 2. 바인딩 선택 가이드

| 바인딩 | 최소 요구 | 특징 | 적합한 경우 |
|--------|-----------|------|-------------|
| C++ | C++11 | Header-only RAII | 코어와 동일 프로세스 |
| Java | Java 22+ | FFM API (no JNI) | JVM 기반 서비스 |
| .NET | .NET 8+ | LibraryImport | C#/F# 서비스 |
| Node.js | Node 18+ | N-API, prebuilds | 웹 서버/도구 |
| Python | Python 3.8+ | ctypes/CFFI | 스크립팅/프로토타이핑 |

## 3. 공통 API 매핑

| 개념 | C++ | .NET | Java | Node.js | Python |
|------|-----|------|------|---------|--------|
| Context 생성 | `context_t()` | `Context()` | `new Context()` | `new Context()` | `Context()` |
| Context 종료 | `~context_t()` | `Dispose()` | `close()` | `close()` | `close()` |
| Socket 생성 | `socket_t(ctx, type)` | `new Socket(ctx, type)` | `ctx.createSocket(type)` | `ctx.socket(type)` | `ctx.socket(type)` |
| bind | `bind()` | `Bind()` | `bind()` | `bind()` | `bind()` |
| connect | `connect()` | `Connect()` | `connect()` | `connect()` | `connect()` |
| close | `close()` | `Dispose()` | `close()` | `close()` | `close()` |
| send | `send(buf)` | `Send(byte[])` | `send(byte[])` | `send(Buffer)` | `send(bytes)` |
| recv | `recv(buf)` | `Recv(byte[])` | `recv(byte[])` | `recv()` | `recv()` |
| set option | `set()` | `SetOption()` | `setOption()` | `setOption()` | `setOption()` |
| get option | `get()` | `GetOption()` | `getOption()` | `getOption()` | `getOption()` |

## 4. 에러 처리 규칙

- C++: 에러 코드 반환 (C API와 동일)
- .NET/Java/Node.js: 예외 기반, 내부적으로 errno 매핑
- Python: 예외 기반

## 5. 메시지 소유권

- send 성공 시 소유권 이전 (메시지는 비워짐)
- send 실패 시 호출자 책임
- recv는 내부 버퍼 채우기 또는 객체 반환

## 6. 스레드 안전성

- Socket: **non-thread-safe** (단일 스레드 접근)
- Context: **thread-safe** (여러 스레드에서 소켓 생성 가능)

## 7. 버전 정책

- 바인딩 버전은 코어 `VERSION`과 동일
- C API 변경 시 바인딩도 동시 업데이트

## 8. 네이밍 규칙

- Socket 타입은 C API 상수와 동일 명칭 사용
- 옵션은 `ZLINK_*` 상수 그대로 노출
- 메서드명만 언어 관례(파스칼/카멜) 반영
