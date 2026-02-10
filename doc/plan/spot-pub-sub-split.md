# SPOT PUB/SUB 분리 및 Lock 기반 Thread-safe PUB 설계

> **우선순위**: High
> **상태**: 구현 완료 (v0.8.0)
> **버전**: 1.0
> **의존성**:
> - [SPOT 토픽 PUB/SUB 가이드](../guide/07-3-spot.md)
> - [Registry/Discovery 타입 분리 계획](type-segmentation.md)

## 목차
1. [문제 정의](#1-문제-정의)
2. [목표](#2-목표)
3. [비목표](#3-비목표)
4. [요구사항](#4-요구사항)
5. [핵심 설계](#5-핵심-설계)
6. [API 설계](#6-api-설계)
7. [동시성 설계](#7-동시성-설계)
8. [구현 단계 계획](#8-구현-단계-계획)
9. [테스트 계획](#9-테스트-계획)
10. [리스크 및 대응](#10-리스크-및-대응)
11. [완료 기준](#11-완료-기준)

---

## 1. 문제 정의

현재 SPOT은 `spot_node + spot` 구조이며 `spot` 단일 객체가 publish/subscribe/recv를 모두 제공한다.
이 구조에서는 다음 문제가 있다.

- 역할이 혼합되어 API 의도가 불명확하다. (발행 전용/수신 전용 사용 의도 표현 어려움)
- pub/sub 경로가 같은 추상에 묶여 있어 동시성 정책이 명시적이지 않다.
- 향후 기능 확장 시(발행 최적화, 수신 큐 정책 등) 객체 책임 분리가 약하다.

요약: **발행과 수신 책임을 분리해 API와 내부 동시성 모델을 단순화할 필요가 있다.**

## 2. 목표

- `spot`를 제거하고 `spot_pub`, `spot_sub`로 명확히 분리한다.
- `spot_pub`는 lock 기반으로 thread-safe publish를 제공한다.
- 별도 큐/프록시 스레드 없이 호출 스레드에서 즉시 publish를 수행한다.
- discovery/mesh/heartbeat 등 네트워크 제어는 기존처럼 `spot_node` 책임으로 유지한다.

## 3. 비목표

- 기존 `spot` API와의 하위 호환 제공
- queue 기반 async publish 프록시 도입
- ringbuffer 모드 재도입
- topic create/destroy 재도입
- discovery/registry 프로토콜 자체 변경

## 4. 요구사항

### 4.1 기능 요구사항

- `spot_pub`:
  - 기본 생성 시 thread-safe 보장
  - topic publish
  - pub 소켓 옵션 설정
  - raw socket 핸들 노출 금지
- `spot_sub`:
  - topic subscribe/pattern subscribe/unsubscribe
  - recv
  - sub 소켓 옵션 설정
  - raw socket 핸들 노출 허용
- `spot_node`:
  - bind/connect peer/connect registry/register/set discovery 등 제어 기능 유지

### 4.2 동시성 요구사항

- `spot_pub_publish()`는 다중 스레드 동시 호출 시 내부적으로 직렬화되어야 한다.
- 직렬화는 `mutex` 기반으로 구현한다.
- publish 경로에 사용자 메시지 임시 큐를 두지 않는다.

### 4.3 운영 요구사항

- publish 실패 시 즉시 에러 반환한다. (비동기 지연 실패 없음)
- 메시지 캐싱으로 인한 메모리 급증 경로를 만들지 않는다.

## 5. 핵심 설계

### 5.1 객체 모델

- `spot_node_t`: 네트워크 제어 평면
- `spot_pub_t`: 발행 데이터 평면 핸들
- `spot_sub_t`: 구독/수신 데이터 평면 핸들

각 핸들은 동일 `spot_node_t`에 attach된다.

### 5.2 소켓 소유권

- PUB/SUB/DEALER 소켓 소유자는 `spot_node_t`이다.
- `spot_pub_t`/`spot_sub_t`는 소켓을 직접 소유하지 않고 node를 통해 동작한다.
- `spot_pub_t`는 raw 소켓 getter를 제공하지 않는다.
- `spot_sub_t`는 진단/고급옵션 목적의 raw SUB 소켓 getter를 제공한다.

### 5.3 토픽 정책

- topic mode 설정 API는 제공하지 않는다.
- topic create/destroy API는 제거한다.
- publish/subscribe는 토픽 문자열 기반으로만 동작한다.

### 5.4 호환성 정책

- 기존 `spot` API는 제거한다.
- 바인딩(Node/Java/Python/.NET/C++)도 동일 릴리스에서 `spot_pub`/`spot_sub`로 전환한다.

## 6. API 설계

### 6.1 C API (초안)

```c
/* pub */
void *zlink_spot_pub_new(void *node);
int zlink_spot_pub_destroy(void **pub_p);
int zlink_spot_pub_publish(void *pub, const char *topic_id,
                           zlink_msg_t *parts, size_t part_count, int flags);
int zlink_spot_pub_setsockopt(void *pub, int option,
                              const void *optval, size_t optvallen);

/* sub */
void *zlink_spot_sub_new(void *node);
int zlink_spot_sub_destroy(void **sub_p);
int zlink_spot_sub_subscribe(void *sub, const char *topic_id);
int zlink_spot_sub_subscribe_pattern(void *sub, const char *pattern);
int zlink_spot_sub_unsubscribe(void *sub, const char *topic_id_or_pattern);
int zlink_spot_sub_recv(void *sub, zlink_msg_t **parts, size_t *part_count,
                        int flags, char *topic_id_out, size_t *topic_id_len);
int zlink_spot_sub_setsockopt(void *sub, int option,
                              const void *optval, size_t optvallen);
void *zlink_spot_sub_socket(void *sub);
```

- `zlink_spot_pub_new()`:
  - 기본적으로 lock 직렬화가 적용된 thread-safe pub 핸들 생성
- `zlink_spot_sub_socket()`:
  - raw SUB socket 접근 제공 (pub과 달리 허용)

### 6.2 `flags` 정책

- `spot_pub_publish`: 기존과 동일하게 `flags=0`만 허용
- `spot_sub_recv`: `0` 또는 `ZLINK_DONTWAIT` 허용

## 7. 동시성 설계

### 7.1 Lock 모델

- `spot_node_t` 내부에 PUB 전용 mutex(`_pub_sync`)를 둔다.
- 아래 경로는 동일 mutex로 직렬화한다.
  - `spot_pub_publish`
  - PUB bind
  - PUB TLS 설정 변경
  - PUB setsockopt
  - node destroy 중 PUB close
- `spot_pub`는 raw socket 미노출 정책으로 thread-safe 계약이 외부에서 우회되지 않는다.

### 7.2 락 순서

- 기본 락 순서: `_sync` -> `_pub_sync`
- destroy는 소켓 포인터를 `_sync`에서 분리한 뒤 close 단계에서 `_pub_sync` 사용
- 반대 순서 취득 금지 (교착 방지)

### 7.3 성능/메모리 특성

- queue가 없으므로 publish burst 시 메모리 적체가 없다.
- 동시 publish는 mutex 경합만 발생하며, 경합 구간은 실제 send 경로로 제한된다.

## 8. 구현 단계 계획

### 단계 1: 코어 타입 분리

- `core/src/services/spot/`에 `spot_pub.*`, `spot_sub.*` 추가
- 기존 `spot.*` 제거
- `spot_node_t` 내부 `_spots` 관리를 pub/sub 타입별로 분리

### 단계 2: C API 교체

- `core/include/zlink.h`에 spot_pub/sub API 추가
- `core/src/api/zlink.cpp` 엔트리 구현
- 기존 `zlink_spot_*` API 제거

### 단계 3: 바인딩 정합

- C++/Node/Java/Python/.NET에서 `Spot` 제거, `SpotPub`/`SpotSub` 도입
- enum/문서에서 ringbuffer 흔적 제거 최종 점검

### 단계 4: 문서/예제 갱신

- `doc/guide/07-3-spot.md`를 pub/sub 분리 사용 예제로 개편
- bindings 가이드 동기화

## 9. 테스트 계획

### 9.1 단위 테스트

- pub publish 유효성 검증(topic/flags/parts)
- sub subscribe/pattern/unsubscribe 검증
- recv blocking/nonblocking 동작

### 9.2 동시성 테스트

- 다중 스레드 `spot_pub_publish` 경쟁 테스트
- publish 중 bind/setsockopt/destroy 경쟁 테스트
- deadlock 없음 검증(장시간 stress)

### 9.3 통합 테스트

- 로컬 pub/sub
- peer mesh pub/sub (inproc/tcp/ws/tls/wss)
- discovery 자동 연결 환경에서 다중 publisher/subscriber

## 10. 리스크 및 대응

- 리스크: API 전면 교체로 바인딩 동시 수정량 증가
  - 대응: 코어 변경 후 바인딩을 같은 브랜치에서 순차 적용
- 리스크: lock 범위 과대 시 publish 처리량 저하
  - 대응: `_pub_sync` 범위를 PUB socket 접근 코드로 제한
- 리스크: destroy 시 락 순서 오류로 교착
  - 대응: 락 순서 규칙 문서화 + 경쟁 테스트 추가

## 11. 완료 기준

- `spot_pub`/`spot_sub` API가 코어와 바인딩에 일관되게 반영
- 기존 `spot` API 제거 완료
- `spot_pub` raw socket API 미제공 확인
- `spot_sub` raw socket API 제공 및 문서화 완료
- `thread-safe pub` lock 직렬화 테스트 통과
- SPOT 기존 통합 테스트 시나리오(transport/discovery/scale) 무회귀
