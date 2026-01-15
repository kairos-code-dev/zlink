# ASIO-Only Migration 체크리스트

**프로젝트:** zlink ASIO-Only Migration
**브랜치:** feature/asio-only
**시작일:** 2026-01-15
**목표 완료일:** 2026-01-28

이 체크리스트는 각 Phase의 진행 상황을 추적하기 위한 문서입니다.

## Phase 0: 준비 및 기준 설정 (1-2일)

**목표 날짜:** 2026-01-15 ~ 2026-01-16
**상태:** ⏸️ 대기

### 작업 항목
- [ ] Baseline 성능 측정 (Windows)
  - [ ] TCP latency p50/p99
  - [ ] TCP throughput (msg/s)
  - [ ] IPC latency (Windows는 N/A)
  - [ ] WebSocket latency
  - [ ] CPU 사용률
  - [ ] 메모리 사용량
- [ ] Baseline 성능 측정 (WSL/Linux)
  - [ ] TCP latency p50/p99
  - [ ] TCP throughput (msg/s)
  - [ ] IPC latency p50/p99
  - [ ] WebSocket latency
  - [ ] CPU 사용률
  - [ ] 메모리 사용량
- [ ] 모든 테스트 통과 확인 (64 tests)
  - [ ] Windows
  - [ ] WSL
  - [ ] Linux
- [ ] 코드 분석
  - [ ] `ZMQ_IOTHREAD_POLLER_USE_ASIO` 사용 위치 목록화
  - [ ] 제거 가능한 조건부 컴파일 식별
  - [ ] OS 분기 매크로 위치 확인

### 완료 기준
- [ ] Baseline 성능 데이터 기록 (`baseline.md`)
- [ ] 모든 테스트 통과 (64/64)
- [ ] 코드 분석 문서 작성 (`code_analysis.md`)

### 결과
- **성능 데이터:** (링크 또는 요약)
- **테스트 결과:** PASS / FAIL
- **발견 사항:** (특이사항 기록)

---

## Phase 1: 조건부 컴파일 제거 - Transport Layer (2-3일)

**목표 날짜:** 2026-01-17 ~ 2026-01-19
**상태:** ⏸️ 대기

### 작업 항목
- [ ] `session_base.cpp` 정리
  - [ ] ASIO include 조건부 컴파일 제거
  - [ ] Transport 생성 경로 단순화
- [ ] `socket_base.cpp` 정리
  - [ ] ASIO 관련 조건부 컴파일 제거
  - [ ] Include 단순화
- [ ] 빌드 스크립트 정리
  - [ ] CMakeLists.txt 주석 업데이트
  - [ ] `ZMQ_IOTHREAD_POLLER_USE_ASIO` 강제 설정 확인
- [ ] 테스트 검증
  - [ ] Linux x64 빌드 및 테스트
  - [ ] macOS ARM64 빌드 및 테스트
  - [ ] Windows x64 빌드 및 테스트

### 완료 기준
- [ ] 모든 플랫폼 빌드 성공 (3/3)
- [ ] 모든 테스트 통과 (64/64, 모든 플랫폼)
- [ ] 조건부 컴파일 50% 이상 제거
- [ ] 성능: baseline 대비 ±5% 이내

### 성능 검증
- **TCP Latency p99:** baseline vs phase1 = ___%
- **TCP Throughput:** baseline vs phase1 = ___%
- **판정:** PASS / FAIL

### 결과
- **제거된 파일 수:** ___
- **조건부 컴파일 제거율:** ___%
- **성능 영향:** (요약)
- **발견 사항:** (특이사항 기록)

---

## Phase 2: 조건부 컴파일 제거 - I/O Thread Layer (2-3일)

**목표 날짜:** 2026-01-20 ~ 2026-01-22
**상태:** ⏸️ 대기

### 작업 항목
- [ ] `io_thread.hpp` 정리
  - [ ] `#include <boost/asio.hpp>` 조건부 제거
  - [ ] `get_io_context()` 항상 제공
- [ ] `io_thread.cpp` 정리
  - [ ] ASIO 전용 경로로 단순화
  - [ ] 불필요한 분기 제거
- [ ] 관련 헤더 정리
  - [ ] `poller.hpp` - ASIO만 사용하도록 단순화
  - [ ] `poller_base.hpp` - 불필요한 추상화 제거 검토
- [ ] 테스트 검증
  - [ ] 모든 플랫폼 빌드 성공
  - [ ] 모든 테스트 통과 (64/64)

### 완료 기준
- [ ] io_thread 관련 조건부 컴파일 100% 제거
- [ ] 모든 테스트 통과 (64/64, 모든 플랫폼)
- [ ] 성능: baseline 대비 ±5% 이내
- [ ] 코드 라인 수 10% 이상 감소

### 성능 검증
- **TCP Latency p99:** baseline vs phase2 = ___%
- **TCP Throughput:** baseline vs phase2 = ___%
- **판정:** PASS / FAIL

### 결과
- **코드 라인 감소:** before ___ → after ___ (감소율 ___%)
- **조건부 컴파일 제거율:** ___%
- **성능 영향:** (요약)
- **발견 사항:** (특이사항 기록)

---

## Phase 3: Build System 정리 (1-2일)

**목표 날짜:** 2026-01-23 ~ 2026-01-24
**상태:** ⏸️ 대기

### 작업 항목
- [ ] `CMakeLists.txt` 정리
  - [ ] `ZMQ_IOTHREAD_POLLER_USE_ASIO` 강제 설정 단순화
  - [ ] 관련 옵션 정리
  - [ ] 주석 업데이트
- [ ] `platform.hpp.in` 정리
  - [ ] ASIO 관련 매크로 단순화
  - [ ] 불필요한 poller 선택 로직 제거
- [ ] Clean build 검증
  - [ ] Linux
  - [ ] macOS
  - [ ] Windows

### 완료 기준
- [ ] Clean build 성공 (모든 플랫폼)
- [ ] 모든 테스트 통과 (64/64)
- [ ] CMake 경고 0건
- [ ] 빌드 시간 측정 및 기록

### 빌드 성능
- **Linux 빌드 시간:** before ___ → after ___
- **Windows 빌드 시간:** before ___ → after ___
- **판정:** PASS / FAIL

### 결과
- **CMake 경고:** ___건
- **빌드 시간:** (요약)
- **발견 사항:** (특이사항 기록)

---

## Phase 4: 문서화 및 주석 정리 (1일)

**목표 날짜:** 2026-01-25
**상태:** ⏸️ 대기

### 작업 항목
- [ ] 주석 업데이트
  - [ ] "Phase 1-B", "Phase 1-C" 등 임시 주석 제거
  - [ ] ASIO 전용 동작 명확히 설명
  - [ ] Transport별 주석 일관성 확보
- [ ] `CLAUDE.md` 업데이트
  - [ ] ASIO-only 아키텍처 반영
  - [ ] 빌드 요구사항 업데이트
  - [ ] 제거된 feature 문서화
- [ ] `README.md` 업데이트
  - [ ] ASIO 기반 설명
  - [ ] 성능 특성 문서화

### 완료 기준
- [ ] 모든 임시 주석 제거
- [ ] CLAUDE.md 업데이트 완료
- [ ] README.md 업데이트 완료
- [ ] 문서 리뷰 완료

### 결과
- **제거된 임시 주석:** ___건
- **업데이트된 문서:** (리스트)
- **발견 사항:** (특이사항 기록)

---

## Phase 5: 최종 검증 및 성능 측정 (2-3일)

**목표 날짜:** 2026-01-26 ~ 2026-01-28
**상태:** ⏸️ 대기

### 작업 항목
- [ ] 전체 플랫폼 빌드 및 테스트
  - [ ] Linux x64
  - [ ] Linux ARM64
  - [ ] macOS x86_64
  - [ ] macOS ARM64
  - [ ] Windows x64
  - [ ] Windows ARM64 (cross-compile)
- [ ] 성능 벤치마크 (모든 플랫폼)
  - [ ] Windows TCP latency/throughput
  - [ ] WSL TCP latency/throughput
  - [ ] Linux TCP latency/throughput
  - [ ] IPC performance (Linux/macOS)
  - [ ] WebSocket performance
- [ ] 메모리 프로파일링
  - [ ] Valgrind (Linux)
  - [ ] Address Sanitizer (모든 플랫폼)
- [ ] CI/CD 검증
  - [ ] GitHub Actions workflow 확인
  - [ ] 모든 플랫폼 자동 빌드 성공

### 완료 기준
- [ ] 모든 플랫폼 빌드 성공 (6/6)
- [ ] 모든 테스트 통과 (64/64, 모든 플랫폼)
- [ ] 성능: baseline 대비 ±10% 이내
- [ ] 메모리 누수 0건
- [ ] CI/CD 통과

### 성능 최종 결과

#### Windows
- **TCP Latency p99:** baseline vs final = ___%
- **TCP Throughput:** baseline vs final = ___%
- **판정:** PASS / FAIL

#### WSL
- **TCP Latency p99:** baseline vs final = ___%
- **TCP Throughput:** baseline vs final = ___%
- **판정:** PASS / FAIL

#### Linux
- **TCP Latency p99:** baseline vs final = ___%
- **TCP Throughput:** baseline vs final = ___%
- **판정:** PASS / FAIL

### 메모리 검증
- **Valgrind 결과:** 누수 ___건
- **Address Sanitizer 결과:** 경고 ___건
- **판정:** PASS / FAIL

### 결과
- **최종 성능:** (요약)
- **메모리 안전성:** (요약)
- **발견 사항:** (특이사항 기록)

---

## 최종 릴리스 체크리스트

### 문서
- [ ] 성능 보고서 작성 (`performance_report.md`)
- [ ] Migration summary 작성 (`SUMMARY.md`)
- [ ] CHANGELOG.md 업데이트
- [ ] 버전 정보 업데이트

### 코드
- [ ] 조건부 컴파일 100% 제거 확인
- [ ] 코드 리뷰 완료
- [ ] 불필요한 파일 제거

### 품질
- [ ] 모든 플랫폼 테스트 통과 (6/6)
- [ ] 성능: baseline ±10% 이내
- [ ] 메모리 누수 0건
- [ ] Sanitizer 경고 0건

### CI/CD
- [ ] GitHub Actions 통과
- [ ] 릴리스 아티팩트 생성 확인

### 릴리스
- [ ] Git 태그 생성 (`v0.2.0-asio-only`)
- [ ] Release notes 작성
- [ ] main 브랜치 머지

---

## 진행 상황 요약

| Phase | 상태 | 시작일 | 완료일 | 결과 |
|-------|------|--------|--------|------|
| Phase 0: 준비 | ⏸️ 대기 | - | - | - |
| Phase 1: Transport | ⏸️ 대기 | - | - | - |
| Phase 2: I/O Thread | ⏸️ 대기 | - | - | - |
| Phase 3: Build System | ⏸️ 대기 | - | - | - |
| Phase 4: 문서화 | ⏸️ 대기 | - | - | - |
| Phase 5: 검증 | ⏸️ 대기 | - | - | - |

**전체 진행률:** 0% (0/6 Phase 완료)

---

## 범례

- ⏸️ 대기 (Pending)
- 🔄 진행중 (In Progress)
- ✅ 완료 (Completed)
- ❌ 실패 (Failed)
- ⚠️ 보류 (On Hold)

---

**마지막 업데이트:** 2026-01-15
**다음 업데이트:** Phase 0 완료 시
