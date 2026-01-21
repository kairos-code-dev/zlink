# ZMP 확장 기능 상세 설계 (v1 초안)

작성일: 2026-01-22
작성자: Codex
상태: Draft

## 목표
- ZMP v0의 단순 프레이밍은 유지하되, 운영/디버깅 가시성과
  프로토콜 안정성을 보강한다.
- 핫패스 비용을 최소화하고, 선택 기능은 기본 비활성으로 둔다.

## 범위 (추가 기능)
1) ERROR/READY 제어 프레임
2) READY 메타데이터 교환(옵션)
3) Heartbeat TTL/Context 확장(옵션)
4) 플래그 조합 허용(MORE+IDENTITY/CONTROL)

## 범위 제외
- 대용량 프레임 확장(64-bit length)
- ZMTP 상호운용성 확보(프로토콜 변환/호환)
- 라우팅/구독 동작의 근본적 재정의

## 1. 프레임/헤더 기본
ZMP 헤더는 그대로 유지한다.

```
0: magic(0x5A)
1: version (0x02)
2: flags
3: reserved(0)
4..7: body_len (uint32, network order)
```

본문 길이 상한은 uint32 최대값(4GB-1)으로 확대한다.

## 2. 제어 프레임
제어 프레임은 `flags`에 `CONTROL` 비트를 켠다.
본문 첫 바이트는 제어 타입으로 사용한다.

### 2.1 READY
- 목적: 핸드셰이크 완료 명시, 메타데이터 전달
- 타입: `READY (0x04)`
- 바디 포맷:
  - byte0: control_type = READY
  - byte1..: 메타데이터(옵션)

### 2.2 ERROR
- 목적: 프로토콜/핸드셰이크 실패 이유 전달
- 타입: `ERROR (0x05)`
- 바디 포맷:
  - byte0: control_type = ERROR
  - byte1: error_code (u8)
  - byte2: reason_len (u8)
  - byte3..: reason bytes (ASCII)

권장 error_code:
- 0x01: MALFORMED (형식 오류)
- 0x02: UNSUPPORTED (미지원 기능/버전)
- 0x03: INCOMPATIBLE (소켓 타입 불일치 등)
- 0x04: AUTH (인증/정책 실패)
- 0x7F: INTERNAL (기타 내부 오류)

## 3. READY 메타데이터(옵션)
메타데이터는 ZMTP의 property 인코딩 규격을 재사용한다.

```
name_len(u8) | name(bytes) | value_len(u32, network order) | value(bytes)
```

### 3.1 전송 조건
- 기본 비활성. 핸드셰이크 단계에서만 전송한다.
- 활성화는 소켓/컨텍스트 옵션으로 제어한다(옵션 명칭은 별도 결정).
- 비활성 시 READY 바디는 1바이트(control_type)만 포함한다.

### 3.2 기본/확장 프로퍼티
기본 프로퍼티(권장):
- Socket-Type (문자열)
- Identity (DEALER/ROUTER만, 바이너리 가능)

확장 프로퍼티(선택):
- Resource (리소스/서비스 식별자, 문자열)
  - 충돌을 피하기 위해 `Zlink-Resource`와 같은 네임스페이스 사용
    권장.

### 3.3 인코딩/제약
- name_len은 1..255, value_len은 0..2^32-1이지만,
  운영 상한을 둔다(예: 총 메타데이터 4KB 이내 권장).
- 알 수 없는 프로퍼티는 무시하거나 메타데이터로 보관한다.
- Identity는 `recv_routing_id`가 켜진 경우에만 반영한다.

### 3.4 목적/효과
- 연결 시점의 소켓 타입/아이덴티티/리소스를 기록해
  모니터링/디버깅 가시성을 높인다.
- 핫패스 비용은 없다(핸드셰이크 1회).

## 4. Heartbeat TTL/Context 확장(옵션)
기존 Heartbeat는 타입 1바이트만 사용한다. 확장형은 TTL/Context
필드를 추가한다.

### 4.1 HEARTBEAT
- 타입: `HEARTBEAT (0x02)`
- 바디 포맷:
  - byte0: control_type = HEARTBEAT
  - byte1..2: ttl_deciseconds (u16, network order)
  - byte3: ctx_len (u8, 0..16 권장)
  - byte4..: ctx bytes

### 4.2 HEARTBEAT_ACK
- 타입: `HEARTBEAT_ACK (0x03)`
- 바디 포맷:
  - byte0: control_type = HEARTBEAT_ACK
  - byte1: ctx_len
  - byte2..: ctx bytes

### 4.3 의미/동작
- ttl_deciseconds는 상대에게 "이 시간 안에 신호가 없으면
  끊어도 된다"는 제안 값이다.
- 합의 규칙(권장):
  - 로컬 타임아웃이 없으면 TTL을 그대로 채택.
  - 로컬 타임아웃이 있으면 `min(local, ttl)`을 사용.
- ctx는 왕복 확인 및 지연 측정에 사용하는 불투명 값이다.
- 수신측은 HEARTBEAT 수신 시 동일 ctx로 ACK를 보내는 것을 권장.

### 4.4 호환성/에러 처리
- 수신측은 바디 길이가 1인 레거시 형식을 허용한다.
- 확장형은 길이가 4 이상일 때만 파싱한다.
- ctx_len이 비정상적이면(E.g. 64 초과) EPROTO 처리 권장.

## 5. 플래그 조합 허용
현재 ZMP 디코더는 단일 비트만 허용한다. 이를 완화한다.

허용 조합(권장):
- MORE + IDENTITY (라우팅 프레임에 더 많은 파트가 뒤따름)
- 단일 플래그(기존 동작 유지)

금지 조합(예시):
- CONTROL + MORE (제어 프레임은 단일 프레임만 허용)
- SUBSCRIBE/CANCEL과 다른 플래그 결합
- 미정의 비트 사용

디코더는 `flags & zmp_flag_mask`만 허용하고, 금지 조합은 EPROTO로
처리한다.

## 6. 핸드셰이크 흐름
1) HELLO 송신/수신
2) HELLO 교환 완료 후 READY 송신
3) READY 수신 시 핸드셰이크 완료
4) 오류 발생 시 ERROR 송신 후 연결 종료

READY 이전에 데이터 프레임 수신 시 오류 처리.

## 7. 참고 소스 위치
- `src/zmp_protocol.hpp`: version/flags/control type 정의
- `src/zmp_decoder.cpp`: 플래그 조합 허용, control 메시지 길이 처리
- `src/zmp_encoder.cpp`: control 플래그/바디 인코딩 확인
- `src/asio/asio_zmp_engine.cpp`: HELLO/READY/ERROR 처리,
  heartbeat TTL/Context 파싱/타이머 정책
- `src/mechanism.cpp`: 메타데이터 property 인코딩/파싱 재사용

## 8. 결정 요약
- READY/ERROR 도입으로 핸드셰이크 상태와 실패 원인을 명확히 한다.
- 메타데이터는 기본 비활성으로 유지하고, 필요 시만 전송한다.
- Heartbeat는 레거시 1바이트 형식을 그대로 허용한다.
- 플래그 조합은 MORE+IDENTITY/CONTROL만 허용해 라우팅/멀티파트
  호환성을 높인다.
- 대용량 프레임 확장은 범위에서 제외한다.
