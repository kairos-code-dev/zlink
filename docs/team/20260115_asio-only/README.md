# ASIO-Only Migration

**상태:** 계획 단계 (Planning Phase)
**브랜치:** feature/asio-only
**작성일:** 2026-01-15

## 빠른 요약

현재 zlink는 ASIO backend를 이미 구현했지만, 조건부 컴파일(`ZMQ_IOTHREAD_POLLER_USE_ASIO`)로 인해 코드 복잡도가 높습니다. 이 프로젝트는 **ASIO 전용 코드로 단순화**하여 유지보수성을 향상시키는 것이 목표입니다.

## 핵심 목표

1. **코드 단순화**: 조건부 컴파일 매크로 제거
2. **성능 유지**: Baseline 대비 ±10% 이내
3. **기능 유지**: 모든 transport 및 소켓 패턴 지원

## 주요 문서

- **[plan.md](./plan.md)** - 상세 마이그레이션 계획 (1,151줄)
  - 현재 아키텍처 분석
  - 5단계 마이그레이션 계획
  - 성능 벤치마크 기준
  - 리스크 및 롤백 전략

## 마이그레이션 단계

| Phase | 설명 | 기간 | 상태 |
|-------|------|------|------|
| Phase 0 | 준비 및 기준 설정 | 1-2일 | 대기 |
| Phase 1 | Transport Layer 정리 | 2-3일 | 대기 |
| Phase 2 | I/O Thread Layer 정리 | 2-3일 | 대기 |
| Phase 3 | Build System 정리 | 1-2일 | 대기 |
| Phase 4 | 문서화 및 주석 정리 | 1일 | 대기 |
| Phase 5 | 최종 검증 및 성능 측정 | 2-3일 | 대기 |

**총 예상 기간:** 9-14일 (2026-01-15 ~ 2026-01-28)

## 현재 아키텍처

```
Application
    ↓
Socket API (zmq_socket, zmq_bind, ...)
    ↓
socket_base_t → session_base_t
    ↓
Engines (asio_engine_t, asio_ws_engine_t, asio_zmtp_engine_t)
    ↓
Transports (tcp/ipc/ssl/ws/wss_transport)
    ↓
Listener/Connecter (asio_*_listener/connecter)
    ↓
I/O Thread (io_thread_t) + ASIO Poller (asio_poller_t)
    ↓
Boost.ASIO (io_context)
```

## 제거 대상

이미 ASIO로 전환되어 있으므로 실제로는 **정리(cleanup)** 작업:

- `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` 조건부 컴파일 제거
- ASIO 경로만 남기고 단순화
- 불필요한 헤더 include 정리

## 시작하기

### Phase 0: Baseline 측정
```bash
# 1. 브랜치 확인
git checkout feature/asio-only

# 2. 빌드
./build-scripts/linux/build.sh x64 ON

# 3. 테스트
cd build/linux-x64 && ctest --output-on-failure

# 4. Baseline 성능 측정
taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20 > baseline.txt
```

### Phase 1-5: 각 단계별 작업
상세 내용은 [plan.md](./plan.md) 참조

## 성공 기준

- **기능:** 모든 테스트 통과 (64/64, 모든 플랫폼)
- **성능:** Baseline 대비 ±10% 이내
- **품질:** 메모리 누수 0건, 조건부 컴파일 100% 제거
- **문서:** CLAUDE.md, README.md 업데이트 완료

## 참고 문서

- [CLAUDE.md](../../CLAUDE.md) - 프로젝트 전체 가이드
- [이전 ASIO 성능 개선](../20260114_ASIO-성능개선/plan.md) - Speculative write 구현

## 문의

- 프로젝트: zlink (libzmq fork)
- 담당: dev-cxx agent
- 브랜치: feature/asio-only
