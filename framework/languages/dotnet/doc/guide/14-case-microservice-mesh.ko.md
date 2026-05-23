<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: 케이스 — 전자상거래 체크아웃](./13-case-ecommerce-checkout.ko.md) | [다음: 케이스 — 실시간 멀티플레이 게임](./15-case-realtime-game.ko.md)
<!-- framework-adapter-nav:end -->

# 케이스 — 내부 마이크로서비스 mesh + 운영

> [12-grpc-alternative](./12-grpc-alternative.ko.md)의 케이스 스터디 중 하나다.
> 다수 내부 서비스가 서로 호출(BFF aggregation 포함)하고, 운영에서 클러스터
> topology 를 들여다봐야 하는 상황을 다룬다.

## 1. 시나리오와 기존 스택

수십~수백 개의 내부 서비스가 gRPC/REST 로 서로 호출하고, gateway/BFF 는 요청 하나로
여러 서비스를 fan-out 한다. 운영은 호출 분배·위치 해결·관측을 위해 **service
mesh(Envoy/Istio) + service discovery(Eureka/Consul/xDS) + 별도 텔레메트리 수집**을
얹는다. 서비스 수가 늘수록 의존성·토폴로지 파악이 별도 과제가 된다.
([scaling microservices](https://www.netguru.com/blog/scaling-microservices),
[Netflix service mesh](https://vivekbansal.substack.com/p/system-design-study-netflixs-adoption))

## 2. ZLink 구성

| | 기존 스택 | ZLink |
|---|-----------|-------|
| 호출 | gRPC unary + stub(서비스별) | `IZLinkClient.Request/Send`(channel 이름만) |
| 위치 해결 | Eureka/Consul/xDS | `UseDiscovery(...)` + Registry |
| 부하 분배 | Envoy/Istio sidecar(L7) | channel `Discovery` 가 peer 분배 |
| 관측 | mesh telemetry + 별도 수집 | `AddZLinkMonitoring(...)` + `IZLinkRegistryQuery` topology 조회 |

서비스가 늘어도 응용은 `Request("pricing", ...)` 처럼 **channel 이름만** 안다.
어디에 몇 개 떠 있는지는 Registry view 가 숨긴다. BFF fan-out 도 같은
`IZLinkClient` 하나로 channel 이름만 바꿔 호출한다(응답 필요하면 `Request`, 통지면
`Send`).

운영 점검은 sidecar telemetry 대신 in-process 로 한다.

```csharp
// 클러스터 topology 를 그대로 관리 화면에 노출
app.MapGet("/admin/topology", async (IZLinkRegistryQuery registry) =>
    Results.Ok(await registry.TopologySnapshotAsync()));
```

runtime 변화(socket connect/disconnect, registry status/topology)는
`AddZLinkMonitoring(...)` 으로 기존 로깅/메트릭에 합류시킨다
([09-monitoring](./09-monitoring.ko.md)).

## 3. 사라지는 인프라 / 경계

- **사라지는 것:** Envoy/Istio sidecar, mesh control plane, 별도 discovery 컴포넌트,
  mesh telemetry 수집 파이프라인.
- **경계:** 서드파티가 부르는 **외부 공개 API** 는 REST/gRPC 가 표준이다. 영속
  데이터·이력은 DB 가 맡는다. 공통 경계는
  [12-grpc-alternative](./12-grpc-alternative.ko.md)의 "솔직한 경계" 절 참고.

## 4. 핵심 강점

sidecar/control plane/별도 discovery 없이 **channel name + Registry 한 겹**으로
mesh 의 호출·분배·관측을 가져간다. 운영자는 topology 를 in-process
`IZLinkRegistryQuery`([08-registry](./08-registry.ko.md))로 직접 조회한다.

## 5. 더 보기

- 케이스 허브: [12-grpc-alternative](./12-grpc-alternative.ko.md)
- 사용법: [04-channel-messaging](./04-channel-messaging.ko.md), [08-registry](./08-registry.ko.md), [09-monitoring](./09-monitoring.ko.md)
- 다음 케이스: [15-case-realtime-game](./15-case-realtime-game.ko.md)
