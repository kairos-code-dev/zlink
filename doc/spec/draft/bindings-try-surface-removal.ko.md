[스펙 목차](../README.ko.md)

# Draft -- Exception Binding Try Surface Removal

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 공개 binding
> 표면 변경을 보장하지 않는다.
> 구현과 공개 헤더, errno 계약, 관련 binding spec 문서가 확정되면 정식 spec
> 문서에 나누어 반영한다.

## 1. 목적

이 초안은 예외 기반 binding의 `Try*` 공개 표면을 줄이고,
blocking 여부를 기존 `flags`로 표현하는 방향을 정리한다.

핵심 목표는 아래와 같다.

- `send`와 `trySend`처럼 이름만 다른 공개 API 쌍을 줄인다.
- 호출자가 blocking과 non-blocking을 함수 이름이 아니라 `flags`로 읽게 만든다.
- temporary backpressure만 `false`로 드러내고, 나머지 실패는 기존 예외로
  유지한다.
- `publish`도 `send`와 같은 submit 계약으로 다루어, 성공 여부를 같은 방식으로
  읽게 만든다.

## 2. 범위

이 초안은 아래 binding에만 적용한다.

- `.NET`
- `Java`
- `Node`
- `Python`
- `C++`

이 초안은 아래 함수군을 다룬다.

- `send`
- callback completion `request`
- `publish`
- `recv`
- `subscribe`

이 초안은 아래 binding에는 적용하지 않는다.

- `C`
- `Go`
- `Rust`

위 세 binding은 기존 C 스타일 surface를 유지한다. 즉 별도 `Try*` 제거 정책을
이 초안에 맞춰 억지로 맞추지 않는다.

## 3. 배경

현재 예외 기반 binding 문서는 아래처럼 `Try*` 이름을 공개 API에 두고 있다.

- `trySend`
- `tryRecv`
- `tryRequest`

이 구조는 "blocking 방식 차이"보다 "함수 이름 차이"가 먼저 보이는 문제가 있다.
실제로 non-blocking 여부는 이미 `flags`로 표현할 수 있는데, 공개 API가
함수 이름까지 나뉘어 있어 사용자가 같은 동작을 두 번 외워야 한다.

또한 `publish`는 같은 submit 계열인데도 반환 규칙이 `send`와 분리되어 보인다.
이 초안은 submit 계열은 같은 방식으로, receive 계열은 receive에 맞는 방식으로
정리하려고 한다.

## 4. 설계 원칙

### 4.1 `Try*` 이름은 공개 API에서 제거한다

대상 binding에서는 `trySend`, `tryRecv`, `tryRequest` 같은 이름을 공개
surface에서 제거한다.

blocking과 non-blocking의 선택은 함수 이름이 아니라 기존 `flags` 인자로
표현한다.

### 4.2 submit 계열은 `bool` 반환으로 통일한다

아래 함수군은 성공 여부를 `bool`로 반환한다.

- `send`
- callback completion `request`
- `publish`

규칙은 아래와 같다.

- blocking 호출에서 submit이 성공하면 항상 `true`를 반환한다.
- non-blocking 호출에서 temporary backpressure가 걸리면 `false`를 반환한다.
- temporary backpressure가 아닌 다른 submit 실패는 기존과 같은 typed exception으로
  전달한다.

즉 `false`는 "지금 잠시 못 보낸다"는 경우에만 쓴다.
잘못된 대상, 연결 부재, 상태 오류, 내부 오류 같은 것은 `false`로 숨기지 않는다.

### 4.3 receive 계열은 payload를 유지한다

`recv`와 `subscribe`는 데이터를 돌려주는 함수이므로 `bool`만으로는 payload를
함께 표현할 수 없다.

이 초안은 receive 계열에서 `Try*` 이름만 제거하고, 결과 shape는 아래처럼
정리한다.

- blocking 호출에서 성공하면 payload를 반환한다.
- non-blocking 호출에서 현재 읽을 데이터가 없으면 empty 표현을 반환한다.
- 실제 receive 실패는 기존과 같은 typed exception으로 전달한다.

empty 표현은 언어 관용구를 따른다.

- `.NET`: nullable reference
- `Java`: nullable reference
- `Node`: `null`
- `Python`: `None`
- `C++`: `std::optional`

즉 receive 계열은 "temporary backpressure"가 아니라 "현재 읽을 데이터가 없음"을
표현한다.

## 5. 함수군별 제안 계약

### 5.1 `send`

`send`는 단일 part, multipart, routed send를 포함해 같은 반환 규칙을 쓴다.

- `flags`에 non-blocking이 없으면 성공 시 `true`
- non-blocking에서 temporary backpressure면 `false`
- 그 외는 `SubmitError` 계열 예외

### 5.2 callback completion `request`

이 절은 callback을 받는 `request(..., callback, ...)`만 다룬다.
`await`/`Promise`/coroutine 기반 `request`는 기존처럼 완료 결과를 반환하거나
reject하는 표면을 유지한다.

callback completion `request`의 submit 단계는 아래 규칙을 따른다.

- blocking submit 성공 시 `true`
- non-blocking submit에서 temporary backpressure면 `false`
- 그 외 submit 실패는 `SubmitError` 계열 예외

submit이 성공한 뒤의 완료 의미는 바꾸지 않는다.

- callback은 정확히 한 번 호출된다.
- reply 단계 실패는 callback의 `RequestResult` 또는 언어별 `RequestError`
  의미로 전달한다.

### 5.3 `publish`

`publish`는 `send`와 같은 submit 계열로 본다.

- blocking publish 성공 시 `true`
- non-blocking publish에서 temporary backpressure면 `false`
- 그 외 submit 실패는 `SubmitError` 계열 예외

이 규칙을 두는 이유는, `publish`도 결국 "지금 submit이 되었는가"가 핵심인
send 계열이기 때문이다.

### 5.4 `recv`

`tryRecv`는 제거하고 `recv(flags)` 하나로 정리한다.

- blocking recv 성공 시 payload 반환
- non-blocking recv에서 현재 데이터가 없으면 empty 표현 반환
- 실제 recv 실패는 `RecvError` 계열 예외

### 5.5 `subscribe`

`subscribe`도 receive 계열로 보고 `recv`와 같은 규칙을 따른다.

- blocking subscribe 성공 시 `TopicMessage` 반환
- non-blocking subscribe에서 현재 매칭 메시지가 없으면 empty 표현 반환
- 실제 recv 실패는 `RecvError` 계열 예외

## 6. 언어별 공개 shape 초안

아래 표는 방향을 설명하기 위한 초안이다.
정확한 타입명과 overload 조합은 각 binding 문서에서 구현 시점에 확정한다.

| 언어 | submit 계열 | receive 계열 |
|---|---|---|
| `.NET` | `bool Send(...)`, `bool Request(..., callback, ...)`, `bool Publish(...)` | `Received? Recv(...)`, `TopicMessage? Subscribe(...)` |
| `Java` | `boolean send(...)`, `boolean request(..., callback, ...)`, `boolean publish(...)` | `@Nullable Received recv(...)`, `@Nullable TopicMessage subscribe(...)` |
| `Node` | `send(...): boolean`, `request(..., callback, ...): boolean`, `publish(...): boolean` | `recv(...): Received \| null`, `subscribe(...): TopicMessage \| null` |
| `Python` | `send(...) -> bool`, `request(..., callback, ...) -> bool`, `publish(...) -> bool` | `recv(...) -> Received \| None`, `subscribe(...) -> TopicMessage \| None` |
| `C++` | `bool send(...)`, `bool request(..., callback, ...)`, `bool publish(...)` | `std::optional<received_t> recv(...)`, `std::optional<topic_message_t> subscribe(...)` |

Java는 nullability annotation 또는 문서 주석으로 non-blocking empty 반환 가능성을
분명하게 적어야 한다.

## 7. 정식 spec 반영 시 수정 대상

구현이 확정되면 최소한 아래 문서를 함께 갱신해야 한다.

- `doc/spec/bindings/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/cpp/README.md`

필요하면 관련 errno 설명과 submit failure 테스트 문서도 함께 맞춘다.
