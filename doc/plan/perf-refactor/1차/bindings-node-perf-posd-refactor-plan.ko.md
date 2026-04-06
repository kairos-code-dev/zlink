# `bindings/node/perf` POSD 리팩토링 계획

> POSD 리뷰 등급: **B** — metrics 추상화 양호하나 parseArgs 중복과 패턴 보일러플레이트
> 대상: `bindings/node/perf/`

## 1. 목표

Node perf 코드가 아래 성질을 구조적으로 만족하도록 만든다.

- parseArgs가 1곳에서만 정의된다.
- single 패턴 파일의 소켓 설정 보일러플레이트가 팩토리로 추출된다.
- multi server/client의 반복 구조가 공통 러너로 통합된다.
- `core/perf`와 동일한 측정 의미를 유지한다.

## 2. 현재 문제 요약

### 2.1 parseArgs 11회 복붙

`perf_multi_dealer_dealer_client.ts`, `perf_multi_router_router_client.ts`,
`perf_multi_dealer_router_client.ts`, `perf_multi_pubsub_client.ts`,
`perf_multi_spot_client.ts`, `perf_multi_stream_client.ts` 및 대응 server 파일 등
11개 파일에 24줄짜리 동일한 `parseArgs` 함수가 복사되어 있다.

### 2.2 single 패턴 소켓 설정 반복

- `perf_pair.ts` (13-26줄): ctx 생성 → 소켓 생성 → endpoint → bind/connect
- `perf_pubsub.ts` (12-29줄): 거의 동일
- `perf_dealer_dealer.ts` (12-25줄): 거의 동일

14줄짜리 설정 블록이 6개 패턴 파일에서 반복.

### 2.3 multi server recv 루프 중복

- `perf_multi_dealer_dealer_server.ts` (87줄): poller.poll() 기반 recv → echo
- `perf_multi_pubsub_server.ts` (89줄): 거의 동일한 구조
- `perf_multi_router_router_server.ts`: 유사 구조

recv echo 루프가 서버 파일마다 반복.

### 2.4 multi client send 루프 중복

- `perf_multi_dealer_dealer_client.ts` (78줄)
- `perf_multi_dealer_router_client.ts` (100줄)
- `perf_multi_router_router_client.ts` (103줄)

send loop + retry + metrics 수집이 60%+ 유사.

### 2.5 perf_multi_common.ts 혼재

`perf_multi_common.ts` (309줄)이 프로세스 스폰, 라인 수집, 포트 예약,
SPOT 재시도를 모두 담고 있다. 테스트 오케스트레이션과 metrics 수집이 분리되지 않음.

## 3. 설계 원칙

- `core/perf`와 동일한 측정 의미 절대 우선
- TypeScript/Node 관용 스타일 유지
- 공통화는 변경 증폭 축소가 목적
- Worker thread 기반 metrics 구조는 유지 (perf_metrics.ts 양호)
- **inline 코드 요구사항** (`PERF_POLICY.md` 715-727줄):
  패턴 파일에서 send/recv API 호출, EAGAIN 처리, 모델 선택, ready gate,
  소켓 생성이 직접 보여야 한다. 설정/TLS/결과 출력만 공통화 가능.
- **매 단계 완료 시**:
  1. build + 테스트 통과
  2. smoke perf 실행: 정상 종료, RESULT line 정책 형식 출력, 결과 파일 생성 확인
     (수치 비교는 하지 않음 — 병렬 작업으로 측정값 왜곡 가능)
  3. hot-path에 새 lock/alloc/log 없음 확인
  4. full comparable run + 수치 비교는 **전체 리팩토링 완료 후 순차 실행**

## 4. 단계별 실행 계획

### 단계 0. 현황 동결

할 일:
- parseArgs 중복 위치 전수 목록 (11개 파일)
- single 패턴 간 diff 매핑
- multi server/client 간 diff 매핑
- `doc/perf/PERF_POLICY.md` recv/callback 모드 매트릭스 감사:
  - single: callback-only (전 패턴, 예외 없음)
  - multi: recv-only (SPOT/STREAM만 dual-mode)
  - 미지원 조합이 fail-fast하는지 확인
- STREAM 공유 클라이언트 경로 현황 확인
- direct native API 사용 현황 (N-API 직접 호출 grep)
- `core/perf` 대비 semantic parity 체크리스트:
  - throughput/bandwidth/latency 정의 일치
  - phase 구조 (ready → warmup → active)
  - RESULT line 형식
  - recv_mode / direction 일치

완료 기준:
- 통합 대상이 함수 단위로 정리됨
- PERF_POLICY 위반 목록이 작성됨

### 단계 1. parseArgs 통합

할 일:
- `perf_multi_common.ts`에 통합 parseArgs 추가:

```typescript
interface MultiArgs {
    endpoint: string;
    transport: string;
    msgSize: number;
    duration: number;
    warmup: number;
    clients: number;
    recvMode: string;
}

function parseMultiArgs(): MultiArgs
```

- 6개 파일에서 로컬 parseArgs 삭제, import로 교체

완료 기준:
- parseArgs 정의 1곳
- 인자 추가 시 수정 포인트 1곳

### 단계 2. single 측정 인프라 공통화 (측정 로직은 inline 유지)

할 일:
- `runSingleBench` 같은 통합 팩토리는 만들지 않음
- 공통화 범위: metric header encode/decode, RESULT 출력, TLS 설정
- **소켓 생성, endpoint, collector 연결, send API 호출은
  각 패턴 파일에 inline 유지** (`PERF_POLICY.md` 715-727줄)

완료 기준:
- metric/RESULT 인프라가 공통 모듈
- 소켓 생성/collector/send API는 패턴 파일에서 직접 보임

### 단계 3. multi 측정 인프라 공통화

할 일:
- multi 공통 러너는 만들지 않음
- 공통화 범위: orchestrator, RESULT 포맷, metric helper, parseArgs
- **server/client의 recv loop, send loop, EAGAIN 처리, 모델 선택은
  각 패턴 파일에 inline 유지** (`PERF_POLICY.md` 715-727줄)
- `perf_multi_common.ts`의 SPOT retry 코드 삭제 (정책: retry 금지)

완료 기준:
- parseArgs/RESULT/metric 인프라가 공통 모듈
- recv/send loop, EAGAIN은 패턴 파일에서 직접 보임
- retry 코드 0건

### 단계 4. perf_multi_common.ts 책임 분리

할 일:
- 프로세스 오케스트레이션 (`spawnMultiPair` 등) → `perf_multi_orchestrator.ts`
- metrics 수집/집계 → 기존 `perf_metrics.ts`에 유지
- 포트 예약/네트워크 유틸 → `perf_multi_common.ts`에 유지

완료 기준:
- 각 파일의 책임이 명확히 분리됨
- 309줄 단일 파일 → 2-3개 100줄 이하 파일

### 단계 5. 검증

할 일:
- `npm run build` / `tsc` 확인
- single/multi 전 패턴 smoke
- `core/perf` 대비 semantic parity 확인
- 결과 파일 `bindings/node/perf/results/` 확인

완료 기준:
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지

## 5. 완료 정의

- parseArgs 정의 1곳
- metric/RESULT 인프라가 공통 모듈로 통합됨
- send/recv loop, EAGAIN 처리, 모델 선택은 각 패턴 파일에서 직접 보임
- perf_multi_common.ts 책임이 분리됨
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지
- `doc/perf/PERF_POLICY.md` recv/callback 모드 제약이 코드에서 강제됨
- 미지원 recv 모드 조합이 fail-fast
- direct native API 호출 0건 (Node binding API만 사용)

---

## 완료 상태

> 완료일: 2026-04-06
> 검증: 빌드 통과 + 스모크 테스트 통과 + 완료 정의 대비 코드 리뷰 통과

단계 완료. send/recv 6파일 인라인화와 `perf_single_common` 정리가 반영되었고, 대형 메시지 0.00 이슈를 해소한 최신 스모크에서 `status=complete`와 180 RESULT lines를 확인했다.
