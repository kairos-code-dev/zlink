# Registry/Discovery 타입 분리 및 SPOT 군 분리 계획

> **우선순위**: High
> **상태**: Draft
> **버전**: 1.0
> **의존성**:
> - [Service Discovery 가이드](../guide/07-1-discovery.md)
> - [SPOT 토픽 PUB/SUB 가이드](../guide/07-3-spot.md)

## 목차
1. [문제 정의](#1-문제-정의)
2. [목표](#2-목표)
3. [비목표](#3-비목표)
4. [요구사항](#4-요구사항)
5. [설계안](#5-설계안)
6. [프로토콜 변경안](#6-프로토콜-변경안)
7. [API 변경안](#7-api-변경안)
8. [구현 단계 계획](#8-구현-단계-계획)
9. [테스트 계획](#9-테스트-계획)
10. [마이그레이션 전략](#10-마이그레이션-전략)
11. [리스크 및 대응](#11-리스크-및-대응)
12. [완료 기준](#12-완료-기준)

---

## 1. 문제 정의

현재 Registry/Discovery 경로에서 Receiver(Gateway 대상)와 SPOT Node가 같은 서비스 네임 공간을 공유한다.
이 구조에서는 다음 문제가 있다.

- Registry가 수신한 등록 엔트리가 Gateway용인지 SPOT용인지 구분할 수 없다.
- Discovery가 service_name만 기준으로 목록을 전달하므로, 의도치 않게 다른 평면(plane)의 엔트리가 섞일 수 있다.
- SPOT에서 service_name으로 군(그룹) 분리하고 싶어도, 타입 정보가 없으면 운영 중 혼선 가능성이 높다.

요약: **service_name 단일 축만으로는 Gateway와 SPOT를 동시에 안정적으로 분리 운영하기 어렵다.**

## 2. 목표

- Registry가 등록/해제/heartbeat/update_weight 요청을 받을 때 엔트리 타입을 구분 저장한다.
- 타입을 최소 2종으로 분리한다.
  - `gateway_receiver`
  - `spot_node`
- Discovery는 생성 시점에 타입을 고정하고, 구독/조회는 고정 타입으로 동작한다.
- SPOT는 `service_name` 단위 군 분리를 유지하되, 같은 이름의 Gateway 엔트리와 충돌하지 않게 한다.
- `service_type`은 내부 구현에서만 사용하고 사용자 C API에는 노출하지 않는다.

## 3. 비목표

- Service Discovery 전체 프로토콜 재설계
- Gateway 라우팅 알고리즘 변경
- SPOT 데이터 플레인(PUB/SUB) 전달 방식 변경
- 기존 등록 데이터의 영구 저장소 스키마 도입

## 4. 요구사항

### 4.1 기능 요구사항

- Registry 내부 저장 키를 `(type, service_name)` 기준으로 관리한다.
- Discovery 인스턴스는 생성 시 `service_type`을 고정 보관한다.
- Discovery 구독/조회는 `(fixed_type, service_name)`으로 처리한다.
- Receiver/Gateway API는 기본 타입을 `gateway_receiver`로 동작한다.
- SPOT Node 등록 API는 기본 타입을 `spot_node`로 동작한다.
- SPOT `set_discovery()`는 `spot_node` 타입만 조회/연결한다.

### 4.2 호환 요구사항

- 하위 호환은 제공하지 않는다.
- Registry/Discovery/Receiver/SPOT을 동일 릴리스 기준으로 일괄 적용한다.

### 4.3 운영 요구사항

- 타입별 엔트리 수/브로드캐스트 크기/갱신 주기를 관측 가능해야 한다.
- 단일 버전 일괄 배포 절차가 명확해야 한다.

## 5. 설계안

### 5.1 핵심 모델

새 공통 식별자:

- `service_type` (enum)
- `service_name` (string)
- `routing_id` (기존)
- `endpoint` (기존)
- `weight` (기존)

Registry의 내부 맵을 아래와 같이 재구성한다.

- 기존: `map<string service_name, vector<receiver_entry>>`
- 변경: `map<service_key(type, name), vector<receiver_entry>>`

### 5.2 타입 정의

초기 enum:

- `1`: gateway_receiver
- `2`: spot_node

원칙:

- 모든 등록/조회 경로는 명시적 타입(1 또는 2)만 사용한다.
- `service_type`은 애플리케이션 API로 노출하지 않는다.

### 5.5 Discovery 타입 고정 방식

- Discovery 객체 생성 시 타입을 입력받아 내부 필드로 고정한다.
- 생성 이후 타입 변경은 허용하지 않는다.
- `subscribe/get/count`는 기존처럼 `service_name`만 받되, 내부적으로 고정 타입을 사용한다.
- 잘못된 타입의 Discovery를 SPOT에 연결하려 할 경우 즉시 실패시킨다.

### 5.3 브로드캐스트 분리 전략

옵션 A (권장): 단일 SERVICE_LIST 안에 type 필드 추가

- 장점: 소켓/채널 추가 없음, 구현 단순
- 단점: payload 크기 증가

옵션 B: 타입별 메시지 분리

- 장점: 구독자별 payload 축소
- 단점: 프로토콜/코드 복잡도 상승

본 계획은 **옵션 A**를 기준으로 진행한다.

### 5.4 SPOT 군 분리 방식

SPOT 등록 시 `service_name=spot-field-a`처럼 군 이름을 명시한다.
`set_discovery(node, discovery, "spot-field-a")`는 내부적으로 타입 `spot_node` + 이름 `spot-field-a` 조합만 조회한다.

결과:

- `gateway_receiver/spot-field-a`와 `spot_node/spot-field-a`가 공존 가능
- SPOT는 동일 이름의 Gateway 군과 충돌 없이 자동 연결

## 6. 프로토콜 변경안

### 6.1 REGISTER/UNREGISTER/HEARTBEAT/UPDATE_WEIGHT

프레임에 `service_type`(u16) 필드를 필수로 추가한다.

- `[msg_id, service_type, service_name, endpoint, weight]`

`unregister`/`heartbeat`/`update_weight`도 동일하게 type 포함.

### 6.2 SERVICE_LIST

receiver 엔트리 단위에 `service_type`을 포함한다.

- 기존 엔트리: `endpoint, routing_id, weight`
- 변경 엔트리: `service_type, endpoint, routing_id, weight`

### 6.3 타입 결정 원칙

- Receiver/Gateway 경로는 내부적으로 `gateway_receiver`를 사용한다.
- SPOT Node 경로는 내부적으로 `spot_node`를 사용한다.
- Discovery 경로는 생성 시 고정된 타입을 사용한다.
- 사용자는 `subscribe/get/count` 호출 시 기존처럼 `service_name`만 전달한다.

## 7. API 변경안

### 7.1 공개 C API 정책

- Discovery 생성 API에서만 타입 입력을 받는다.
- Discovery 구독/조회 API는 `service_name`만 받는 기존 시그니처를 유지한다.
- `service_type` enum은 Discovery 생성 파라미터 검증용으로만 공개한다.
- 타입 분기는 내부 구현(Registry/Discovery/Receiver/SpotNode)에서 처리한다.

### 7.2 SPOT API 반영

- `zlink_spot_node_register()`는 내부적으로 type=`spot_node`로 등록
- `zlink_spot_node_set_discovery()`는 typed 조회 경로 사용
- `service_name`은 기존 API 그대로 군 분리 키로 사용
- `zlink_spot_node_set_discovery()`는 discovery의 고정 타입이 `spot_node`인지 검증

## 8. 구현 단계 계획

### 단계 1: 내부 타입 모델 도입

- Registry 내부 자료구조를 `(type, name)` 키로 전환
- receiver entry에 type 저장
- 기존 로직 컴파일/단위테스트 통과

산출물:

- `registry.cpp/hpp` 타입 키 구조 반영
- 기존 테스트 무회귀

### 단계 2: 프로토콜 v2 파서/인코더 추가

- discovery protocol 유틸에 type frame encode/decode 추가
- Registry 수신부는 새 포맷만 처리
- SERVICE_LIST 송출에 type 포함

산출물:

- `discovery/protocol.*`, `registry.cpp`, `discovery.cpp`

### 단계 3: Discovery 내부 typed 캐시/조회 추가

- discovery 캐시 키를 `(type, name)` 기준으로 확장
- discovery 생성 시 타입 고정 필드 저장
- subscribe/get/count는 고정 타입 기준으로 조회
- discovery 생성 파라미터 타입 검증 추가

산출물:

- `discovery.cpp/hpp`, `api/zlink.cpp`, `include/zlink.h`, `spot/spot_node.cpp`

### 단계 4: Gateway/Receiver 경로 정합

- Receiver register/update/unregister/heartbeat에서 type=`gateway_receiver` 송신
- Gateway 테스트 전체 회귀

산출물:

- `receiver.cpp`, `gateway.cpp` 영향 최소 검증

### 단계 5: SPOT 경로 정합

- SpotNode register/unregister/heartbeat에서 type=`spot_node` 송신
- `set_discovery` peer refresh가 typed 조회 기반으로 동작

산출물:

- `spot/spot_node.cpp`
- spot discovery 자동 연결 테스트 보강

### 단계 6: 운영 관측/안정화

- 타입별 카운터/로그 태그 추가
- 문서/마이그레이션 가이드 업데이트

## 9. 테스트 계획

### 9.1 단위 테스트

- protocol encode/decode (type 필수 포맷)
- registry key 분리 동작
- discovery 내부 typed cache 조회

### 9.2 통합 테스트

- Gateway receiver와 Spot node가 동일 service_name 사용 시 상호 비간섭 검증
- Spot discovery 자동 연결 시 spot_node 타입만 연결되는지 검증
- discovery 생성 타입 불일치 시 `spot_node_set_discovery()` 실패 검증

### 9.3 대규모 테스트

- 기존 100x100 spot 테스트 유지
- multi spotnode + discovery 자동연결 테스트에서 typed 분리 검증 추가

## 10. 마이그레이션 전략

### 10.1 적용 순서

1. Registry/Discovery/Receiver/SpotNode를 동일 브랜치에서 동시 반영
2. 통합 테스트(gateway + spot + discovery) 전체 통과 확인
3. 단일 릴리스로 일괄 배포

### 10.2 운영 가이드

- Gateway 계열 service_name 규칙 예: `gw.<domain>.<svc>`
- SPOT 계열 service_name 규칙 예: `spot.<world>.<field>`
- 타입 + 이름 규칙을 배포 체크리스트에 포함

## 11. 리스크 및 대응

- 리스크: 혼합 버전에서 파싱 불일치
  - 대응: 호환 모드 없이 단일 버전 일괄 배포

- 리스크: SERVICE_LIST payload 증가
  - 대응: 타입 필드를 u16 고정, 필요 시 압축/분할 전송 검토

- 리스크: 기존 API 사용자 혼란
  - 대응: 공개 API 변경 없음 + 문서에 내부 타입 분리 원칙 명시

- 리스크: SPOT 자동연결 오연결
  - 대응: set_discovery 경로에서 type 강제, 테스트로 회귀 방지

## 12. 완료 기준

아래 항목을 모두 만족하면 완료로 본다.

- Registry 내부 저장/조회가 `(type, service_name)` 기준으로 동작
- Gateway/Receiver와 SPOT이 같은 service_name을 사용해도 분리 동작
- Spot discovery 자동연결이 `spot_node` 타입만 대상으로 연결
- 기존 discovery/gateway/spot 테스트 무회귀
- 신규 typed 분리 테스트 100% PASS
- 문서(`04`, `05`, 본 문서) 업데이트 완료
