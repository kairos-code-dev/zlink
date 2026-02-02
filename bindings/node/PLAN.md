# Node.js Binding Plan

## 목표
- C API 기반 Node.js 바인딩 제공
- 공통 계약은 `bindings/COMMON_API.md` 준수
- 코어 버전과 바인딩 버전을 동일하게 유지

## 설계 원칙
- Socket은 thread-unsafe, Context만 thread-safe
- 예외 기반(throw) API 제공
- Message 소유권 규칙은 C API와 일치

## API 표면 (1차/2차 합쳐서 전부 구현)
- Context, Socket, Message, Poller
- Monitor (monitor open/recv)
- Spot, Registry, Discovery, Provider, Gateway
- 유틸: atomic_counter, stopwatch, proxy

## 인터롭 전략
- N-API 기반 C++ addon
- 네이티브 라이브러리 로딩은 플랫폼별 바이너리 제공
- Buffer/Uint8Array로 메시지 전송

## 구조
- `bindings/node/src/` : JS/TS API
- `bindings/node/native/` : N-API C++ addon
- `bindings/node/tests/` : Jest/Vitest 테스트
- `bindings/node/examples/` : 기본 샘플

## 빌드/배포
- npm 패키지 배포
- prebuildify 또는 node-pre-gyp로 바이너리 배포
- 코어 release 자산과 연동

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
- 빌드/런타임 로딩 가이드
