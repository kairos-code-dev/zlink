# 계획 리뷰 (Gemini)

## 결론
- **최종 판단: 수정필요 (Critical Issues)**
- WSL 환경 특성으로 인한 성능 왜곡 가능성이 매우 높음
- 파일시스템 및 빌드 타겟 통일 필수

## 주요 이슈

### 1. 파일시스템 성능 이슈 ✅ 해결됨

**원래 우려:**
- `/mnt/d/` (NTFS 마운트)에서 빌드/실행 시 I/O 성능이 `/home/ulalax/` (ext4)보다 **현저히 저하**됨
- 파일시스템 차이로 인해 벤치마크 결과가 **심각하게 왜곡**될 가능성

**실제 상황:**
- ✅ **libzmq-ref는 이미 `/home/ulalax/project/ulalax/libzmq-ref`에 있음**
- ✅ **ext4 파일시스템 사용 중**
- ✅ **복사 작업 불필요**

**결과:**
- Gemini가 우려했던 NTFS vs ext4 성능 차이 문제는 **존재하지 않음**
- 이미 최적의 환경에 있음

### 2. 빌드 타겟 통일 필요 ⚠️ CRITICAL

**문제:**
- 현재 계획은 zlink(Linux) vs libzmq-ref(Windows?) 비교를 암시
- OS 스케줄러 차이(Windows vs Linux)가 I/O 모델 차이보다 더 큰 영향

**해결책:**
- libzmq-ref도 **WSL Linux 타겟으로 빌드**
- 동일 OS, 동일 파일시스템에서 벤치마크 수행

### 3. 프로파일링 도구 준비

**문제:**
- `perf`는 WSL2에서 사용 가능하지만 커널 버전에 맞는 패키지 설치 필요
- Windows 바이너리는 `perf`로 프로파일링 불가

**해결책:**
```bash
# linux-tools 설치
sudo apt-get update
sudo apt-get install linux-tools-generic linux-tools-$(uname -r)

# perf 확인
perf list
```

### 4. 동적 분석 추가

**제안:**
- 정적 분석(소스 코드 리뷰)와 함께 동적 분석 추가
- `strace -c`로 시스템 콜 비용 비교
- `epoll_wait` vs `select` 호출 빈도 및 타임아웃 분석

```bash
# 시스템 콜 통계
strace -c -f ./bench_router_dealer tcp 64 1000000

# 특정 시스템 콜 추적
strace -e epoll_wait,select,poll -f ./bench_router_dealer tcp 64 1000000
```

## 수정 제안 사항 (업데이트)

### 단계 0에 추가 필요:

1. **[해결됨] 파일시스템 통일**
   ```
   - 불필요: libzmq-ref는 이미 /home/ulalax/project/ulalax/libzmq-ref에 있음
   - 이미 ext4 파일시스템 사용 중
   ```

2. **[필수] 빌드 타겟 통일**
   ```
   - libzmq-ref를 WSL Linux용으로 빌드
   - 이유: Windows vs Linux 스케줄러 차이 제거
   ```

3. **[필수] 프로파일링 도구 설치**
   ```bash
   sudo apt-get install linux-tools-generic linux-tools-$(uname -r)
   perf list  # 확인
   ```

4. **[추가] 동적 분석 도구**
   ```
   - strace -c: 시스템 콜 통계
   - strace -e: 특정 시스템 콜 추적 (epoll_wait, select, poll)
   ```

### 단계 1에 추가 필요:

**동적 분석 추가:**
```
- strace를 사용한 시스템 콜 패턴 비교
- epoll_wait vs select 호출 빈도 및 타임아웃 분석
- 시스템 콜 비용 비교 (strace -c)
```

## 기타 확인 사항

### 경로 접근성
- `/mnt/d/project/ulalax/libzmq-ref`는 표준 WSL 마운트 경로
- 접근 가능 여부는 사용자가 `ls /mnt/d/project/ulalax/libzmq-ref`로 확인 필요

### 벤치마크 스크립트
- `benchwithzmq/run_benchmarks.sh` 존재 확인됨
- `benchwithzmq/run_comparison.py` 존재 확인됨
- 실행 기반 마련됨

## 리스크 재평가

### High Priority
1. **파일시스템 성능 왜곡** ⚠️
   - 현재: NTFS vs ext4 차이로 인한 왜곡
   - 대응: libzmq-ref를 ~/로 복사 필수

2. **OS 스케줄러 차이** ⚠️
   - 현재: Windows vs Linux 스케줄러 차이
   - 대응: 모두 WSL Linux로 빌드 필수

### Medium Priority
3. **프로파일링 도구 불일치**
   - 현재: Linux perf vs Windows ETW
   - 대응: 모두 perf 사용 (Linux 빌드)

## 결론

계획의 기본 구조는 우수하나, **WSL 환경 특성을 고려한 보완이 필수**입니다.

**Critical 이슈:**
1. libzmq-ref를 WSL 내부 파일시스템으로 복사
2. libzmq-ref를 Linux 타겟으로 빌드
3. 프로파일링 도구 설치

이 3가지를 단계 0에 명시적으로 추가해야 합니다.

**최종 판단: 수정 후 승인 가능**
