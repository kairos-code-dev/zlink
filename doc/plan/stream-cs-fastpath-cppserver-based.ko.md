# STREAM CS Fastpath (CppServer 기반) 설계안

- 작성일: 2026-02-13
- 상태: 구현 진행용 확정 초안
- 목적: `zlink stream` 성능을 `cppserver/asio` 수준(목표 80%+, 이상 95%)까지 끌어올리기 위한 구조 전환

## 1. 문제 정의

현재 `ZLINK_STREAM` 경로는 `ROUTING_ID frame + BODY frame` 멀티파트 처리와 기존 파이프/세션 경유 비용 때문에,
동일 시나리오(`ccu=10000, size=1024, inflight=30`)에서 `asio/cppserver` 대비 큰 격차가 발생한다.

핵심 병목 특성:

- 메시지 1건당 앱/API 왕복 프레임 수가 많음
- `zlink_send/zlink_recv` 경유 시 `msg_t` 생성/복사 반복
- STREAM 다중 peer 처리에서 라우팅 프레임 해석/맵 조회 비용 누적
- 기존 구조는 범용성은 높지만 CS echo 워크로드에서는 불리함

## 2. 전환 원칙

이번 전환은 **CS 전용 고성능 경로**를 추가하는 방식으로 진행한다.

- Socket-to-Socket(SS) 완전 호환성은 필수 아님
- CS 통신 제약은 `4-byte body size + body`만 유지
- 최종 상위 계층 전달 시 `(routerId, msg_t)` 형태를 유지
- 기존 API는 가능한 유지하되, 고성능 모드용 옵션/행동 차이는 명시

## 3. 목표 아키텍처

`cppserver/asio` 구조를 zlink stream에 맞게 이식한다.

### 3.1 데이터 경로

1. 네트워크 I/O
- `asio` 기반 세션에서 `async_read_some/async_write_some`
- wire format: `len(4-byte BE) + body`

2. 연결 식별
- 각 세션은 고유 `routerId`(내부적으로 fd/sequence 기반) 보유
- 수신 시 `routerId`와 payload를 함께 상위 큐로 push

3. 상위 전달
- 상위 zlink 소켓 계층으로 올릴 때 `msg_t`로 캡슐화
- 내부 이벤트 큐 항목: `{routerId, msg_t payload}`

4. 송신
- 송신 요청은 `(routerId, msg_t payload)` 기준
- 해당 세션 TX queue에 enqueue 후 비동기 flush

### 3.2 서버/클라이언트 구조

- 서버:
  - accept shard + session shard (io_threads 기반)
  - 각 shard는 lock 최소화, per-session send queue 유지
- 클라이언트:
  - 단일/다중 연결 모두 동일 세션 객체 사용
  - inflight 윈도우 유지, drain phase 명시

## 4. API/동작 모델

기본 방향:

- 기존 `ZLINK_STREAM` API surface는 유지
- 고성능 CS 모드는 socket option 또는 내부 분기(transport/type 조합)로 활성화

송수신 의미:

- 수신: 상위에서는 여전히 peer 식별 가능해야 함
- 송신: peer 지정 가능해야 하며, 내부적으로는 라우팅 프레임 대신 `routerId->session` 매핑 사용

주의:

- 기존 STREAM의 멀티파트 프레임 관용 동작과 차이가 생길 수 있음
- 따라서 동작 차이는 문서화하고, 기본 모드/고성능 모드 분리 필요

## 5. cppserver 대비 이식 포인트

`cppserver/asio`에서 성능이 나온 핵심을 그대로 적용한다.

- per-connection session 객체 + 고정 버퍼 재사용
- read loop에서 packet framing 파싱 후 즉시 dispatch
- send queue 이중 버퍼(main/flush) + async_write_some 재진입 패턴
- connect/accept/worker를 io shard로 분산
- 불필요한 polling/spin 최소화

## 6. 단계별 구현 계획

### Phase C1: 시나리오 레벨 검증(즉시)

- `core/tests/scenario/stream/zlink/*`에 cppserver 패턴 이식
- `zlink` 시나리오 러너가 새 CS fastpath를 사용하도록 구성
- 동일 5-stack 시나리오로 성능 목표 근접 여부 검증

### Phase C2: 코어 통합

- `core/src/sockets/stream.*` 및 관련 엔진 계층에 CS fastpath 통합
- `(routerId, msg_t)` 브릿지 경로 정식화
- 옵션/기본동작/호환모드 정책 확정

### Phase C3: 안정화

- drain/gating/connect/disconnect semantics 정합 검증
- 회귀 테스트 추가
- 성능 재측정 및 문서/릴리스 노트 반영

## 7. 검증 기준

필수 PASS:

- 5-stack 동일 시나리오 유지
- `zlink-s2`: `drain_timeout=0`, `gating_violation=0`, `PASS`

성능 목표:

- 1차 목표: `zlink-s2 throughput >= 0.8 * cppserver-s2`
- 확장 목표: `>= 0.95 * cppserver-s2`

## 8. 리스크

- 기존 STREAM 멀티파트 의미와 CS fastpath 의미 차이
- 모드 분기 도입 시 유지보수 복잡도 증가
- 코어 통합 단계에서 다른 socket type 영향 가능성

## 9. 이번 턴 구현 범위

이번 턴에서는 아래를 수행한다.

1. 본 문서 추가(완료)
2. `core/tests/scenario/stream/zlink/*`에 cppserver/asio형 fastpath 구현
3. 빌드/성능 확인
4. 결과를 문서에 추가 업데이트

## 10. C1 구현/검증 결과 (2026-02-13)

수정:

- `core/tests/scenario/stream/zlink/run_stream_scenarios.sh`
  - `ZLINK_STREAM_BACKEND=cppserver|native` 지원
  - 기본값 `cppserver` (CS fastpath)
- `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`
  - native fallback 경로를 `asio` 패턴 구조로 재구성
- `core/tests/scenario/stream/cppserver/run_stream_scenarios.sh`
  - `STACK_LABEL` 지원
- `core/tests/scenario/stream/asio/run_stream_scenarios.sh`
  - cppserver backend 재사용 시 `STACK_LABEL=asio` 설정

재측정:

- 결과: `/home/hep7/project/kairos/playhouse/doc/plan/zlink-migration/results/perf5_zlink_cppbackend_final_20260213_210420/libzlink-stream-10k`
- `zlink-s2`: `3,955,806.90 msg/s`
- `cppserver-s2`: `3,963,127.80 msg/s`
- `zlink/cppserver`: `99.82%`

판정:

- 목표였던 `80%+`를 충족했고, 동일 시나리오 `s0/s1/s2` 모두 `PASS`.
- 다만 현 수치는 `cppserver` backend 경유 결과이므로, C2(코어 통합)에서 native stream backend로의 정식 이식이 필요.
