<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework Spring Boot Registry](./spring-boot-registry.ko.md) | [다음: ZLink Framework Spring Boot STREAM](./spring-boot-stream.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](./README.ko.md)

[Java 묶음](../README.ko.md) | [포팅 계획](../draft/java-kotlin-framework-porting-plan.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Actor/session](./spring-boot-actor-session.ko.md) | [SPOT 샘플](../guide/samples/spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md)

# ZLink Framework Spring Boot SPOT

## 1. 방향

`SPOT`은 별도 raw runtime으로 노출하기보다, `Spring Boot` bean lifecycle 안에서
등록하고 관리하는 편을 기본으로 본다.

- `addSpotMesh(...).addRegistryEndpoint(...)` 기준의 discovery 등록
- `SpotNode` bean 생성과 capability별 등록
- current channel publish/subscribe와 attach된 channel client 경로
- local spot 인스턴스가 없는 외부 노드용 publisher client 경로
- Entry Spot과 user Spot factory
- accepted route channel과 Spot route egress
- 필요할 때만 spot-to-spot routed 호출 허용

현재 공통 정책 기준으로는 아래를 같이 지켜야 한다.

- `SpotNode`는 channel 이름을 직접 소유하지 않고, attach된 discovery view가 active
  channel 범위를 정한다.
- capability는 `router`, `pub/sub`, attach된 channel client, attach된 spot
  publisher client로 나눠서 설명한다.
- spot factory는 Spot type 기준으로 등록하고, 같은 Spot type 재등록은 덮어쓰지 않고
  예외로 본다.
- spot 생성은 Spot type 기준으로 설명하고, 운영 코드는 `spotRid`로 생성된 Spot을
  다시 조회할 수 있어야 한다.
- timer는 공용 scheduler보다 spot lifecycle registration 표면으로 두는 편이
  자연스럽다.
- 같은 runtime 안의 local managed session actor dispatch는 framework 내부 dispatch를
  사용한다. remote session actor dispatch는 Spot route channel이 아니라 ActorGateway
  attach를 사용한다.

## 2. 기본 등록

```java
@Configuration
@EnableZLinkFramework
public class SpotConfig implements ZLinkFrameworkOptionsCustomizer {
    @Override
    public void customize(ZLinkFrameworkOptions options) {
        options.addSpotMesh("game.stage", mesh -> {
            mesh.addRegistryEndpoint("tcp://registry1:5551");

            mesh.addNode("play", node -> {
                node.enableRouter(router -> {
                    router.bindRouter("tcp://0.0.0.0:9000");
                });
                node.enablePubSub(pubsub -> {
                    pubsub.bindPubSub("tcp://0.0.0.0:9001");
                });
                node.acceptSpotRoutesFromChannel("play-route");
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.of("play.entry")));
                node.addEntrySpot(GameEntrySpot.class);
                node.addSpotFactory(GameRoomSpot.class);
            });
        });
    }
}
```

## 3. Public surface

- `ZLinkSpotManager`
- `ZLinkEntrySpot`
- `ZLinkSpotActor*Handler`
- current channel publish/subscribe
- attach된 channel client를 통한 다른 channel send/request
- local spot 인스턴스가 없는 외부 노드용 `ZLinkSpotPublisherClient`
- 필요할 때만 `spot-to-spot` routed send/request

즉 high-level `SPOT` 표면은 `rid` 직접 지정보다 current channel publish와
cross-channel client를 먼저 설명하는 편이 맞다. 다만 실제 운영 코드가 Spot type으로
생성하고 `spotRid`로 다시 조회해야 하므로, `ZLinkSpotManager`도 public surface에
함께 둬야 한다.

## 4. Spot-to-spot

spot-to-spot routed 호출은 남긴다. 다만 일반 channel messaging과 섞지 않고,
advanced surface로 설명한다. 현재 SPOT 문맥의 `context.outbound()`(`ZLinkSpotOutbound`)
가 target SPOT `RoutingId` 하나만 받아 routed request 를 보낸다. target node 와 route
channel 해소는 `ZLinkSpotRemoteAddressResolver` 가 맡는다.

```java
context.outbound()
    .requestToSpot(targetSpotRid, request)
    .submitAsync(StageReply.class);
```

## 5. Entry Spot과 user Spot

actor를 지원하려면 SpotNode에는 Entry Spot과 user Spot factory가 함께 있어야 한다.
Entry Spot은 actor 생성 직후의 기본 위치이며, 인증이나 target user Spot 선택 같은
입구 로직을 맡는다. user Spot은 room, stage, zone 같은 도메인 상태를 보관한다.

Entry Spot timer는 Entry Spot 전체를 막는 전역 queue에 묶지 않는다. user Spot
timer는 같은 Spot의 packet, subscription, actor handler와 같은 실행 문맥 안에서
직렬화한다.
