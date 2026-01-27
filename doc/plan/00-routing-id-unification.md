# Routing ID 통합 스펙 (Routing ID Unification)

> **우선순위**: 0 (Foundation - 다른 기능의 선행 작업)
> **상태**: Draft
> **버전**: 1.0

## 목차
1. [개요](#1-개요)
2. [현재 상태 분석](#2-현재-상태-분석)
3. [정리된 정책](#3-정리된-정책)
4. [C API 명세](#4-c-api-명세)
5. [구현 계획](#5-구현-계획)
6. [비호환 변경](#6-비호환-변경)
7. [검증 방법](#7-검증-방법)

---

## 1. 개요

### 1.1 배경

현재 routing_id는 용도와 맥락에 따라 여러 형태로 혼용된다:
- 자동 생성:
  - ROUTER: 5바이트 `[0x00][uint32_t]` 형식
  - STREAM: 4바이트 `uint32_t`
- 사용자 설정: 가변 길이 문자열 (최대 255바이트, `ZMQ_ROUTING_ID`)
- 연결 alias: 가변 길이 문자열 (`ZMQ_CONNECT_ROUTING_ID`)
- 메시지 내부: 4바이트 (`uint32_t`, msg_t 내부 필드)

### 1.2 목표

**호환성 미고려로 자동 생성 포맷을 5바이트로 통일**한다:
- 자동 생성 값은 `uint32_t` 시퀀스 (0 제외)
- 저장 포맷은 **모든 소켓에서 5B `[0x00][uint32]`**
  - STREAM의 자동 생성 포맷도 4B -> 5B로 변경
- `ZMQ_ROUTING_ID`, `ZMQ_CONNECT_ROUTING_ID`는 가변 길이 문자열 유지
- 핸드셰이크 포맷(길이+가변) 유지

즉, **자동 생성 포맷만 5B로 통일**하고 문자열 alias는 그대로 둔다.

### 1.3 STREAM 소켓 정책

STREAM 소켓은 현재 4바이트 routing_id를 사용 중이나,
본 계획에서는 **자동 생성 포맷을 5바이트로 통일**한다.

---

## 2. 현재 상태 분석

### 2.1 소켓별 routing_id 처리

| 소켓 | 자동 생성 | 크기 | 저장 방식 |
|------|----------|------|----------|
| ROUTER | `[0x00][uint32]` | 5B | `blob_t` |
| STREAM | `[uint32]` | 4B | `blob_t` |
| DEALER | 없음 | - | - |
| PUB/SUB | 없음 | - | - |

### 2.2 현재 자료구조

```cpp
// options.hpp
unsigned char routing_id_size;      // 0~255
unsigned char routing_id[256];      // 가변 길이

// pipe.hpp
blob_t _router_socket_routing_id;   // 가변 길이
int _server_socket_routing_id;      // 4B (uint32 의미)

// msg.hpp
uint32_t routing_id;                // 4B (이미 uint32)
```

### 2.3 현재 API

```cpp
// 소켓 identity 설정 (가변 길이)
zmq_setsockopt(socket, ZMQ_ROUTING_ID, "player-42", 9);
zmq_getsockopt(socket, ZMQ_ROUTING_ID, buf, &size);

// 연결 alias 설정 (가변 길이)
zmq_setsockopt(socket, ZMQ_CONNECT_ROUTING_ID, "alias", 5);
```

---

## 3. 정리된 정책

### 3.1 핵심 정책

| 항목 | 변경 후 |
|------|---------|
| 자동 생성 routing_id 포맷 | **모든 소켓에서 5B `[0x00][uint32]`** |
| 사용자 설정 (`ZMQ_ROUTING_ID`) | 가변 길이 문자열 유지 |
| 연결 alias (`ZMQ_CONNECT_ROUTING_ID`) | 가변 길이 문자열 유지 |
| 핸드셰이크 포맷 | 길이+가변 유지 |

### 3.2 routing_id 사용 규칙

- `ZMQ_ROUTING_ID`는 **소켓 identity**로 사용한다.
- `ZMQ_CONNECT_ROUTING_ID`는 **다음 connect에 적용되는 연결 alias**다.
- 미설정 시 자동 생성:
  - 모든 소켓에서 uint32 값을 생성
  - 저장 포맷은 `[0x00][uint32]` 5바이트
  - 0은 피하도록 통일 (필요 시 1부터 증가)

### 3.3 문자열 alias 유지 이유

- ROUTER는 문자열 alias를 사용한 디버깅/로깅 패턴이 많다.
- `ZMQ_CONNECT_ROUTING_ID`는 연결별 alias 지정에 필요하다.
- 따라서 **routing_id 길이 고정은 하지 않는다.**

### 3.4 문자열/바이너리 처리 원칙

- routing_id는 **바이너리 데이터**로 취급한다.
- 문자열로 사용하려면 애플리케이션이 직접 인코딩/디코딩한다.
- 자동 생성 routing_id는 내부 포맷이며, 외부 API에서 숫자 변환을 제공하지 않는다.

---

## 4. C API 명세

### 4.1 통일 type

```c
/* routing_id 표준 타입 */
typedef struct {
    uint8_t size;             /* 0~255 */
    uint8_t data[255];
} zmq_routing_id_t;
```

### 4.2 사용 예시 (자동 + 문자열 routing_id)

```c
void *sock = zmq_socket(ctx, ZMQ_ROUTER);

/* 자동 생성 routing_id 조회 */
uint8_t auto_buf[255];
size_t auto_size = sizeof(auto_buf);
zmq_getsockopt(sock, ZMQ_ROUTING_ID, auto_buf, &auto_size);
printf("auto routing_id(size=%zu) = ", auto_size);
for (size_t i = 0; i < auto_size; ++i)
    printf("%02x", auto_buf[i]);
printf("\n");

/* 문자열 routing_id 지정 */
const char *rid = "router-A";
zmq_setsockopt(sock, ZMQ_ROUTING_ID, rid, strlen(rid));

/* connect alias 지정 */
const char *alias = "edge-1";
zmq_setsockopt(sock, ZMQ_CONNECT_ROUTING_ID, alias, strlen(alias));

/* 읽어서 출력 (문자열일 때만 안전) */
uint8_t buf[255];
size_t size = sizeof(buf);
zmq_getsockopt(sock, ZMQ_ROUTING_ID, buf, &size);
printf("routing_id(size=%zu) = %.*s\n", size, (int)size, buf);
```

### 4.3 소켓 옵션 (현행 유지)

```c
/* ZMQ_ROUTING_ID: 소켓 identity (가변 길이) */
zmq_setsockopt(socket, ZMQ_ROUTING_ID, buf, size);
zmq_getsockopt(socket, ZMQ_ROUTING_ID, buf, &size);

/* ZMQ_CONNECT_ROUTING_ID: 다음 connect에 사용할 alias */
zmq_setsockopt(socket, ZMQ_CONNECT_ROUTING_ID, buf, size);
```

---

## 5. 구현 계획

### 5.1 수정 파일

| 파일 | 변경 내용 |
|------|----------|
| `include/zmq.h` | `zmq_routing_id_t` 선언 |
| `src/core/options.hpp/cpp` | 모든 소켓 routing_id 자동 생성 로직 추가 |
| `src/sockets/router.cpp` | 자동 생성 포맷 5B 유지 |
| `src/sockets/stream.cpp` | 자동 생성 포맷 4B -> 5B 변경 |
| `tests/` | auto routing_id 관련 테스트 갱신 |
| `doc/plan/01-enhanced-monitoring.md` | monitoring에서 routing_id_t 사용 |

### 5.2 단계별 구현

```
Phase 1: type 추가
└─ zmq_routing_id_t 정의

Phase 2: 자동 생성 통일
├─ 모든 소켓에서 uint32 시퀀스 생성
└─ 저장 포맷은 5B로 통일

Phase 3: 테스트/문서 갱신
├─ routing_id 길이 기대값 수정
└─ monitoring 문서 반영
```

---

## 6. 비호환 변경

### 6.1 호환성 정책

- **기존 버전과의 호환성은 고려하지 않는다.**
- `ZMQ_ROUTING_ID`, `ZMQ_CONNECT_ROUTING_ID` 문자열 사용은 **설계 상 필요하여 유지**한다.
- **STREAM 자동 생성 routing_id 길이 변경(4B -> 5B)**
  - 길이/내용을 직접 기대하는 코드나 테스트는 업데이트 필요

---

## 7. 검증 방법

### 7.1 단위 테스트

| 테스트 | 설명 |
|--------|------|
| `test_router_auto_id_format` | ROUTER 자동 생성 5B 형식 확인 |
| `test_stream_routing_id_size` | STREAM routing_id 5B 확인 |
| `test_connect_rid_string_alias` | 문자열 alias 동작 확인 |

---

## 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 0.1 | 2025-01-25 | 초안 작성 |
| 0.2 | 2025-01-25 | HELLO 프레임 형식 수정 (`control_type` 필드 추가) |
| 0.3 | 2026-01-25 | uint32 통일안 추가 (폐기) |
| 0.4 | 2026-01-25 | 형식 통일 보류, 현행 정책 정리 |
| 0.5 | 2026-01-25 | routing_id_t 표준화 |
| 0.6 | 2026-01-25 | 자동 생성 routing_id 4B 통일 (폐기) |
| 0.7 | 2026-01-25 | 자동 생성 값 uint32 통일, 포맷 5B/4B 유지 (폐기) |
| 0.9 | 2026-01-26 | 호환성 미고려 명시 + 자동 생성 포맷 5B 통일 (STREAM 포함) |
| 1.0 | 2026-01-26 | 변환 함수 제거, 문자열 routing_id 예시 추가 |
