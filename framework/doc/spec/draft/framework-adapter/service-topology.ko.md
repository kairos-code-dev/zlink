[스펙 목차](../../README.ko.md)

# Draft -- ZLink Framework Service Topology

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, topology를 사용자에게 어떻게 숨기고 내부에 어떻게
> 매핑할지 방향을 설명한다.

## 1. 목적

`ZLink Framework`는 zlink의 raw topology를 없애는 계층이 아니라, 그것을
**서비스 단위 개념으로 다시 묶는 계층**이다.

즉 내부 구현은 `ROUTER`, `PUB`, `SUB`, `SPOT`, `STREAM`를 계속 쓸 수 있지만,
사용자에게는 아래 개념이 먼저 보여야 한다.

- service name
- request client
- message handler
- event publisher
- event subscriber
- stream handler

## 2. service grouping

현재 초안은 provider grouping의 기준을 `service_name`으로 본다.

예를 들면 아래처럼 묶는다.

- `api.profile`
- `api.inventory`
- `api.payment`
- `game.stage.sync`

클라이언트는 endpoint 주소보다 `service_name`을 먼저 기준으로 삼는다.
현재 방향에서는 framework runtime도 이 기준을 그대로 따른다. 즉 outbound 요청
경로는 "노드 하나에 모든 service를 합쳐 관리하는 연결"보다, "접근하려는
service마다 별도 channel을 두는 구조"를 기본으로 본다.

- `api.profile`에 처음 요청하면 `api.profile` 전용 channel을 만든다.
- 이 channel은 그 service view에 묶인 `Discovery`와 outbound socket을 가진다.
- 같은 `service_name`에 속한 provider 집합은 discovery가 자동으로 갱신한다.
- framework는 그 service에 대한 `rid` 집합과 상태만 관리하면 된다.

수동 연결을 쓰면 그 service의 provider 집합을 직접 설정한다. 운영 점검이나 제어
plane에서는 Registry topology snapshot/query 또는 원격 `RegistryQueryClient`로
전체 provider 집합을 읽을 수도 있다.

이 구조가 일반적인 gateway 기반 호출 모델과 어떻게 다른지, 왜 gateway 없이도
location transparency를 얻을 수 있는지는 [overview.ko.md](./overview.ko.md)의
section 3을 참고한다.

## 3. 상호작용 모델과 topology 매핑 초안

| 공용 모델 | 내부 기본 매핑 초안 |
|-----------|---------------------|
| `request-response` | `ROUTER <-> ROUTER`, 필요하면 routed `SPOT` request/reply |
| `command` | `ROUTER <-> ROUTER` send 또는 `SPOT` routed send |
| `publish-subscribe` | `PUB/SUB` 또는 `SPOT` |
| `stream` | `STREAM` |
| `worker-dispatch` | 별도 조합 모델 |

## 4. playhouse use case에 대한 해석

`playhouse` 시나리오에서는 play 서버가 여러 api 서비스군에 요청을 보내야 할 수
있다. 이때 사용자가 생각하는 단위는 보통 socket이 아니라 서비스 client다.

예를 들면 아래처럼 보는 편이 자연스럽다.

- `IZLinkClient`가 `api.profile`에 요청하면 framework는 `api.profile` channel을 쓴다.
- `IZLinkClient`가 `api.inventory`에 요청하면 framework는 `api.inventory` channel을 쓴다.
- api 서버는 각 service group에 대해 request handler를 제공한다.

즉 outward API는 공용 client 하나로 보이더라도, 내부 runtime은 service마다 별도
channel과 socket을 가질 수 있다. 현재 초안은 이 service별 channel 구조를 기본
방향으로 본다.

## 5. Discovery와 수동 연결

두 방식 모두 필요하다.

### 5.1 Discovery

- 운영 환경 기본값으로 적합하다.
- service_name 기준 provider grouping과 자동 갱신에 유리하다.
- 각 service 전용 channel이 자기 service view를 독립적으로 유지하기에 적합하다.
- 일반 요청 경로에서는 다른 service topology를 매번 조회하지 않고, 현재 service
  view와 `rid` 집합만 보면 된다.

### 5.2 수동 연결

- 개발, 테스트, 단순 배포에서 유용하다.
- Discovery 없이도 같은 공용 API를 유지할 수 있다.

### 5.3 Registry topology query

- 운영 점검, warm-up, 관리 화면, 디버깅에 유용하다.
- Discovery가 지금 보고 있는 개별 service view 밖의 전체 상태를 읽을 수 있다.
- 다만 일반 요청 경로의 service channel 생성과 실시간 요청 분산을 모두 topology
  query에 의존하는 구조는 기본 방향으로 보지 않는다.

즉 공용 표면은 "client/server 등록 방식"을 먼저 보이고, 내부 구현은
"service별 Discovery + service별 outbound channel"을 기본으로 두는 편이 좋다.

## 6. 범위 밖에 두는 것

- `DEALER <-> DEALER`를 공용 모델로 노출하는 일
- raw multipart header를 application handler 인자로 직접 노출하는 일

이 둘은 필요해지면 고급 문서에서 다루되, 현재 `ZLink Framework` 초안의 중심에는 두지
않는다.
