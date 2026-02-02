# Java Binding Plan

## 목표
- C API 기반 Java 래퍼 제공
- 공통 계약은 `bindings/COMMON_API.md` 준수
- 코어 버전과 바인딩 버전을 동일하게 유지

## 설계 원칙
- Socket은 thread-unsafe, Context만 thread-safe
- 예외 기반 API, errno 매핑 유지
- Message 소유권 규칙은 C API와 일치

## API 표면 (1차/2차 합쳐서 전부 구현)
- Context, Socket, Message, Poller
- Monitor (monitor open/recv)
- Spot, Registry, Discovery, Provider, Gateway
- 유틸: atomic_counter, stopwatch, proxy

## 인터롭 전략
- JNI 기반의 얇은 래퍼
- native 라이브러리 로딩은 OS별 아키텍처 감지 후 처리
- ByteBuffer(Direct) 중심으로 메시지 전송

## 구조
- `bindings/java/src/main/java/` : Java API
- `bindings/java/src/main/cpp/` : JNI glue
- `bindings/java/src/test/java/` : JUnit 테스트
- `bindings/java/examples/` : 기본 샘플

## 빌드/배포
- Gradle 기반 빌드
- Maven Central 배포(예정)
- 코어 release 태그에서 native 바이너리 사용 또는 로컬 빌드

## 테스트 시나리오
- Context 생성/종료
- Socket 생성/종료
- PAIR send/recv
- DEALER/ROUTER 라우팅
- 옵션 set/get
- Poller 동작
- Monitor 이벤트
- Spot publish/recv

## 문서
- 스레드 모델, 메시지 소유권, 에러 매핑
- JNI 환경 설정/빌드 방법
