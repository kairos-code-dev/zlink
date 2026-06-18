<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Use Cases](README.ko.md) | [다음: Use Case -- Playhouse Play To API](02-playhouse-play-to-api.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[use case 목록](README.ko.md) | [Framework 문서 묶음](../../../README.ko.md) | [검증](../spec/usecase-validation.ko.md)

# Use Case -- Service To Service RPC

## 1. 상황

일반적인 웹 백엔드에서는 서비스 A가 서비스 B에 내부 요청을 보내고 응답을 받는
일이 아주 흔하다.

예를 들면 아래와 같은 호출이다.

- `api-gateway -> profile-service`
- `order-service -> inventory-service`
- `match-service -> ranking-service`

지금 목표는 이 흐름을 HTTP 내부 호출이나 gRPC처럼 쓰되, zlink 기반 TCP 비동기
메시징 위에서 더 가볍게 다루게 만드는 것이다.

## 2. 사용자가 기대하는 경험

- 서버 쪽에서는 프레임워크의 handler 등록 방식으로 요청을 받는다.
- 클라이언트 쪽에서는 DI로 주입된 client 또는 proxy를 통해 요청을 보낸다.
- 요청 하나마다 header, payload, timeout, metadata를 다룰 수 있다.
- payload는 codec에 따라 byte payload로 직렬화된다. JSON은 기본값이고, Protobuf나
  MessagePack 같은 binary codec은 framework codec extension으로 추가한다.
- 대상 서버 주소를 직접 적지 않고, channel 이름 기준으로 보낼 수 있다.

즉 사용자는 "몇 번 포트의 어느 peer에게 connect할까"보다
"`profile` channel로 `GetProfileRequest`를 어디에 보낼까"를 먼저 생각한다.

여기서 중요한 차별점은 위치투명성을 위해 별도 gateway를 강제하지 않는다는
점이다. 기존 웹 시스템에서는 서비스 주소를 숨기기 위해 gateway를 두는 경우가
많고, provider 선택을 위해 전용 load balancer를 앞단에 두기도 한다.
현재 방향에서는 `channel name`과 channel별 `Discovery`만으로 직접
location-transparent 호출이 가능해야 한다.

## 3. 필요한 능력

- 요청/응답 상호작용
- channel 이름 기반 대상 선택
- 수동 연결과 Discovery 연결 둘 다 지원
- gateway나 전용 로드밸런서 없이도 위치투명 호출 가능
- channel별 `rid` 집합 관리
- timeout, correlation, deadline 전달
- 공통 에러 모델
- codec 교체. codec을 바꿔도 handler와 client 호출 모양은 유지하고, 구성 단계에서
  framework codec extension을 등록한다.
- 프레임워크 DI 통합

## 4. 기대하는 처리 흐름

1. 애플리케이션이 `profile-service` client를 DI로 받는다.
2. client가 `GetProfileRequest`를 header + payload 메시지로 만든다.
3. `ZLink Framework`가 `profile-service` 전용 channel을 통해 요청을 보낸다.
4. 서버 handler가 요청을 처리한다.
5. 응답 또는 에러가 같은 상관관계 정보와 함께 돌아온다.

## 5. 내부 매핑 초안

이 use case의 기본 매핑은 `DEALER -> ROUTER`가 가장 자연스럽다.
다만 그 topology를 사용자가 직접 의식하게 만들지는 않는다.

공용 개념은 아래처럼 잡는 편이 낫다.

- `request-response`
- `channel client`
- `message handler`
- `channel name`

## 6. 이 use case가 설계에 주는 요구

- `ZLink Framework`의 **기본 모델**은 request-response여야 한다.
- `rid`를 직접 다루는 routed 호출은 고급 내부 모델로 남길 수 있지만, 기본 서버
  API로 삼는 것은 우선순위가 낮다.
- payload codec은 고정하지 않아야 한다.
- 기본 packet key를 안정적으로 정할 수 있어야 한다.
- 기본 규칙만으로 부족하면 `PacketName` 같은 explicit override가 있어야 한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Use Cases](README.ko.md) | [다음: Use Case -- Playhouse Play To API](02-playhouse-play-to-api.ko.md)
<!-- framework-adapter-nav:bottom:end -->
