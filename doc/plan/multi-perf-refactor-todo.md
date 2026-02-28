# Multi Perf 리팩토링 작업 목록

> 원본 계획: `doc/plan/multi-perf-refactor-echo-oneway-plan.md`
> 각 Phase 완료 후 빌드 검증. 실패 시 해당 Phase 내에서 수정.

---

## Phase 1: Send/Recv 정책 통일 + DEALER_DEALER One-way 전환

범위: 전 패턴 (9개)

- [x] 1-1. `ZLINK_DONTWAIT` 송신 경로를 blocking send로 전환 (전 패턴)
- [x] 1-2. `bench_send_flags()`, `classify_send_result()` EAGAIN 분기 단순화
- [x] 1-3. 환경변수 제거: `PERF_MULTI_BLOCKING_SEND`, `PERF_MULTI_SEND_BACKOFF_US`, `PERF_MULTI_CLIENT_IDLE_SLEEP_US`
- [x] 1-4. 서버 `publish_once()`의 `ZLINK_DONTWAIT` → blocking send 전환
- [x] 1-5. recv 경로가 §3.1 정책에 맞는지 확인, 불일치 시 수정
  - `DEALER_DEALER`: poll + recv + batch drain
  - 나머지: poll + recv + batch drain
- [x] 1-6. pubsub `nodrop` 기본 적용 확인
- [x] 1-7. `MULTI_DEALER_DEALER`: 서버 `relay_dealer_once()` 제거, one-way(서버→클라이언트 발행) 구조로 전환
- [x] 1-8. `run_comparison.py`의 `MULTI_ECHO_PATTERNS`에서 `MULTI_DEALER_DEALER` 제거
- [x] **검증**: 전 패턴 빌드 성공

---

## Phase 2: 메시지 크기 버그 수정

범위: 전 패턴

- [x] 2-1. 전송 길이가 `current_msg_size`를 사용하는지 전수 확인
- [x] 2-2. `max_size`가 capacity로만 사용되는지 확인
- [x] 2-3. 수정 후 size별 throughput 곡선 차이 스모크 확인 (64B vs 1024B)
- [x] **검증**: 빌드 성공 + 64B vs 1024B throughput 차이 확인

---

## Phase 3: GATEWAY Echo 전환

범위: `MULTI_GATEWAY` 서버/클라이언트

- [x] 3-1. 서버 구조 변경: receiver(recv) + gateway(send) 쌍 구성
  - Registry/Discovery 인프라 셋업
  - receiver bind → Registry 등록
  - Discovery + Gateway 생성 (클라이언트 서비스 구독)
- [x] 3-2. 클라이언트 구조 변경: gateway(send) + receiver(recv) 쌍 구성
  - receiver bind → Registry 등록
  - Discovery + Gateway 생성 (서버 서비스 구독)
  - `wait_for_gateway()` 연결 대기
- [x] 3-3. 서비스 네이밍 구현 (`perf-server`, `perf-client-{N}`)
- [x] 3-4. `run_comparison.py`의 `MULTI_ECHO_PATTERNS`에 `MULTI_GATEWAY` 추가
- [x] 3-5. throughput/bandwidth 계산이 Echo 공식(×2)으로 전환 확인
- [x] **검증**: GATEWAY 단독 빌드 + 스모크 (tcp, 64B)

---

## Phase 4: throughput 집계 통일

범위: 전 패턴

- [x] 4-1. 모든 패턴의 throughput 계산이 `sum(worker recv_count) / duration` 공식인지 확인
- [x] 4-2. worker별 평균을 쓰는 경로가 있으면 수정
- [x] 4-3. Echo/One-way 단위(ops/s vs msg/s) 표기 확인
- [x] **검증**: 빌드 성공

---

## Phase 5: inflight/미사용 코드 정리

범위: 전 파일

- [x] 5-1. inflight 관련 코드/환경변수/문서 제거 (§7.1)
- [x] 5-2. 제거 대상 환경변수 목록 정리 (§7.2)
  - `PERF_MULTI_BLOCKING_SEND`
  - `PERF_MULTI_SEND_BACKOFF_US`
  - `PERF_MULTI_CLIENT_IDLE_SLEEP_US`
  - `PERF_PROFILE` 기반 blocking 분기
- [x] 5-3. 미사용 파일 삭제 (§7.3) — CMake/runner 미참조 기준
- [x] **검증**: 빌드 성공 + 제거 대상 환경변수 grep 결과 0건

---

## Phase 6: 문서/러너 동기화

범위: 문서 + 스크립트

- [x] 6-1. `PERF_MULTI_TEST_POLICY.md` 갱신
  - 패턴 분류표 (§8.1): DEALER_DEALER → one-way, GATEWAY → Echo
  - 메트릭/latency 규칙 (§8.3, §9.1, §9.3): latency divisor, throughput 단위 갱신
  - 환경변수 목록 (§12.4): 제거된 변수 반영
  - 표준 메시지 크기 (§11.2): STREAM* msg size 분리 반영
- [x] 6-2. `run_comparison.py` Echo/One-way 분류 최종 갱신
- [x] 6-3. 멀티 벤치 가이드 문서 갱신
- [x] 6-4. AGENTS.md stub 파일 금지 원칙 문구 확인 (§4.5 반영)
- [x] **검증**: 문서 내 분류/환경변수/메트릭 정의가 코드와 일치

---

## Phase 7: 전체 스모크 + 성능 검증

범위: 전 패턴

- [x] 7-1. §9.2 검증 매트릭스 전체 실행
  - 러너: `core/perf/run_benchmarks_multi.sh` (메인)
  - transport: tcp, tls, ws, wss
  - msg sizes (기본): 64, 256, 1024, 65536, 131072, 262144
  - msg sizes (STREAM* 3종): 64, 256, 1024, 65536
  - 실행 순서: GATEWAY 단독 → 전체 패턴
- [x] 7-2. 결과 리포트 생성
- [x] **검증**: §9 완료 기준 전항목 통과

---

## 완료 기준

- [x] 전 패턴에 recv/send/pubsub 정책 일관 적용
- [x] `MULTI_GATEWAY`가 gateway/receiver Echo 모델로 동작
- [x] `MULTI_SPOT`은 one-way 유지
- [x] throughput이 전체 클라이언트 집계 기준 계산
- [x] inflight 관련 흔적이 코드/문서에 없음
- [x] hwm/sndhwm/rcvhwm 정책 변경 없음
- [x] 스모크 결과 메트릭/산출물 정상 생성
- [x] 미사용 파일 정리 완료
- [x] stub 파일(`#include` 한 줄 위임) 없음
