# STREAM 5-Stack Parity, 성능 확정, 릴리스/후속 계획

- 작성일: 2026-02-13
- 현재 기준 결정:
  - 5개 스택(`zlink / asio / cppserver / dotnet / net-zlink`)은 **동일 시나리오(`s0/s1/s2`)**로 맞춘다.
  - 지금은 **성능 기준선 확정**을 먼저 완료한다.
  - `net-zlink` 성능 개선은 **zlink 1.2 배포 + 라이브러리 1.2 갱신 이후**에 진행한다.
  - 이번 릴리스 단계에서는 **bindings 배포하지 않음**, **NuGet 배포하지 않음**.

## 1. 현재 완료 상태 (2026-02-13)

완료된 항목:

- 5-stack 통합 실행 스크립트 완성
  - `core/tests/scenario/stream/run_stream_compare.sh`
- 각 스택별 `s0/s1/s2` 러너 완성
  - `core/tests/scenario/stream/zlink/run_stream_scenarios.sh`
  - `core/tests/scenario/stream/asio/run_stream_scenarios.sh`
  - `core/tests/scenario/stream/dotnet/run_stream_scenarios.sh`
  - `core/tests/scenario/stream/cppserver/run_stream_scenarios.sh`
  - `core/tests/scenario/stream/net-zlink/run_stream_scenarios.sh`
- `s0` 의미를 5개 스택에서 동일화
  - `s0`: 단일 연결(`ccu=1`, `inflight=1`) 1:1 echo 정합
- 공통 출력 규격 통일
  - `RESULT`, `METRIC`, `metrics.csv` 동일 컬럼
- 공통 파라미터 입력 정렬
  - `ccu`, `size`, `inflight`, `warmup`, `measure`, `drain-timeout`, `connect-concurrency`, `backlog`, `hwm`
- `cppserver`는 `upstream`이 없을 때 자동 clone 지원
  - `CPPSERVER_UPSTREAM_URL`, `CPPSERVER_UPSTREAM_REF` 환경변수 지원

## 2. 확정 벤치 프로파일 (고정)

- `ccu=10000`
- `size=1024`
- `inflight=30` (클라이언트 기준)
- `warmup=3`
- `measure=10`
- `drain-timeout=10`
- `connect-concurrency=256`
- `backlog=32768`
- `hwm=1000000`
- `io_threads=32`
- `latency_sample_rate=16`

## 3. 성능 기준선(현재 확정값)

실행:

- 결과 루트:
  - `core/tests/scenario/stream/result/perf5_final_20260213_152949`
- CSV:
  - `core/tests/scenario/stream/result/perf5_final_20260213_152949/metrics.csv`
- 요약:
  - `core/tests/scenario/stream/result/perf5_final_20260213_152949/summary.json`

`S2 throughput (msg/s)`:

- `cppserver-s2`: `3,753,486.60` (`PASS`)
- `asio-s2`: `1,993,289.70` (`PASS`)
- `dotnet-s2`: `1,689,780.60` (`PASS`)
- `zlink-s2`: `181,310.70` (`PASS`)
- `net-zlink-s2`: `9,532.20` (`FAIL`, `drain_timeout=1`)

`S1 connect-only`:

- 5개 스택 모두 `connect_success=10000`, `connect_fail=0`, `PASS`

해석:

- 현재 시점에서 5-stack 동일 시나리오 기준선은 확보됨.
- `net-zlink`는 기능적으로 시나리오 정합은 맞지만 `s2` 성능/드레인 안정성 미달.
- 이 이슈는 릴리스 이후 `net-zlink 1.2` 개선 단계에서 처리한다.

## 4. 릴리스 범위(이번 사이클)

이번 사이클 포함:

- 코어 zlink 버전 `1.2.0` 반영
- 코어 빌드/테스트/시나리오 검증
- 코어 릴리스 태그/배포 확인

이번 사이클 제외:

- bindings 배포
- NuGet 배포
- `net-zlink` 성능 개선/최적화

## 5. 다음 작업 순서 (중요)

### 5.1 Phase R1: zlink 1.2 반영 + 빌드/테스트

1. 버전 업데이트
   - `VERSION`
   - 필요 시 `CHANGELOG.md`
2. 빌드/테스트
   - `ctest --test-dir /home/hep7/project/kairos/zlink/core/build --output-on-failure`
3. stream 시나리오 빠른 확인
   - 5-stack 재실행은 필요 시 1회 smoke + 기준선 비교

### 5.2 Phase R2: 코어 릴리스 (bindings 제외)

태그 정책:

- 필수 배포 태그: `core/v1.2.0`
- alias 태그: `v1.2`

실행 예시:

```bash
git checkout -b release/version-1.2.0 origin/main
git add VERSION CHANGELOG.md
git commit -m "release: bump core version to 1.2.0"
git push origin release/version-1.2.0
gh pr create --base main --head release/version-1.2.0 --title "release: version 1.2.0" --body "core 1.2.0"
gh pr merge --squash --delete-branch

git checkout main
git pull origin main
git tag -a core/v1.2.0 -m "core release 1.2.0"
git tag -a v1.2 -m "alias tag for 1.2.0"
git push origin core/v1.2.0
git push origin v1.2
```

배포 모니터링:

```bash
TAG=core/v1.2.0
build_run_id=$(gh run list --workflow build.yml --event push --limit 100 --json databaseId,displayTitle,headBranch | jq -r --arg t "$TAG" '.[] | select((.headBranch == $t) or (.displayTitle | contains($t))) | .databaseId' | head -n1)
conan_run_id=$(gh run list --workflow core-conan-release.yml --event push --limit 100 --json databaseId,displayTitle,headBranch | jq -r --arg t "$TAG" '.[] | select((.headBranch == $t) or (.displayTitle | contains($t))) | .databaseId' | head -n1)

gh run watch "$build_run_id" --exit-status
gh run watch "$conan_run_id" --exit-status
```

### 5.3 Phase R3: 라이브러리 1.2 갱신 완료 확인

- 코어 릴리스 완료 및 산출물 확인
- 작업 컨텍스트 리셋 후 후속 단계 시작

### 5.4 Phase R4: net-zlink 1.2 개선 단계 (릴리스 이후)

개선 목표:

- `net-zlink-s2` 안정성(`drain_timeout=0`) 회복
- throughput 상승

검증:

- 동일 5-stack 시나리오/동일 파라미터 재측정
- 결과를 새 timestamp 디렉터리에 저장
- 기존 기준선(`perf5_final_20260213_152949`) 대비 비교표 생성

## 6. 컨텍스트 리셋 후 바로 시작할 명령

사전 점검:

```bash
cd /home/hep7/project/kairos/zlink
git pull --ff-only
```

5-stack 기준선 재실행(필요 시):

```bash
TIMESTAMP=manual_recheck_$(date +%Y%m%d_%H%M%S) \
CCU=10000 INFLIGHT=30 WARMUP=3 MEASURE=10 DRAIN_TIMEOUT=10 \
CONNECT_CONCURRENCY=256 BACKLOG=32768 HWM=1000000 IO_THREADS=32 LATENCY_SAMPLE_RATE=16 \
RUN_DOTNET=1 RUN_CPPSERVER=1 RUN_NET_ZLINK=1 \
/home/hep7/project/kairos/zlink/core/tests/scenario/stream/run_stream_compare.sh
```

## 7. 참고 경로

- 통합 실행: `core/tests/scenario/stream/run_stream_compare.sh`
- zlink: `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`
- asio: `core/tests/scenario/stream/asio/test_scenario_stream_asio.cpp`
- dotnet(tcp): `core/tests/scenario/stream/dotnet/Program.cs`
- cppserver runner: `core/tests/scenario/stream/cppserver/run_stream_scenarios.sh`
- net-zlink: `core/tests/scenario/stream/net-zlink/Program.cs`
- 현재 확정 결과:
  - `core/tests/scenario/stream/result/perf5_final_20260213_152949/metrics.csv`
  - `core/tests/scenario/stream/result/perf5_final_20260213_152949/summary.json`

## 8. 최종 결정 요약

- 5개 시나리오 동일화: 완료
- 성능 기준선 확정: 완료 (`perf5_final_20260213_152949`)
- 이번 단계에서 `net-zlink` 개선: 미수행(의도적 보류)
- 다음 단계 우선순위:
  1. zlink core `1.2.0` 반영
  2. core 빌드/테스트 및 배포 확인
  3. 컨텍스트 리셋
  4. `net-zlink 1.2` 개선 + 5-stack 재측정
