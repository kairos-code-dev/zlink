# Java Binding Plan

## 목표
- C API 기반 Java 래퍼 제공
- 공통 계약은 `bindings/COMMON_API.md` 준수
- 코어 버전과 바인딩 버전을 동일하게 유지

## 설계 원칙 (jvm-zmq 참고)
- Socket은 thread-unsafe, Context만 thread-safe
- 예외 기반 API, errno 매핑 유지
- Message 소유권 규칙은 C API와 일치
- JDK 22+의 FFM API(외부 함수/메모리) 기반 구현을 1순위로 한다.
- 리소스 관리는 AutoCloseable + Cleaner를 조합해 안전하게 처리한다.
  - Cleaner는 사용자가 close를 잊어도 네이티브 핸들 누수를 방지하기 위한 안전장치

## API 표면 (1차/2차 합쳐서 전부 구현)
- Context, Socket, Message, Poller
- Monitor (monitor open/recv)
- Spot, Registry, Discovery, Receiver, Gateway
- 유틸: atomic_counter, stopwatch, proxy

## 인터롭 전략
- FFM API 기반의 얇은 래퍼
- native 라이브러리 로딩은 OS별 아키텍처 감지 후 처리
- ByteBuffer(Direct) 중심으로 메시지 전송
  - 네이티브 라이브러리는 패키지 리소스로 동봉하고 런타임에 추출 후 로드

## 구조
- `bindings/java/src/main/java/` : Java API
- `bindings/java/src/main/cpp/` : (옵션) JNI glue
- `bindings/java/src/test/java/` : JUnit 테스트
- `bindings/java/examples/` : 기본 샘플

## 빌드/배포
- Gradle 기반 빌드
- GitHub Packages 또는 Maven Central 배포
- 코어 release 태그에서 native 바이너리 사용 또는 로컬 빌드
  - `native/<os>-<arch>/` 규칙으로 리소스 배치

## 런타임 요구사항
- `--enable-native-access=ALL-UNNAMED` 사용 안내
- OS/아키텍처별 네이티브 라이브러리 로딩 정책 문서화

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
