# PUB/SUB PGM/EPGM 스펙 (PGM/EPGM Multicast PUB/SUB)

> **우선순위**: 6 (Recommended Feature)
> **상태**: Draft
> **버전**: 1.0
> **의존성**:
> - PUB/SUB 소켓 기본 동작
> - (선택) [05-spot-topic-pubsub.md](05-spot-topic-pubsub.md) 연동
> - OpenPGM (빌드 옵션)

## 목차
1. [개요](#1-개요)
2. [아키텍처](#2-아키텍처)
3. [엔드포인트/옵션](#3-엔드포인트옵션)
4. [C API 명세](#4-c-api-명세)
5. [구현 계획](#5-구현-계획)
6. [검증 방법](#6-검증-방법)
7. [리스크/제약](#7-리스크제약)
8. [변경 이력](#8-변경-이력)

---

## 1. 개요

### 1.1 배경

PUB/SUB의 대규모 fanout에서 TCP 기반 다중 연결은 연결 수와
복제 비용이 급격히 증가한다. PGM/EPGM 멀티캐스트는 네트워크
레벨에서 패킷 복제를 수행해 **대규모 fanout 효율**을 개선할 수 있다.
본 기능은 **libzlink와 동일하게 OpenPGM**(멀티캐스트 라이브러리)을
연동해 구현한다.

### 1.2 목표

- **PUB/SUB 멀티캐스트 지원**: `pgm://`, `epgm://` 전송 지원
- **저비용 fanout**: 다수 구독자에 대한 송신 비용 절감
- **옵션 기반 활성화**: 빌드 시 의존성 발견 시에만 활성
- **기존 API 유지**: `zlink_bind/connect`와 기존 옵션만 사용

### 1.3 비목표

- ROUTER/DEALER 등 비 PUB/SUB 패턴 지원
- 멀티캐스트 신뢰성 보장(SLA) 자체 제공
- 멀티캐스트 네트워크 구성/라우팅 자동화

---

## 2. 아키텍처

### 2.1 동작 개요

```
PUB (bind: pgm/epgm)  -->  [Multicast 네트워크]  -->  SUB (connect)
```

- PUB가 멀티캐스트 그룹으로 송신한다.
- SUB는 멀티캐스트 그룹을 구독한다.
- 필터링은 **SUB 측**에서 수행된다.
- 멀티캐스트 소켓은 일반 TCP/IPC와 **혼용 금지**가 원칙이다.

### 2.2 SPOT 연동(선택)

- SPOT Node의 PUB/SUB 소켓을 `pgm/epgm`으로 구성 가능
- Discovery/Gateway는 동일하며 **전송 경로만 변경**

---

## 3. 엔드포인트/옵션

### 3.1 URI 형식

- `pgm://<iface>;<multicast_addr>:<port>`
- `epgm://<iface>;<multicast_addr>:<port>`

> 정확한 형식은 libzlink의 PGM URI 규칙을 따른다.

### 3.2 관련 소켓 옵션

다음 옵션은 이미 공개 API에 존재하며 PGM/EPGM에서 사용된다.

- `ZLINK_RATE`: 송신 속도 제한 (Kbps)
- `ZLINK_RECOVERY_IVL`: 재전송/복구 타이밍
- `ZLINK_SNDBUF`, `ZLINK_RCVBUF`: OS 버퍼 크기
- `ZLINK_MULTICAST_HOPS`: 멀티캐스트 홉 제한
- `ZLINK_MULTICAST_MAXTPDU`: 최대 전송 단위 (TPDU)

---

## 4. C API 명세

### 4.1 API 변경 없음

- **신규 API 추가 없음**
- 기존 `zlink_bind`, `zlink_connect`, `zlink_setsockopt`로 사용

### 4.2 예시

```
zlink_setsockopt(pub, ZLINK_RATE, &rate, sizeof(rate));
zlink_bind(pub, "epgm://eth0;239.192.1.1:5555");
```

---

## 5. 구현 계획

### 5.1 빌드/의존성

1) OpenPGM 탐지/연결
- **OpenPGM 사용을 전제로** CMake에서 탐지 (pkg-config 사용)
- 실패 시 PGM/EPGM 비활성 (컴파일 제외)
- 필요 시 `OPENPGM_PKGCONFIG_NAME`로 패키지 이름 지정

2) 옵션 토글
- `-DWITH_OPENPGM=ON/OFF` (기본 OFF)
- OFF일 때 `pgm://` URI는 `EPROTONOSUPPORT` 반환

### 5.2 전송 계층 통합

1) `pgm/epgm` URI 파서 추가
- 기존 endpoint 파서에 스킴 등록
- `iface`, `group`, `port` 파싱

2) PGM 전송 구현
- OpenPGM API를 래핑하는 전송 클래스 추가
- PUB->PGM 송신, SUB->PGM 수신 구현

3) 소켓 옵션 매핑
- `ZLINK_RATE`, `ZLINK_RECOVERY_IVL` 등 옵션을 PGM 핸들에 전달

### 5.3 SPOT 연동

- SPOT Node 옵션에 `transport=pgm|epgm` 허용
- Discovery/Gateway 로직은 변경 없이 동작

---

## 6. 검증 방법

1) 기능 테스트
- PUB/SUB 멀티캐스트 기본 송수신
- 구독 필터(토픽) 정상 동작 확인

2) 회귀 테스트
- 기존 TCP/IPC/WSS/WS 경로 영향 없음

3) 성능 테스트
- fanout 규모별 throughput 비교 (TCP vs PGM)

---

## 7. 리스크/제약

- 멀티캐스트 네트워크 설정(IGMP, 라우팅) 필요
- VM/컨테이너 환경에서 지원 제한 가능
- TLS/암호화는 별도 경로 필요 (PGM 자체 암호화 미지원)

---

## 8. 변경 이력

- 2026-01-29: 초안 작성
