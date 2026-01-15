# ROUTER 성능 최적화 계획

## 목표
- ROUTER 패턴 성능 격차(-32% ~ -43%)를 최우선으로 축소하고, 작은 메시지(64B-1KB) 및 IPC 오버헤드 개선으로 전체 성능을 안정화한다.
- zlink(ASIO 기반)와 libzmq-ref(epoll/select 기반)의 구현 차이를 구조적으로 비교하여 병목 원인을 규명한다.
- 성공 기준 및 종료 조건: ROUTER 격차를 -10% 이하로 축소하고, 동일 조건에서 10회 반복 평균/표준편차로 개선을 확인한 뒤 계획을 종료한다.

## 범위 및 기준 자료
- 기준 분석: `docs/performance/baseline/BASELINE_ANALYSIS.md`
- 비교 대상: `/home/ulalax/project/ulalax/zlink` vs `/home/ulalax/project/ulalax/libzmq-ref`
- 우선순위: ROUTER -> 작은 메시지 배칭 -> IPC
- 변경 통제: 기준점은 git tag 또는 특정 commit hash로 고정하고, 모든 결과는 해당 기준점과의 비교로 기록한다.

## 단계 0: 분석 준비 및 동일 조건 확보
- 빌드 타겟 통일(필수): libzmq-ref를 WSL Linux용으로 빌드해 Windows vs Linux 스케줄러 차이를 제거.
  - 명령어:
    - `cmake -S /home/ulalax/project/ulalax/libzmq-ref -B /home/ulalax/project/ulalax/libzmq-ref/build -DZMQ_BUILD_TESTS=ON -DBUILD_BENCHMARKS=ON`
    - `cmake --build /home/ulalax/project/ulalax/libzmq-ref/build -j $(nproc)`
- 빌드 옵션 동기화: 컴파일러, 최적화 레벨, assert/trace, CXX 표준.
- 실행 환경 동기화: 코어 고정(`taskset`), CPU governor(`performance`), NUMA 설정(`numactl --cpunodebind/--membind`), 백그라운드 프로세스 최소화, 동일한 메시지 크기/패턴.
- libzmq-ref 빌드 및 벤치마크 도구 확인: `/home/ulalax/project/ulalax/libzmq-ref`에서 빌드 스크립트와 bench 실행 파일 위치를 점검하고 동일 파라미터 사용 가능 여부 확인.
- 벤치마크 재현 스크립트 정리: zlink와 libzmq-ref에서 동일 파라미터로 실행, 워밍업 횟수/측정 횟수 고정.
- 프로파일링 도구 설치:
  - `sudo apt-get update`
  - `sudo apt-get install -y linux-tools-common linux-tools-generic linux-tools-$(uname -r) perf strace`
- 동적 분석 도구 추가:
  - `strace -c -f -o <output> <bench>` (시스템 콜 통계)
  - `strace -f -tt -e epoll_wait,select,poll -o <output> <bench>` (특정 시스템 콜 추적)
- 결과 파일명 규칙 정의: `{date}_{lib}_{pattern}_{msgsize}_{runs}runs_{commit}.csv` (예: `20260115_zlink_router_256B_10runs_a1b2c3d.csv`).
- 산출물 폴더 구조: `docs/team/20260115_router-performance-optimization/output/` 아래에 `bench/`, `profiles/`, `analysis/` 하위 디렉터리로 분류 저장.

검증
- 동일 파라미터로 baseline 벤치마크 10회 반복 실행, 평균/표준편차 기록 및 결과 저장.
- 회귀 테스트: `ctest` 61/61 통과 확인.

## 단계 1: ROUTER 구현 비교 분석 (최우선)
### 비교 범위
- 라우팅 테이블/ID 매핑 구조, 재사용 정책, 충돌/삭제 처리.
- 메시지 경로: 입력 큐 -> 라우팅 -> 출력 큐 경로에서의 복사/복합 프레이밍 비용.
- 멀티파트 처리, affinity, backpressure, pipe 활성/비활성 전환 로직.
- 이벤트 루프/폴링 모델 차이(ASIO vs epoll/select)로 인한 wakeup 및 scheduling 비용.

### 작업 항목
- zlink: `src/` ROUTER 관련 클래스 및 pipe/queue 경로 정리.
- libzmq-ref: `/home/ulalax/project/ulalax/libzmq-ref`의 ROUTER 구현 파일 위치 명시(예: `src/router.cpp`, `src/router.hpp`), poller/pipe 흐름 확인.
- 주요 차이점 목록화: 데이터 구조, 호출 경로, lock 범위, 메모리 할당 패턴.

검증
- 동일 ROUTER 벤치마크 10회 반복(작은 메시지 포함) 후 평균/표준편차로 차이 유지 여부 확인.

## 단계 2: 프로파일링 계획 (원인 규명)
### 측정 포인트
- CPU 샘플링: hot path 식별(라우팅, 큐잉, 복사, wakeup).
- 락 경합/원자 연산 비용(큐/pipe 동기화).
- 메모리 할당/해제 빈도 및 per-message overhead.
- IPC 경로: syscall 빈도, context switch, memcpy/zero-copy 여부.

### 도구/방법
- Linux(zlink): `perf record -F 99 -g -- <bench>` -> `perf report`, `perf script` + flamegraph, `perf stat -r 10`로 시스템콜/분기 확인.
- syscall trace: `strace -f -tt -o <output> <bench>`로 IPC 경로 점검.
- Windows(libzmq-ref): ETW/Windows Performance Recorder 또는 WSL perf(가능 범위)로 동일 샘플링.
- 동일 메시지 크기(64B, 256B, 1KB) 및 ROUTER 패턴 집중 측정.

검증
- 프로파일 로그와 flamegraph 아카이브(`output/profiles/`), 주요 hot path 비교 표 정리.

## 단계 3: 최적화 우선순위 1 - ROUTER 병목 개선
### 후보 방향
- 라우팅 테이블 조회/갱신 비용 절감(해시 구조, 캐시 친화성).
- 불필요한 복사 제거(프레임 이동/참조 전달), 분기 최소화.
- pipe 활성화/비활성 전환 및 wakeup 빈도 최적화.

검증
- ROUTER 벤치마크 10회 반복 후 throughput/latency 평균/표준편차 기록.
- 작은 메시지(64B-1KB) 지표 개선 여부 및 성공 기준(-10% 이하) 달성 확인.
- 회귀 테스트: `ctest` 61/61 통과 확인.

## 단계 4: 최적화 우선순위 2 - 작은 메시지 배칭
### 후보 방향
- 작은 메시지에 대한 내부 배칭/병합 기준 검토.
- 큐/pipe에서 고정 오버헤드가 큰 경로 최소화.
- 최소 복사 경로 확보(프레임 공유/참조 카운트).

검증
- 작은 메시지 중심 벤치마크(64B/256B/1KB) 10회 반복 및 결과 비교.
- 회귀 테스트: `ctest` 61/61 통과 확인.

## 단계 5: 최적화 우선순위 3 - IPC 오버헤드
### 후보 방향
- IPC transport의 read/write 경로에서 syscall 수 절감.
- wakeup/notify 전략 점검(과도한 wakeup 방지).
- 고정 크기 버퍼 및 zero-copy 가능한 경로 검토.

검증
- IPC 전용 벤치마크 10회 반복 및 throughput/latency 평균/표준편차 측정.
- 회귀 테스트: `ctest` 61/61 통과 확인.

## 산출물 및 기록
- 비교 분석 문서: ROUTER 구현 차이와 원인 가설 정리.
- 프로파일링 결과: 핵심 hot path, 비용 추정치, 최적화 후보(`output/profiles/`).
- 단계별 벤치마크 결과 표: 전/후 비교, 개선율 기록(`output/bench/`).
- 분석 요약 및 결정 로그: `output/analysis/`에 기준점, 변경 내역, 결론 기록.

## 리스크 및 대응
- 플랫폼 차이(Windows vs Linux)로 인한 비교 왜곡 가능성: 동일 환경을 가능한 한 WSL 기반으로 정렬.
- 측정 편차(thermal throttling, background noise): CPU 고정/거버너 고정, 10회 반복 평균/표준편차로 완화.
- 기능 회귀 위험: 각 단계 종료 시 `ctest` 61/61 통과로 확인.
- 도구 오버헤드(perf/strace): 측정 도구 유무 비교로 영향 기록.
- ASIO/epoll 설계 차이로 인한 구조적 한계: 개선 가능 범위와 근본 한계 분리 기록.
