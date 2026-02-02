# Python Binding Plan

## 목표
- C API 기반 Python 바인딩 제공
- 공통 계약은 `bindings/COMMON_API.md` 준수
- 코어 버전과 바인딩 버전을 동일하게 유지

## 설계 원칙
- Socket은 thread-unsafe, Context만 thread-safe
- 예외 기반 API, errno 매핑 유지
- Message 소유권 규칙은 C API와 일치
- Pythonic API를 제공하되 메서드 이름/개념은 공통 계약과 맞춘다

## API 표면 (1차/2차 합쳐서 전부 구현)
- Context, Socket, Message, Poller
- Monitor (monitor open/recv)
- Spot, Registry, Discovery, Provider, Gateway
- 유틸: atomic_counter, stopwatch, proxy

## 인터롭 전략
- 1안: CPython C-API 확장 모듈(최고 성능)
- 2안: CFFI 기반 모듈(개발 속도/이식성)
- 배포용 바이너리는 manylinux/macos/windows wheel 제공

## 구조
- `bindings/python/src/` : Python API
- `bindings/python/native/` : C/CFFI 모듈
- `bindings/python/tests/` : pytest 테스트
- `bindings/python/examples/` : 기본 샘플

## 빌드/배포
- `pyproject.toml` 기반 빌드 (PEP 517)
- PyPI 배포
- 코어 release 태그 자산을 참조하거나 로컬 빌드

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
- wheel 설치와 로컬 빌드 방법
