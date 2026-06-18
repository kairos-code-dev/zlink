# SupportChat C++ Sample

SupportChat 샘플은 고객과 상담원이 conversation spot에 참여하고, 메시지, typing, idle, close 상태를 주고받는 흐름을 C++ framework 구조로 보여준다.

## 실행

이 샘플은 현재 CMake sample target과 `test_cpp_framework_sample_parity`에서 build/contract 검증을 수행한다. full client/server runner는 서버 역할별 실행과 stream endpoint 준비가 필요하다.

## Topology

- `Client`는 고객, 상담원, 재접속 고객 흐름을 시나리오처럼 표현한다.
- `Server/Api`는 인증과 conversation open 요청을 받는다.
- `Server/Support`는 actor, Entry Spot, Conversation Spot, timer, notification 책임을 가진다.
- `Server/Session`은 stream session 인증과 conversation 참여를 담당한다.
- `Shared`는 conversation 메시지 계약을 정의한다.

## Success Condition

`test_cpp_framework_sample_parity`가 conversation 상태 전이, Entry Spot admission, actor handler 등록을 검증한다.

## 회귀 테스트

`ctest -R test_cpp_framework_sample_parity`와 CMake sample target build가 회귀 테스트 역할을 한다.
