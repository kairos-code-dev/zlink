[스펙 목차](../../README.ko.md)

# Draft -- ZLink Framework Overview

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `ZLink Framework`의 목표와 범위를 설명하기 위한
> 개요 문서다.

## 1. 한 줄 정의

`ZLink Framework`는 zlink 바인딩 위에 올라가서, 기존 애플리케이션 프레임워크에서
**gateway나 전용 로드밸런서 없이도** `channel_name` 기준의 직접 channel 호출,
pub/sub, `SPOT`, `STREAM`, channel discovery를 사용할 수 있게 하는 상위
계층이다.

## 2. 무엇을 제공하는가

`ZLink Framework`는 아래 기능을 하나의 방향으로 묶는다.

- server-to-server send/request
- router-to-router integration
- pub/sub integration
- spot integration
- stream integration
- channel discovery
- registry topology inspection
- channel별 Discovery
- framework-friendly handler / client / event API

즉 raw socket과 low-level discovery를 프레임워크 사용자가 직접 다루지 않고도,
기존 HTTP나 gRPC를 쓰던 감각에 가까운 개발 모델을 제공하는 것이 목표다.
다만 내부에서 무엇을 쓰는지는 숨기더라도, framework가 실제로 통합할 transport
축 자체는 명확해야 한다.

현재 이 초안은 아래 네 축을 직접 통합 대상으로 본다.

1. `ROUTER <-> ROUTER`
2. `SPOT`
3. `PUB/SUB`
4. `STREAM`

그리고 그 위에 사용자 경험은 아래처럼 다시 올린다.

- 서버 간 `send`
- 서버 간 `request`
- pub/sub
- `SPOT` 현재 channel 안의 publish/subscribe
- attach된 channel client를 통한 cross-channel send/request
- stream handler

## 3. 핵심 차별점

일반적인 웹 서버 환경에서는 위치투명성과 provider 선택을 위해 아래 중 하나를
두는 경우가 많다.

- API gateway
- 전용 load balancer
- service proxy

`ZLink Framework`는 이와 다른 방향을 기본으로 본다.

- 호출자는 gateway 주소 대신 `channel_name`을 기준으로 요청한다.
- framework runtime이 channel마다 별도 outbound socket을 만든다.
- Discovery가 그 channel view 안의 provider 위치를 숨긴다.
- framework는 그 channel 안의 `rid` 집합과 연결 상태를 기준으로 요청을 보낸다.
- 요청은 중간 gateway 없이 provider로 직접 간다.

즉 "위치투명성을 얻으려면 반드시 gateway를 거쳐야 한다"는 전제를 두지 않는다.

## 4. 무엇을 대체할 수 있는가

### 4.1 잘 대체하는 것

- 내부 서비스 간 위치투명 호출
- channel 이름 기반 provider 선택
- 일부 내부 gateway 또는 내부 load balancing 계층
- request-response와 event fan-out을 함께 쓰는 내부 통신

### 4.2 바로 대체하지 않는 것

- 외부 공개 API용 edge gateway
- 인증/인가 중앙 정책
- rate limiting, quota, WAF
- public API versioning
- edge cache, billing, audit

즉 `ZLink Framework`는 곧바로 edge API gateway 제품이 아니라,
**내부 서비스 통신 기반**에 더 가깝다.

## 5. 그 위에 무엇을 만들 수 있는가

이 계층 위에는 필요에 따라 별도의 고성능 API gateway를 만들 수 있다.

즉 관계는 아래처럼 보는 편이 정확하다.

1. `ZLink`
   기반 messaging/runtime library
2. `ZLink Framework`
   framework integration layer
3. optional gateway products
   필요하면 그 위에 올리는 ingress 또는 edge 계층

## 6. 현재 우선 범위

현재 초안이 우선 다루는 상호작용 모델은 아래와 같다.

- request-response
- command
- publish-subscribe
- stream

고급 조합 모델은 아래처럼 후속으로 다룬다.

- worker-dispatch
- scatter-gather
- workflow orchestration

여기서 `stream`은 더 이상 단순 후순위 참고 항목이 아니다. 현재 방향에서는
`STREAM`도 네 가지 직접 통합 축 중 하나다. 다만 `send/request`처럼 모든
프레임워크의 기본 업무 API로 똑같이 보이게 하기보다, 연결 수명과 packet 처리
성격을 드러내는 별도 handler 모델로 설명하는 편을 기본으로 본다.

## 7. 다음 문서

전체 문서 목록과 읽는 순서는 [README.ko.md](./README.ko.md)를 참고한다.
