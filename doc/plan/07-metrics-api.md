# 메트릭스 API 스펙 (Metrics API)

> **우선순위**: 7 (Recommended Feature)
> **상태**: Draft
> **버전**: 1.0
> **의존성**:
> - `zmq_socket_stats_t` (기존 소켓 통계)
> - `zmq_socket_peer_info`/`zmq_socket_peers` (피어 통계)

## 목차
1. [개요](#1-개요)
2. [메트릭 모델](#2-메트릭-모델)
3. [C API 명세](#3-c-api-명세)
4. [구현 계획](#4-구현-계획)
5. [검증 방법](#5-검증-방법)
6. [변경 이력](#6-변경-이력)

---

## 1. 개요

### 1.1 배경

운영 환경에서 **송수신량, 큐 적체, 드롭 발생**을 빠르게 확인할 수
있는 API가 필요하다. 현재는 소켓 내부 카운터가 존재하지만,
명시적인 정책/테스트가 부족하다.

### 1.2 목표

- **안정된 메트릭 API**: C API로 표준화
- **낮은 오버헤드**: 메트릭 수집이 성능에 영향 최소
- **스냅샷 방식**: 호출 시점 기준의 일관된 수치 제공
- **확장 가능성**: 추후 컨텍스트/프로세스 수준으로 확장

---

## 2. 메트릭 모델

### 2.1 소켓 단위 (기본)

`zmq_socket_stats_t` 기반:

- `msgs_sent`, `msgs_received`
- `bytes_sent`, `bytes_received`
- `msgs_dropped`
- `monitor_events_dropped`
- `queue_size` (outbound 큐 총합)
- `hwm_reached` (HWM 이벤트 총합)
- `peer_count`

추가 권장(1차 범위):

- `queue_outbound` / `queue_inbound` 분리
- `drops_hwm`, `drops_no_peers`, `drops_filter` (드롭 원인별 카운터)
- `last_send_ms`, `last_recv_ms` (최근 송수신 시간, monotonic 기준)

> 참고: `queue_inbound`는 큐에 적재된 **프레임 단위** 카운트를 반환한다.
> multipart 메시지는 프레임 수만큼 증가할 수 있다.

### 2.2 피어 단위 (선택)

`zmq_socket_peer_info` 기반:

- `routing_id`, `remote_addr`, `connected_time`
- `msgs_sent`, `msgs_received`

### 2.3 확장(옵션)

- 컨텍스트/프로세스 전체 통계
- 전송(transport)별 카운터

---

## 3. C API 명세

### 3.1 현행 API (안정화/문서화)

- `int zmq_socket_stats(void *socket, zmq_socket_stats_t *stats)`
- `int zmq_socket_peer_info(void *socket, const zmq_routing_id_t *rid, zmq_peer_info_t *info)`
- `int zmq_socket_peers(void *socket, zmq_peer_info_t *peers, size_t *count)`

### 3.2 신규 API (1차)

- `int zmq_socket_stats_ex(void *socket, zmq_socket_stats_ex_t *stats)`
  - `queue_outbound`, `queue_inbound` 분리 제공
  - `drops_hwm`, `drops_no_peers`, `drops_filter`
  - `last_send_ms`, `last_recv_ms` (monotonic 기준)

### 3.3 확장 API (옵션)

- `int zmq_socket_stats_reset(void *socket)`
  - 누적 카운터를 0으로 초기화
- `int zmq_context_stats(void *ctx, zmq_context_stats_t *stats)`
  - 컨텍스트 전체 집계 (옵션)

---

## 4. 구현 계획

### 4.1 1차: 소켓 메트릭 확장/안정화

1) `zmq_socket_stats_ex` 추가
- `queue_outbound/inbound`, 드롭 원인, 마지막 송수신 시각 제공

2) 스냅샷 일관성
- 수집 시점의 내부 카운터와 큐 상태를 안정적으로 스냅샷

3) 테스트 추가
- 송수신/드롭 카운터 증가 검증
- 큐 적체 시 `queue_outbound`, `hwm_reached` 증가 확인

### 4.2 2차: 피어 메트릭 정합성 강화

- `zmq_socket_peers` 반환 순서/수량 보장 규칙 정의
- 라우팅 ID 기반 조회 오류 코드 정리

### 4.3 3차: 확장 API (옵션)

- `zmq_socket_stats_reset` 추가
- 컨텍스트/프로세스 레벨 집계 구조 설계

---

## 5. 검증 방법

- 기능 테스트: 통계 값 증가/감소 동작 확인
- 멀티스레드: thread-safe 소켓에서도 동일 동작 확인
- 성능 테스트: 통계 호출 오버헤드 측정

---

## 6. 변경 이력

- 2026-01-29: 초안 작성
