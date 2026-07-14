<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: Framework 공개 계약 관리](00-public-contract-governance.ko.md) | [다음: ZLink Framework Interaction Model](02-interaction-model.ko.md)
<!-- framework-adapter-nav:end -->


[문서 묶음](../common/README.ko.md) | [상호작용 모델](02-interaction-model.ko.md) | [메시지 모델](03-message-model.ko.md) | [channel topology](server/10-channel-topology.ko.md) | [framework API](05-framework-api.ko.md) | [공통 sample](../common/sample/README.ko.md) | [공통 E2E](../common/e2e/README.ko.md) | [.NET](../dotnet/README.ko.md) | [Java](../java/README.ko.md) | [Node.js](../node/README.ko.md) | [C++](../cpp/README.ko.md)

# ZLink Framework Overview

## 1. 한 줄 정의

`ZLink Framework`는 zlink 바인딩 위에 올라가서, 기존 애플리케이션 프레임워크에서
**gateway나 전용 로드밸런서 없이도** `channel name` 기준의 직접 channel 호출,
pub/sub, `SPOT`, `STREAM`, location store 기반 자동 연결을 사용할 수 있게 하는 상위
계층이다.

## 2. 무엇을 제공하는가

`ZLink Framework`는 아래 기능을 하나의 방향으로 묶는다.

- server-to-server send/request
- channel messaging integration
- pub/sub integration
- spot integration
- stream integration
- channel별 location store 기반 자동 연결([location runtime](server/40-location-runtime.ko.md))
- runtime monitoring
- location runtime query (원시 row, runtime이 합성한 topology 보기, status 운영 조회)
- framework-friendly handler / client / event API

raw socket과 low-level 연결 배선을 프레임워크 사용자가 직접 다루지 않고도
기존 HTTP나 gRPC를 쓰던 감각에 가까운 개발 모델을 제공하는 것이 목표다.
다만 내부에서 무엇을 쓰는지는 숨기더라도 framework가 실제로 통합할 transport
축 자체는 명확해야 한다.

현재 이 문서는 아래 네 축을 직접 통합 대상으로 본다.

1. channel messaging
   `DEALER(client) -> ROUTER(server)`
2. `SPOT`
3. `PUB/SUB`
4. `STREAM`

그 위에 사용자 경험은 아래처럼 다시 올린다.

- 서버 간 `send`
- 서버 간 `request`
- pub/sub
- `SPOT` named instance 생성/조회와 현재 channel 안의 publish/subscribe
- route bridge channel socket을 통한 cross-channel send/request
- local spot 인스턴스가 없는 외부 노드용 SPOT channel publish client
- stream session
- socket/location runtime/spot runtime event

transport 축은 사용자에게 그대로 노출하지 않더라도, 내부 wire 경계는 언어별
adapter가 공통으로 지켜야 한다.

- 서버 간 framework message (`DEALER/ROUTER`, routed channel, `SPOT` channel,
  internal actor dispatch, internal session proxy)는 multipart `header + payload`를
  사용한다.
- `STREAM`은 하나의 stream packet을 기본 단위로 사용한다. stream header와 payload는 그
  packet 안의 frame으로 다룬다.

이 구분은 성능과 소유권을 위한 기본 정책이다. 서버 간 body를 header와 함께 단일
직렬화 envelope로 묶으면 body를 다시 복사하거나 재인코딩하게 되고 route나 dispatch가
header만 읽는 장점도 잃는다.

## 3. 핵심 차별점

일반적인 웹 서버 환경에서는 위치투명성과 provider 선택을 위해 아래 중 하나를
두는 경우가 많다.

- API gateway
- 전용 load balancer
- service proxy

`ZLink Framework`는 이와 다른 방향을 기본으로 본다.

- 호출자는 gateway 주소 대신 `channel name`을 기준으로 요청한다.
- framework runtime이 channel마다 별도 outbound socket을 만든다.
- location store 기반 자동 연결이 그 channel view 안의 provider 위치를 숨긴다.
- framework는 그 channel 안의 `rid` 집합과 연결 상태를 기준으로 요청을 보낸다.
- 요청은 중간 gateway 없이 provider로 직접 간다.

"위치투명성을 얻으려면 반드시 gateway를 거쳐야 한다"는 전제를 두지 않는다.

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

`ZLink Framework`는 곧바로 edge API gateway 제품이 아니라
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

현재 스펙이 우선 다루는 상호작용 모델은 아래와 같다.

- request-response
- command
- publish-subscribe
- stream

현재 방향에서는 `STREAM`도 네 가지 직접 통합 축 중 하나다. 다만 `send/request`처럼 모든
프레임워크의 기본 업무 API로 똑같이 보이게 하기보다, 연결 수명과 packet 처리
성격을 드러내는 별도 handler 모델로 설명하는 편을 기본으로 본다.

## 7. 다음 문서

전체 문서 목록과 읽는 순서는 [README.ko.md](../common/README.ko.md)를 참고한다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](README.ko.md) | [이전: Framework 공개 계약 관리](00-public-contract-governance.ko.md) | [다음: ZLink Framework Interaction Model](02-interaction-model.ko.md)
<!-- framework-adapter-nav:bottom:end -->
