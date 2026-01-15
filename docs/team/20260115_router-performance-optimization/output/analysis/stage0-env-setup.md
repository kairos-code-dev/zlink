# Stage 0: 환경 준비 상태

**날짜**: 2026-01-15
**작성자**: Claude (Multi-AI Team)

## 환경 확인 결과

### 1. 비교 대상 경로 ✅

**zlink:**
- 위치: `/home/ulalax/project/ulalax/zlink`
- 파일시스템: ext4
- 브랜치: `feature/performance-optimization`

**libzmq-ref:**
- 위치: `/home/ulalax/project/ulalax/libzmq-ref`
- 파일시스템: ext4
- 빌드 상태: ✅ 이미 빌드됨

**파일시스템 통일:** ✅ 완료 (둘 다 ext4)

### 2. libzmq-ref 빌드 상태 ✅

**빌드 디렉터리:**
```
/home/ulalax/project/ulalax/libzmq-ref/build/
├── bin/
│   ├── benchmark_radix_tree
│   ├── inproc_lat
│   ├── inproc_thr
│   ├── local_lat
│   ├── local_thr
│   ├── proxy_thr
│   ├── remote_lat
│   └── remote_thr
└── lib/
    └── libzmq.so
```

**ROUTER 전용 벤치마크:**
```
/home/ulalax/project/ulalax/libzmq-ref/perf/router_bench/build/
├── router_latency
└── router_throughput
```

**빌드 타겟:** ✅ WSL Linux (ext4 파일시스템)

### 3. 벤치마크 도구 비교

**zlink:**
- 위치: `/home/ulalax/project/ulalax/zlink/benchwithzmq/`
- 도구: `run_benchmarks.sh`, `run_comparison.py`
- 측정 항목: ROUTER/DEALER, PAIR, PUB/SUB, 다양한 메시지 크기, TCP/IPC/inproc

**libzmq-ref:**
- 기본 벤치마크: local_lat, local_thr, remote_lat, remote_thr
- ROUTER 전용: router_latency, router_throughput
- 위치: `/home/ulalax/project/ulalax/libzmq-ref/build/bin/` 및 `perf/router_bench/build/`

**호환성 분석:**
- zlink의 `benchwithzmq`는 표준 ZMQ API 사용
- libzmq-ref의 벤치마크도 표준 API 사용
- 동일 파라미터 비교 가능 (메시지 크기, transport, 패턴)

### 4. 프로파일링 도구 ⚠️

**perf:**
- 상태: ❌ 설치 필요
- 경고: WSL2 커널 전용 패키지 필요
  ```
  linux-tools-6.6.87.2-microsoft-standard-WSL2
  또는
  linux-tools-standard-WSL2
  ```

**strace:**
- 상태: ❌ 설치 필요

**설치 명령어 (수동 실행 필요):**
```bash
sudo apt-get update
sudo apt-get install -y strace linux-tools-common linux-tools-generic
```

**대안:**
- WSL2 전용 linux-tools가 설치 안 되면:
  ```bash
  sudo apt-get install -y linux-tools-standard-WSL2
  ```

**Stage 2 프로파일링 전 필수:**
- perf 및 strace 설치 완료 필요
- 현재는 Stage 1 (구현 비교)부터 진행 가능

### 5. 산출물 폴더 구조 ✅

```
docs/team/20260115_router-performance-optimization/output/
├── bench/       # 벤치마크 결과 (생성 예정)
├── profiles/    # 프로파일링 결과 (생성 예정)
└── analysis/    # 분석 문서 (현재 문서)
```

### 6. Git 기준점 설정

**현재 브랜치:**
```
feature/performance-optimization
```

**기준 commit (최적화 시작 전):**
```
커밋 태그 생성 필요:
git tag -a perf-baseline-v0 -m "Performance optimization baseline"
```

## Stage 0 완료 상태

| 항목 | 상태 | 비고 |
|------|------|------|
| 산출물 폴더 생성 | ✅ | output/{bench,profiles,analysis} |
| libzmq-ref 경로 확인 | ✅ | /home/ulalax/project/ulalax/libzmq-ref |
| 파일시스템 통일 | ✅ | 둘 다 ext4 (복사 불필요) |
| libzmq-ref 빌드 | ✅ | 이미 완료 |
| 벤치마크 도구 확인 | ✅ | router_bench 포함 |
| perf 설치 | ⚠️ | **사용자 수동 설치 필요** |
| strace 설치 | ⚠️ | **사용자 수동 설치 필요** |
| Git 기준점 설정 | ⏳ | 태그 생성 권장 |

## 다음 단계

**Stage 1 진행 가능:**
- ROUTER 구현 비교 분석은 프로파일링 도구 없이 진행 가능
- 소스 코드 레벨 분석 (데이터 구조, 알고리즘, 호출 경로)

**Stage 2 진행 전 필수:**
- perf 및 strace 설치 완료 필요

## 권장 사항

**1. Git 기준점 태그 생성:**
```bash
git tag -a perf-baseline-v0 -m "Performance optimization baseline before Stage 1"
git push origin perf-baseline-v0
```

**2. 프로파일링 도구 설치 (Stage 2 전):**
```bash
sudo apt-get update
sudo apt-get install -y strace linux-tools-standard-WSL2
```

**3. Stage 1 시작:**
- ROUTER 구현 소스 코드 비교
- 데이터 구조 및 알고리즘 차이 분석
- 주요 차이점 문서화
