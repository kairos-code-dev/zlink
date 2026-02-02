# .NET Binding Plan

## 목표
- C API를 기반으로 한 .NET 래퍼를 제공한다.
- 공통 계약은 `bindings/COMMON_API.md`를 따른다.
- 코어 버전(VERSION)과 바인딩 버전을 동일하게 유지한다.

## 설계 원칙 (net-zmq 참고)
- Socket은 thread-unsafe, Context만 thread-safe (코어 규칙 그대로).
- 에러는 예외 기반으로 노출하되 내부 errno 매핑을 유지한다.
- Message 소유권 규칙은 C API와 일치한다.
- .NET 8+를 기준으로 최신 P/Invoke 패턴(`LibraryImport`)을 사용한다.
- SafeHandle 기반의 자원 수명을 기본으로 한다.
- 타입 안전 옵션/열거형 중심으로 API를 구성한다.
- cppzmq 스타일의 메서드 시그니처를 유지한다.
- `LibraryImport` 소스 생성기 기반으로 성능/안전성 확보.

## API 표면 (1차/2차 합쳐서 전부 구현)
- Context, Socket, Message, Poller
- Monitor (monitor open/recv)
- Spot, Registry, Discovery, Provider, Gateway
- 유틸: atomic_counter, stopwatch, proxy (C API 그대로)

## 인터롭 전략
- P/Invoke로 `zlink` C API 호출 (LibraryImport 기반)
- SafeHandle 기반 소켓/컨텍스트 래핑
- 메시지 버퍼는 `Span<byte>`/`Memory<byte>`를 지원
- 문서화된 옵션만 노출
- 네이티브 바이너리는 별도 패키지(`Net.Zlink.Native.{rid}`)로 제공하는 구성을 기본으로 한다.

## 구조
- `bindings/dotnet/src/` : 관리 코드
- `bindings/dotnet/native/` : 네이티브 빌드/자산
- `bindings/dotnet/tests/` : xUnit 테스트
- `bindings/dotnet/samples/` : 최소 샘플 (PAIR, DEALER/ROUTER, SPOT, TLS)
- `bindings/dotnet/benchmarks/` : 성능 측정
- `bindings/dotnet/docs/` : 문서 (DocFX)
  - 라이브러리 패키지(`Net.Zlink`)와 네이티브 패키지(`Net.Zlink.Native.{rid}`) 분리

## 빌드/배포
- NuGet 패키지 제공
- 문서 사이트는 정적 사이트(예: DocFX)로 제공
- 코어 `release` 태그에서 소스/바이너리 자산을 가져오도록 문서화
- CI에서 Linux/macOS/Windows 빌드 및 테스트

## 성능 전략
- 작은 메시지: ArrayPool 기반 버퍼 재사용
- 큰 메시지: zero-copy 메시지(네이티브 할당 + 해제 콜백)
- 다중 소켓: Poller 사용을 기본 권장

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
