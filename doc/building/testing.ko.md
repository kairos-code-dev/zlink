# 테스트 전략 및 실행

## 1. 테스트 프레임워크

zlink은 **Unity** (C 기반 단위 테스트 프레임워크)를 사용한다.

## 2. 테스트 실행

### 전체 테스트
```bash
ctest --test-dir build --output-on-failure
```

### 특정 테스트
```bash
ctest --test-dir build -R test_pair --output-on-failure
```

## 3. 테스트 디렉토리 구조

| 디렉토리 | 용도 |
|----------|------|
| `core/tests/` | 기능 테스트 (사용자 관점 동작 검증) |
| `core/unittests/` | 내부 로직 테스트 |

### 기능별 테스트 배치
```
tests/
├── routing-id/
│   └── test_router_auto_id_format.cpp
├── monitoring/
│   └── test_monitor_enhanced.cpp
├── discovery/
│   └── test_service_discovery.cpp
└── spot/
    └── test_spot_pubsub_basic.cpp
```

## 4. 테스트 작성 가이드

- behavior 변경 시 `tests/`에 추가
- internal 변경 시 `unittests/`에 추가
- 플랫폼별 스킵 사항은 PR에 기재

## 5. 바인딩 통합 테스트

### 5.1 공통 시나리오

| 시나리오 | 설명 |
|----------|------|
| S1 | Context/Socket Lifecycle |
| S2 | PAIR basic roundtrip |
| S3 | PUB/SUB basic |
| S4 | DEALER/ROUTER basic |
| S5 | XPUB/XSUB subscription propagation |
| S6 | Message multipart |
| S7 | Socket options |
| S8 | Registry/Discovery |
| S9 | Gateway |
| S10 | Spot |

### 5.2 Transport 루프

모든 시나리오는 3가지 transport로 실행:
- `tcp://127.0.0.1:PORT`
- `ws://127.0.0.1:PORT`
- `inproc://name`

### 5.3 언어별 테스트 위치

| 언어 | 프레임워크 | 위치 |
|------|-----------|------|
| .NET | xUnit | `bindings/dotnet/tests/` |
| Java | JUnit | `bindings/java/src/test/` |
| Node.js | node:test | `bindings/node/tests/` |
| Python | unittest | `bindings/python/tests/` |
