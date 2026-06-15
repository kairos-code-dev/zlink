<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Spec -- ZLink Framework C++ Monitoring](./cpp-monitoring.ko.md) | [다음: Spec -- ZLink Framework C++ SPOT](./cpp-spot.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](../README.ko.md)

# Spec -- ZLink Framework C++ Registry

> 이 문서는 **구현 완료된 설계 계약**이다.
> `C++` runtime에서 Registry를 어떤 표면으로 통합할지
> 정리한다.

## 인터페이스 경계

Registry public contract는 `contracts/registry/*`가 소유한다. embedded registry option,
topology query model, registry query client 표면은 public contract가 될 수 있다. backend
discovery factory, route resolver cache, registry payload codec, query transport owner는
`src/runtime/registry/*`에 둔다.

Registry는 운영 점검과 route lookup 보조 기능이다. 일반 request hot path가 registry
runtime 구현 타입이나 backend query state에 의존하도록 public API를 만들지 않는다.

## 1. 방향

`C++` host는 아래 registry 표면을 가져야 한다.

- embedded registry bootstrap
- in-process topology query
- remote registry query client

일반 request 핫패스는 각 channel discovery view를 기준으로 설명하고, registry query는
운영 점검과 topology snapshot 용도로 분리한다.

현재 C++ public query 표면은 in-process snapshot query를 먼저 제공한다. `.NET`의
`ZLinkRegistryServiceSummaryFilter`와 `ZLinkRegistryTopologyFilter`에 대응하는 C++ value
filter를 사용하며, filter 의미는 호출자 반복문이 아니라 registry query module이 소유한다.

```cpp
zlink::framework::registry_query_t query = zlink.registry_query();

zlink::framework::service_summary_filter_t service_filter;
service_filter.name = "game.route";
service_filter.kind = zlink::framework::service_kind_t::channel;
auto services = query.service_summary(service_filter);

zlink::framework::topology_filter_t topology_filter;
topology_filter.name = "play-actors";
topology_filter.kind = zlink::framework::service_kind_t::spot;
topology_filter.state = zlink::framework::topology_state_t::active;
auto topology = query.topology(topology_filter);
```

무인자 `service_summary()`와 `topology()`는 전체 snapshot을 반환한다. 특정 서비스나 topology
상태만 필요하면 filter overload를 사용한다. 이렇게 해야 channel 이름, role, source/state
비교 규칙이 application code에 반복되지 않는다.

Remote registry query client는 `.NET`의 `IZLinkRegistryQueryClient`에 대응한다. 사용자는
native context나 query socket을 만들지 않고 endpoint만 지정한다. client는 backend transport,
native filter 변환, registry topology model 변환을 내부에 숨기고 framework의
`topology_entry_t`와 `result_t`로 반환한다.

```cpp
zlink::framework::registry_query_client_t client;
auto connected = client.connect(
  zlink::framework::registry_query_client_options_t {
    .endpoint = "tcp://127.0.0.1:5551"
  });

zlink::framework::topology_filter_t filter;
filter.name = "play";
auto topology = client.topology(filter);
```

연결되지 않은 client의 topology 조회는 `disconnected` result로 닫힌다. endpoint가 비어 있으면
`request_protocol_error`로 실패한다. 이 규칙은 native binding 예외를 application code가 직접
알지 않아도 되게 하기 위한 것이다.

## 2. Registry-backed Spot lookup

Registry 기본 구현은 Spot owner 조회와 Spot RID directory를 돕는다.

`C++` framework는 `.NET`의 `UseRegistrySpotRemoteAddresses`,
`AddRouteMeshChannel`, `AddSpotMesh`에 대응하는 아래 표면을 제공한다. 사용자는
낮은 수준 `zlink_builder_t::spot_node(...)`를 직접 열지 않고 framework options에서
registry, route mesh, spot mesh를 한 번에 표현한다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.use_discovery().add_registry_endpoint ("tcp://registry:5551");
    options.use_registry_spot_remote_addresses("game.route");
    options.add_route_mesh_channel("game.route")
      .bind("tcp://0.0.0.0:7200")
      .set_routing_id(zlink::routing_id_t::from("7200"))
      .connect("tcp://peer:7201");
    options.add_spot_mesh("game.spots")
      .add_node("play-actors")
      .enable_router("tcp://0.0.0.0:7300")
      .accept_routes_from_channel("game.route");
});
```

registry discovery를 쓰지 않고 accepted route peer를 직접 지정할 때는 route별 manual endpoint를
준다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.add_client_server_channel("api")
      .enable_server("tcp://0.0.0.0:7001");
    options.spot_node("play-actors")
      .enable_router("tcp://0.0.0.0:7300", [](auto &router) {
          router.connect("tcp://127.0.0.1:7301");
      })
      .accept_routes_from_channel("api", [](auto &routes) {
          routes.connect("tcp://0.0.0.0:7001");
      });
});
```

`enable_router(..., configure)`의 manual endpoint는 SPOT router 역할 peer다.
`accept_routes_from_channel(..., configure)`의 manual endpoint는 accepted route channel peer다.
둘은 같은 endpoint 문자열을 쓸 수 있어도 서로 다른 설정 의도를 표현하므로 하나의 필드로 합치지
않는다.

중요한 제한은 다음과 같다.

- Registry Spot 기본값과 custom Spot resolver를 동시에 등록하면 시작 오류로 본다.
- Spot discovery 없이 Registry Spot 기본값을 켜면 validation 오류로 본다.
- route channel이 둘 이상이면 resolver가 사용할 channel 이름을 명시해야 한다.
- 생성된 Spot은 `routing_id_t` 기준으로 조회할 수 있어야 한다.

## 3. Actor relay와 분리

session actor relay는 Registry actor route lookup을 hot path로 사용하지 않는다.

- STREAM session은 `attach_actor_gateway(...)`로 local SpotNode에 붙는다.
- session bind는 local actor handle 또는 remote actor ref를 사용한다.
- actor-session binding은 framework/core runtime state로 유지한다.
- sample-only route store 또는 metadata store를 두지 않는다.

이 분리는 session relay가 Registry 조회 지연이나 sample 전용 저장소에 묶이지 않게
하기 위한 것이다. Registry는 Spot remote address와 topology 관찰을 돕고, session
packet relay는 ActorGateway 경로가 맡는다.

## 4. 회귀 테스트

Registry 회귀 테스트는 `.NET` framework의 discovery와 route lookup 보조 기능을 C++
framework 표면으로 고정한다. Registry는 일반 request hot path의 필수 저장소가 아니므로,
lookup 성공뿐 아니라 잘못된 의존이 생기지 않는지도 검증한다.

현재 구현으로 고정한 항목:

- embedded registry bootstrap이 app host lifecycle에 맞춰 start/stop된다.
- `use_registry_spot_remote_addresses(...)`는 Spot discovery 설정이 있을 때만 허용된다.
- Registry Spot 기본값과 custom Spot resolver를 동시에 등록하면 startup validation이
  실패한다.
- route channel이 둘 이상이면 resolver channel 이름을 명시하지 않은 구성이 실패한다.
- Spot RID directory와 remote address snapshot이 바뀌면 resolver cache가 stale address를
  계속 사용하지 않는다.
- duplicate resolver rejection과 ambiguous route channel validation은 `.NET`과 같은 의미의
  framework error로 보고한다.
- remote registry query client는 topology snapshot을 반환하고, 연결되지 않은 상태와 잘못된
  endpoint를 caller result로 남긴다.
- Registry snapshot event는 등록된 monitoring source에만 전달되고, topology나 service summary
  변화가 있으면 각각 typed monitoring event로 올라간다.
- ActorGateway session binding은 Registry row나 sample-only metadata store에 저장하지
  않는다.

다음 항목은 별도 runtime polling worker와 log sink가 연결될 때 확장한다. 현재 draft에서는
정식 공개 계약처럼 쓰지 않는다.

- remote registry query timeout을 caller result로 돌려주고 monitoring event로 남기는 정책.
- Registry polling worker가 설정된 interval마다 snapshot을 읽고 file log와 monitoring event에
  같은 correlation id를 남기는 정책.

CTest label은 `framework-zlink-registry`를 사용한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Spec -- ZLink Framework C++ Monitoring](./cpp-monitoring.ko.md) | [다음: Spec -- ZLink Framework C++ SPOT](./cpp-spot.ko.md)
<!-- framework-adapter-nav:bottom:end -->
