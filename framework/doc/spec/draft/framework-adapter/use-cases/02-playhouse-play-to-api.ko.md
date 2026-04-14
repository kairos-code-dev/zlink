[스펙 목차](../../../README.ko.md)

# Draft Use Case -- Playhouse Play To API

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `playhouse` 연동 방향을 정리하기 위한 설계 메모다.

## 1. 상황

`playhouse`는 게임 서버 프레임워크이며, play 서버의 stage는 `SPOT` 기반으로
구성할 계획이다. 이 환경에서 play 서버는 api 서버에 outgame 성격의 정보를
질의하거나 처리를 요청해야 한다.

대표 예시는 아래와 같다.

- 프로필 조회
- 인벤토리 조회
- 상점 결제 확인
- 계정 단위 상태 저장

## 2. 사용자가 기대하는 경험

play 서버 개발자는 raw socket을 직접 열고 multipart framing을 손으로 맞추기보다,
아래와 같은 형태를 원할 가능성이 크다.

- `outgameClient.request("profile.get", req)`
- `accountClient.request("inventory.get", req)`
- 서비스별 client를 DI 또는 server bootstrap에서 받아 사용

서버 쪽 api 서버 개발자는 아래와 같은 형태를 원한다.

- `profile.get` handler 등록
- 요청 body를 typed object로 수신
- 공통 header에서 session, correlation, deadline, caller 정보를 조회

## 3. 서비스 묶음에 대한 요구

이 시나리오에서는 대상 서버가 **서비스 단위로 묶여야** 한다.

예를 들면 아래처럼 나뉠 수 있다.

- `api.profile`
- `api.inventory`
- `api.payment`

play 서버가 여러 api 서비스군에 요청을 보내야 한다면, 내부적으로는 서비스별
outbound client나 connection pool이 필요하다. 이때 사용자가 raw `DEALER`
소켓을 직접 들고 있게 만들기보다, `ZLink Framework`가 서비스 이름 기준
client를 관리하는 편이 낫다.

이 모델에서는 중간 gateway나 전용 로드밸런서를 별도로 두지 않아도 된다.
play 서버는 `api.profile`, `api.inventory` 같은 `service_name`만 기준으로
요청하고, 실제 provider 위치와 선택은 Discovery와 adapter가 숨긴다.

## 4. 필요한 능력

- request-response
- service_name 단위 provider grouping
- Discovery를 통한 대상 자동 발견
- 필요하면 수동 endpoint 설정
- service별 client 분리
- 낮은 지연과 높은 동시성
- 게임 서버에서 다루기 쉬운 timeout과 cancellation

## 5. 내부 매핑 초안

이 use case의 기본 topology는 `play side DEALER -> api side ROUTER`가
자연스럽다. 다만 그 구조는 내부 설명으로만 남기고, 공용 API는 아래 개념으로
올리는 편이 낫다.

- `service client`
- `request handler`
- `message name`
- `request context`

즉 사용자는 "dealer를 하나 더 만들까"보다
"`api.profile` client를 하나 더 등록할까"를 먼저 보게 된다.

## 6. 이 use case가 설계에 주는 요구

- 서비스 이름과 client 구분이 중요하다.
- 하나의 play 서버가 여러 논리 서비스에 동시에 붙는 구성을 자연스럽게 다뤄야
  한다.
- `SPOT` 기반 stage 구조와 충돌하지 않아야 한다.
- 게임 서버에서 쓰는 요청은 일반 웹 백엔드 RPC와 같은 프로그래밍 모델로 보여야
  한다.
