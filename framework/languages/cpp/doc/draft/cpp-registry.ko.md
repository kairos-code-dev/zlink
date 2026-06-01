<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ Monitoring](./cpp-monitoring.ko.md) | [다음: Draft -- ZLink Framework C++ SPOT](./cpp-spot.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md)

# Draft -- ZLink Framework C++ Registry

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` runtime에서 Registry를 어떤 표면으로 통합할지
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

## 2. Registry-backed Spot lookup

Registry 기본 구현은 Spot owner 조회와 Spot RID directory를 돕는다.

`C++` framework는 아래 표면을 제공해야 한다.

```cpp
app.use_zlink([](auto &zlink) {
    zlink.node("play-node")
      .discovery([](auto &discovery) {
          discovery.connect_registry("tcp://registry:5551");
      })
      .spot_node("play-actors", [](auto &spot_node) {
          spot_node.use_registry_spot_remote_addresses("game.route");
      });
});
```

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
