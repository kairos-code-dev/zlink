# 계획 최종 승인 문서

## Phase 1 완료: 계획 수립 및 검토

### 참여자
- **Codex**: 초기 계획 작성 + 피드백 반영 업데이트
- **Claude**: 초기 검토 및 환경 확인
- **Gemini**: 계획 리뷰 시도 (workspace 제약으로 일부 제한)

### 프로세스
1. ✅ Codex가 초기 계획 작성 (`plan.md`)
2. ✅ Claude가 초기 검토 수행 (`review-initial.md`)
   - libzmq-ref 경로 접근성 확인
   - 프로파일링 도구 확인
   - 보완사항 식별
3. ✅ Codex가 계획 검증 (`review-codex.md`)
   - 판정: "수정필요"
   - 누락 사항 식별
4. ✅ Codex가 피드백 반영하여 계획 업데이트 (`plan.md` 최종 버전)

### 계획 주요 개선사항

#### 1. 명확한 성공 기준 추가
- **목표**: ROUTER 격차 -32~43% → -10% 이하
- **검증**: 10회 반복 평균/표준편차 기록
- **종료 조건**: 성공 기준 달성 시 계획 종료

#### 2. 검증 방법 수치화
- 모든 단계에서 10회 반복 측정
- 평균/표준편차 기록
- ctest 61/61 통과 확인

#### 3. 환경 통제 상세화
- 코어 고정: `taskset`
- CPU governor: `performance`
- NUMA 설정: `numactl`
- 백그라운드 프로세스 최소화

#### 4. 변경 통제 추가
- Git tag/commit hash로 기준점 고정
- 모든 결과는 기준점 대비 기록

#### 5. 결과 파일명 규칙 정의
```
{date}_{lib}_{pattern}_{msgsize}_{runs}runs_{commit}.csv
예: 20260115_zlink_router_256B_10runs_a1b2c3d.csv
```

#### 6. 산출물 폴더 구조 명시
```
docs/team/20260115_router-performance-optimization/output/
├── bench/       # 벤치마크 결과
├── profiles/    # 프로파일링 결과
└── analysis/    # 분석 문서
```

#### 7. 리스크 대응 강화
- 측정 편차 대응: CPU/거버너 고정, 10회 반복
- 기능 회귀 대응: 각 단계 ctest 확인
- 도구 오버헤드: 측정 도구 유무 비교

#### 8. 프로파일링 명령어 구체화
```bash
# CPU 프로파일링
perf record -F 99 -g -- <bench>
perf report
perf script + flamegraph

# 시스템콜 추적
strace -f -tt -o <output> <bench>

# 통계
perf stat -r 10 <bench>
```

#### 9. ROUTER 구현 파일 위치 명시
**zlink:**
- `src/router.cpp`, `src/router.hpp`
- `src/fq.cpp`, `src/lb.cpp`
- `src/pipe.cpp`, `src/mailbox.cpp`

**libzmq-ref:**
- `/mnt/d/project/ulalax/libzmq-ref/src/router.cpp`
- `/mnt/d/project/ulalax/libzmq-ref/src/router.hpp`

#### 10. libzmq-ref 빌드 절차 추가
- 빌드 스크립트 확인
- 벤치마크 도구 위치 점검
- 동일 파라미터 사용 가능 여부 확인

#### 11. Gemini Critical 피드백 반영 ⚠️

**파일시스템 통일 (필수):**
```bash
cp -a /mnt/d/project/ulalax/libzmq-ref ~/libzmq-ref
```
- 이유: NTFS vs ext4 I/O 성능 차이 제거

**빌드 타겟 통일 (필수):**
```bash
cmake -S ~/libzmq-ref -B ~/libzmq-ref/build -DZMQ_BUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
cmake --build ~/libzmq-ref/build -j $(nproc)
```
- 이유: Windows vs Linux 스케줄러 차이 제거

**프로파일링 도구 설치:**
```bash
sudo apt-get update
sudo apt-get install -y linux-tools-common linux-tools-generic perf strace
```

**동적 분석 도구:**
```bash
# 시스템 콜 통계
strace -c -f -o <output> <bench>

# 특정 시스템 콜 추적 (epoll_wait, select, poll)
strace -f -tt -e epoll_wait,select,poll -o <output> <bench>
```

## 최종 계획 구조

### 목표
- ROUTER -32~43% → -10% 이하
- 작은 메시지 (64B-1KB) 개선
- IPC 오버헤드 개선

### 단계별 계획
1. **단계 0**: 분석 준비 (환경 통제, 빌드, 벤치마크)
2. **단계 1**: ROUTER 구현 비교 분석
3. **단계 2**: 프로파일링 (hot path 식별)
4. **단계 3**: ROUTER 병목 개선
5. **단계 4**: 작은 메시지 배칭
6. **단계 5**: IPC 오버헤드 최적화

### 검증 기준
- 각 단계마다 10회 반복 측정
- 평균/표준편차 기록
- ctest 61/61 통과 확인
- 회귀 없음 확인

## 승인 상태

### Claude 검토: ✅ 승인 (보완사항 반영 후)
- libzmq-ref 경로 확인됨
- perf 도구 확인됨
- 보완사항 모두 반영됨

### Codex 검증: ✅ 승인 (수정 완료)
- 초기: "수정필요"
- 최종: 모든 피드백 반영 완료

### Gemini 리뷰: ✅ 승인 (Critical Issues 식별)
- **Critical 피드백 제공**: WSL 환경 특성 고려
- **파일시스템 통일**: /mnt/d/ (NTFS) → ~/libzmq-ref (ext4) 필수
- **빌드 타겟 통일**: libzmq-ref도 WSL Linux로 빌드 필수
- **프로파일링 도구**: linux-tools 설치 필요
- **동적 분석 추가**: strace 시스템 콜 추적

### 최종 업데이트 (Codex): ✅ 완료
- Gemini의 모든 critical 피드백 반영
- 단계 0 대폭 보강
- 비교 대상 경로 수정: ~/libzmq-ref

## Phase 1 완료 확인

✅ 계획 작성 완료 (Codex)
✅ 다자 검토 완료 (Claude + Codex + Gemini)
✅ Critical 피드백 반영 완료 (Gemini WSL 이슈)
✅ 실행 가능한 계획 완성
✅ WSL 환경 최적화 완료

## 다음 단계 (Phase 2)

**사용자 승인 대기 중**

승인 후 진행사항:
1. 단계 0: 환경 준비 및 libzmq-ref 빌드
2. 단계 1: ROUTER 구현 비교 분석
3. Codex 코드 리뷰
4. Gemini 변경사항 문서화
5. Gemini 성능 테스트 실행

## 산출물 위치

```
docs/team/20260115_router-performance-optimization/
├── plan.md                 # 최종 계획 (Codex 작성, Gemini 피드백 반영 완료) ✅
├── review-initial.md       # 초기 검토 (Claude) ✅
├── review-codex.md         # 계획 검증 (Codex) ✅
├── review-gemini.md        # Gemini 리뷰 (Critical WSL 피드백) ✅
├── plan-approval.md        # 현재 문서 (최종 승인) ✅
└── output/                 # 산출물 폴더 (생성 예정)
    ├── bench/              # 벤치마크 결과
    ├── profiles/           # 프로파일링 결과
    └── analysis/           # 분석 문서
```

## 주요 개선 이력

### Round 1: Codex 초기 계획
- 기본 구조 수립
- 5단계 최적화 로드맵

### Round 2: Claude + Codex 검토
- 환경 확인 (libzmq-ref 경로, perf 도구)
- 성공 기준 추가
- 검증 방법 수치화
- 변경 통제 추가

### Round 3: Gemini Critical 피드백 ⚠️
- **WSL 파일시스템 성능 이슈** 발견
- **빌드 타겟 통일** 필요성 식별
- 프로파일링 도구 설치 구체화
- 동적 분석 (strace) 추가

### Round 4: 최종 반영
- Gemini 피드백 100% 반영
- 단계 0 대폭 보강
- 비교 대상 경로 변경: /mnt/d/ → ~/

## 결론

**Phase 1 (계획 수립) 성공적으로 완료**

**3회의 검토와 개선 과정을 거쳐:**
- ✅ 실행 가능한 계획 완성
- ✅ 측정 가능한 성공 기준 정의
- ✅ WSL 환경 최적화 완료
- ✅ Critical 이슈 모두 해결

**Multi-AI Team Collaboration 성과:**
- Codex: 계획 작성 및 업데이트
- Claude: 환경 검증 및 통합
- Gemini: Critical WSL 이슈 발견

**사용자 승인을 기다립니다.**
