# SPOT Inproc PUB/SUB 전환 비교 계획

> **우선순위**: High  
> **상태**: Draft (실행 전)  
> **버전**: 1.0  
> **작성일**: 2026-02-11

## 1. 목적

현재 SPOT 내부 로컬 전달(`spot_node -> spot_sub`)은 queue dispatch(`matches + deque + cv`) 구조다.  
이를 `inproc PUB/SUB` 구조로 전환했을 때의 효과를 **동일 벤치마크 기준**으로 정량 비교하고, 채택 여부를 결정한다.

핵심은 다음 3가지다.

- fan-out 시 처리량 개선 여부
- 지연 및 CPU/메모리 효율 개선 여부
- 구현 복잡도 증가 대비 운영 가치

## 2. 현재 기준선(Baseline Snapshot)

아래 수치는 전환 전(queue dispatch) 기준의 최근 측정값이다.

### 2.1 MMORPG scale (100x100, payload=4B, global inflight sweep)

- 단일 최고(`throughput_delivery_per_sec`): `19531.935` (`inflight=4096`)
- 반복 안정성(5회 median) 상위: `inflight=256`에서 `18173.357`
- CPU: 대부분 `~99%~101%`
- RSS end: 대체로 `~39MB`

참고 파일:

- `doc/plan/baseline/spot_scale_maxscan.txt`
- `doc/plan/baseline/spot_scale_maxscan_top_repeats.txt`

### 2.2 payload sweep (100x100, inflight=256)

- 64B: `18473.405 delivery/s`, RSS end `40640 KB`
- 512B: `15791.614 delivery/s`, RSS end `45440 KB`
- 1024B: `17411.756 delivery/s`, RSS end `49920 KB`
- 64KB: `13159.547 delivery/s`, RSS end `680320 KB`
- 128KB: `13614.244 delivery/s`, RSS end `1320160 KB`

참고 파일:

- `doc/plan/baseline/spot_scale_payload_sweep_inflight256.txt`

### 2.3 micro SPOT (tcp, 64B, 1:1)

- current: `~2.10M msg/s`, latency `~1083 us`
- baseline: `~2.11M msg/s`, latency `~1081 us`

## 3. 목표/비목표

### 3.1 목표

- 내부 로컬 전달 경로를 `inproc PUB/SUB`로 구현
- 기존 public API와 테스트 시나리오 최대한 유지
- 기존 벤치 프로토콜 그대로 재실행 가능 상태 확보
- 채택/보류를 수치 기반으로 결정

### 3.2 비목표

- discovery/registry 프로토콜 변경
- 외부 네트워크 transport 동작 의미 변경
- 초기 단계에서 모든 최적화 동시 적용(단계적으로 수행)

## 4. 설계 원칙

- 비교 가능성 우선: 전환 전/후 동일 워크로드, 동일 환경, 동일 수집 포맷 유지
- 점진 전환: feature flag 또는 빌드 플래그로 queue/inproc 전환 가능 상태 유지
- 실패 복구 용이성: 즉시 queue 모드로 되돌릴 수 있는 구조 유지

## 5. 구현 단계 계획

### 5.1 단계 0: 비교 인프라 고정

- 현재 결과 포맷(`SPOT_SCALE_RESULT`, `SPOT_SCALE_SUMMARY`) 유지
- 결과 파일 출력 기본 경로 `tmp/spot_scale_result.txt` 유지
- 벤치 실행 스크립트와 파싱 스크립트(간단 python) 고정
- 기준선 로그는 `tmp/`에서 생성 후 `doc/plan/baseline/`로 스냅샷 복사/고정

산출물:

- 기준선 결과 파일 스냅샷 보존(`doc/plan/baseline/*.txt`)

### 5.2 단계 1: 전환 스위치 도입

- 내부 전달 모드 스위치 추가
  - 예: `ZLINK_SPOT_LOCAL_DISPATCH_MODE=queue|inproc`
  - 기본값은 기존 `queue`
- 테스트/벤치에서 모드 명시 실행 가능하도록 반영

산출물:

- 모드별 동일 테스트 통과

### 5.3 단계 2: inproc 로컬 버스 추가

- `spot_node` 내부 로컬 PUB 소켓 생성
- `spot_sub` 생성 시 로컬 SUB 소켓 연결(`inproc://...`)
- 로컬 전달 시 queue enqueue 대신 로컬 PUB로 publish
- topic frame + multipart payload 전달 규약 고정

검토 포인트:

- socket lifetime(생성/종료 순서)
- worker thread affinity와 socket thread-safety

### 5.4 단계 3: 토픽 동기화 전략 적용

- `spot_sub_subscribe/unsubscribe` 호출 시
  - 로컬 SUB 소켓 필터 반영
  - 기존 원격 수신용 `_filter_refcount`도 유지/동기화
- 패턴(`prefix*`) 정책과 기존 동작 일치 검증

구현 방침(1차):

- 내부 로컬 버스는 `PUB(inproc) -> SUB(inproc)` 구조를 사용한다.
- 각 `spot_sub`는 자신의 로컬 `SUB` 소켓에 `SUBSCRIBE/UNSUBSCRIBE`를 직접 적용한다.
- `spot_node`는 기존처럼 `_filter_refcount`를 관리해 원격 peer 수신용 `SUB` 필터를 유지한다.
- 즉, 필터는 아래처럼 이중 동기화된다.
  - 로컬 필터: 각 `spot_sub` 로컬 SUB 소켓
  - 원격 필터: `spot_node`의 `_filter_refcount` 기반 외부 SUB 소켓

`XPUB/XSUB` 채택 여부:

- 1차 전환에서는 **사용하지 않는다**.
- 이유:
  - 구현 복잡도(구독 이벤트 해석/동기화 타이밍) 증가
  - 1차 목표는 구조 전환 효과 검증이며, 비교 단순성이 더 중요
- 2차 최적화 후보로 유지:
  - `XPUB` 구독 이벤트를 이용한 `_filter_refcount` 자동 동기화
  - 구독 churn가 매우 큰 워크로드에서만 도입 검토

### 5.5 단계 4: 큐 경로 제거 또는 유지 결정

- 1차는 dual-path 유지(비교/rollback 목적)
- 성능/안정성 확인 후 queue 경로 축소 또는 제거 결정

### 5.6 단계 5: 안정화

- soak/stress 테스트
- 메모리 누수 및 소켓 종료 시나리오 점검
- 문서 업데이트

## 6. 벤치마크 계획 (전환 전/후 동일)

### 6.1 공통 조건

- 동일 장비/부하 상태에서 실행
- 백그라운드 프로세스 최소화
- 각 항목 최소 5회 반복, median 기준 비교
- transport 원칙:
  - Linux/macOS: 프로세스 간 로컬 transport는 `ipc`
  - Windows: 프로세스 간 로컬 transport도 `ipc`
  - 프로세스 내부 전달 경로는 플랫폼 공통으로 `inproc`

### 6.2 시나리오 A: micro SPOT

- 패턴: `SPOT`
- transport: `tcp`
- payload: `64B`, `256B`, `1024B`
- 지표: throughput, latency

목적:

- 기본 경로 성능 회귀 여부 확인

### 6.3 시나리오 B: MMORPG scale

- `ZLINK_SPOT_RUN_MMORPG_SCALE=1`
- `field=100x100`
- `inflight sweep`: `1,4,16,64,256,1024,4096,10000`
- `payload`: `4B,64B,512B,1024B,64KB,128KB`
- 지표:
  - `throughput_delivery_per_sec`
  - `throughput_publish_per_sec`
  - `cpu_*`
  - `rss_*`

목적:

- fan-out/복사/필터링 비용 민감 구간 비교

## 7. 성공 기준(의사결정 게이트)

inproc 모드 채택 조건:

- 기능 정합성: 기존 spot 테스트 전부 통과
- 성능:
  - MMORPG(100x100) `median throughput_delivery_per_sec` 최소 `+15%` 개선
  - micro SPOT throughput 회귀 `-5%` 이내
- 자원:
  - small payload(`<=1KB`): RSS end 증가 `+20%` 이내
  - large payload(`>=64KB`): RSS end 증가 `+35%` 이내
  - 위 기준은 payload별 baseline 대비로 판정
  - CPU 사용률 악화가 성능 개선 대비 과도하지 않을 것
- 운영:
  - 소켓 lifecycle 관련 크래시/교착 0건

보류 조건:

- 성능 개선이 기준 미달
- 안정성 이슈(종료/해제/동시성) 반복 재현
- 메모리 사용 급증으로 운영 리스크 증가

## 8. 리스크 및 대응

- Slow joiner/구독 전파 지연
  - 대응: settle 구간/재시도 정책 명시
- HWM/백프레셔 정책 미정
  - 대응: 기본값 고정 후 단계적 튜닝
- 모드 이원화로 복잡도 증가
  - 대응: 비교 종료 후 단일 경로로 수렴 결정
- 토픽 필터 이중 동기화 불일치(로컬 SUB vs `_filter_refcount`)
  - 대응: subscribe/unsubscribe 경로 단일화, 불일치 검증 테스트 추가
- `XPUB/XSUB` 미도입으로 구독 이벤트 자동화 부재
  - 대응: 1차는 명시 동기화로 고정, 필요 시 2차에 별도 실험 브랜치 도입

## 9. 실행 순서(권장)

1. 단계 0~1 완료 (스위치 + 회귀 통과)  
2. 단계 2~3 구현 (inproc 경로 완성)  
3. 시나리오 A/B 전체 벤치 1차 실행  
4. 상위 구간 반복 5회로 median 확정  
5. 성공 기준으로 채택/보류 결정  
6. 채택 시 단계 4~5 진행(정리/문서화)

## 10. 산출물

- 코드 변경(PR)
- 벤치 원본 로그(`tmp/*.txt`) + 기준선 스냅샷(`doc/plan/baseline/*.txt`)
- 비교 요약표(전환 전/후)
- 최종 결정 메모(채택/보류 + 근거)
