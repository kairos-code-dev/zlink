[스펙 목차](../../README.ko.md)

# Draft -- ZLink Framework Service Topology

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, topology를 사용자에게 어떻게 숨기고 내부에 어떻게
> 매핑할지 방향을 설명한다.

## 1. 목적

`ZLink Framework`는 zlink의 raw topology를 없애는 계층이 아니라, 그것을
**서비스 단위 개념으로 다시 묶는 계층**이다.

즉 내부 구현은 `DEALER`, `ROUTER`, `PUB`, `SUB`, `SPOT`를 계속 쓸 수 있지만,
사용자에게는 아래 개념이 먼저 보여야 한다.

- service name
- request client
- message handler
- event publisher
- event subscriber

## 2. service grouping

현재 초안은 provider grouping의 기준을 `service_name`으로 본다.

예를 들면 아래처럼 묶는다.

- `api.profile`
- `api.inventory`
- `api.payment`
- `game.stage.sync`

클라이언트는 endpoint 주소보다 `service_name`을 먼저 기준으로 삼는다.
Discovery를 쓰면 같은 `service_name`에 속한 provider를 자동으로 찾고,
수동 연결을 쓰면 그 service의 provider 집합을 직접 설정한다.

이 점은 일반적인 웹 서버 호출 모델과 비교했을 때 중요한 차이점이다.
보통은 위치투명성을 주기 위해 API gateway 또는 그와 비슷한 중간 계층을 두고,
응용이 그 gateway를 향해 호출하거나, 전용 load balancer 뒤의 주소를 호출한다.
현재 초안은 그보다 아래처럼 가는 편을 기본 방향으로 본다.

- 호출자는 gateway 주소 대신 `service_name`을 기준으로 요청한다.
- Discovery가 현재 provider 위치를 숨긴다.
- `ZLink Framework`가 적절한 provider를 골라 직접 요청을 보낸다.
- 즉 provider 선택이 client 쪽에서 이뤄질 수 있어야 한다.

즉 gateway나 전용 로드밸런서를 반드시 거쳐야만 location transparency와
provider selection을 얻는 구조를 전제로 하지 않는다.

## 3. 상호작용 모델과 topology 매핑 초안

| 공용 모델 | 내부 기본 매핑 초안 |
|-----------|---------------------|
| `request-response` | client side `DEALER`, server side `ROUTER` |
| `command` | `DEALER -> ROUTER` 또는 routed send |
| `publish-subscribe` | `PUB/SUB` 또는 `SPOT` |
| `worker-dispatch` | `DEALER -> ROUTER` |
| 고급 브리지 또는 서버 간 중계 | 필요하면 `ROUTER <-> ROUTER` |

## 4. playhouse use case에 대한 해석

`playhouse` 시나리오에서는 play 서버가 여러 api 서비스군에 요청을 보내야 할 수
있다. 이때 사용자가 생각하는 단위는 보통 socket이 아니라 서비스 client다.

예를 들면 아래처럼 보는 편이 자연스럽다.

- `profileClient`는 `api.profile` service group에 보낸다.
- `inventoryClient`는 `api.inventory` service group에 보낸다.
- api 서버는 각 service group에 대해 request handler를 제공한다.

내부적으로는 서비스마다 별도 outbound connection set이나 pool이 필요할 수
있지만, 그것은 `ZLink Framework`의 책임으로 두는 편이 낫다.

## 5. Discovery와 수동 연결

두 방식 모두 필요하다.

### 5.1 Discovery

- 운영 환경 기본값으로 적합하다.
- service_name 기준 provider grouping과 자동 갱신에 유리하다.

### 5.2 수동 연결

- 개발, 테스트, 단순 배포에서 유용하다.
- Discovery 없이도 같은 공용 API를 유지할 수 있다.

즉 공용 표면은 "Discovery 전용 API"보다 "client/server 등록 방식"을 먼저
보이고, 내부 연결 전략은 별도 설정으로 넣는 편이 좋다.

## 6. 범위 밖에 두는 것

- `DEALER <-> DEALER`를 공용 모델로 노출하는 일
- `ROUTER <-> ROUTER`를 일반 사용자용 기본 모델로 설명하는 일

이 둘은 필요해지면 고급 문서에서 다루되, 현재 `ZLink Framework` 초안의 중심에는 두지
않는다.
