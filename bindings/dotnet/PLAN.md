# .NET Binding Plan

## 목표
- C API를 기반으로 한 .NET 래퍼를 제공한다.
- 공통 계약은 `bindings/COMMON_API.md`를 따른다.
- 코어 버전(VERSION)과 바인딩 버전을 동일하게 유지한다.

## 설계 원칙
- Socket은 thread-unsafe, Context만 thread-safe (코어 규칙 그대로).
- 에러는 예외 기반으로 노출하되 내부 errno 매핑을 유지한다.
- Message 소유권 규칙은 C API와 일치한다.

## API 표면 (1차/2차 합쳐서 전부 구현)
- Context, Socket, Message, Poller
- Monitor (monitor open/recv)
- Spot, Registry, Discovery, Provider, Gateway
- 유틸: atomic_counter, stopwatch, proxy (C API 그대로)

## 인터롭 전략
- P/Invoke로 `zlink` C API 호출
- SafeHandle 기반 소켓/컨텍스트 래핑
- 메시지 버퍼는 `Span<byte>`/`Memory<byte>`를 지원
- 문서화된 옵션만 노출

## 구조
- `bindings/dotnet/src/Zlink/` : 관리 코드
- `bindings/dotnet/src/Zlink.Native/` : P/Invoke 선언
- `bindings/dotnet/tests/` : xUnit 테스트
- `bindings/dotnet/examples/` : 최소 샘플 (PAIR, DEALER/ROUTER, SPOT, TLS)

## 빌드/배포
- NuGet 패키지 제공
- 코어 `release` 태그에서 소스/바이너리 자산을 가져오도록 문서화
- CI에서 Linux/macOS/Windows 빌드 및 테스트

## 테스트 시나리오
- Context 생성/종료
- Socket 생성/종료
- PAIR 기본 send/recv
- DEALER/ROUTER 라우팅
- 옵션 set/get
- Poller 동작
- Monitor 이벤트 수신
- Spot publish/recv

## 문서
- API 사용법, 스레드 모델, 메시지 소유권 규칙
- 언어별 차이와 예외 매핑 정책
