<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Spring Boot Registry](spring-boot-registry.ko.md) | [다음: ZLink Framework Spring Boot STREAM](spring-boot-stream.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [Actor/session](spring-boot-actor-session.ko.md) | [SPOT 샘플](../guide/samples/spot-samples.ko.md) | [Stage wrapper](stage-wrapper-on-spot.ko.md)

# ZLink Framework Spring Boot SPOT

## 현재 구현 기준

외부 route channel에서 특정 Spot으로 들어오는 send/request는 framework가 core
`SpotRouteBridge`를 내부에서 사용해 자동으로 연결한다. Java framework runtime은
`bindings/java`의 public `createRouteBridge()` / `SpotRouteBridge` 표면으로
같은 프로세스의 RouteMesh channel socket을 bridge에 연결한다. channel socket은
channel runtime이 계속 소유하며, bridge는 SPOT relay packet만 분류한다. local
`SpotNode` topic plane으로 외부 publish가 필요하면 raw `PUB` attach가 아니라 public
publisher handle을 사용한다.

## 1. 방향

`SPOT`은 별도 raw runtime으로 노출하기보다, `Spring Boot` bean lifecycle 안에서
등록하고 관리하는 편을 기본으로 본다.

- root `useDiscovery().addRegistryEndpoint(...)` 기준의 discovery 등록
- spot node 설정 등록과, 그에 따른 `ZLinkSpotManager`/`ZLinkSpotOutbound` 등 capability bean 조건부 노출
- current channel publish/subscribe와 route bridge channel socket 경로
- local spot 인스턴스가 없는 외부 노드용 publisher client 경로
- Entry Spot과 user Spot factory
- 같은 프로세스의 RouteMesh channel과 SpotNode 자동 연결
- 필요할 때만 spot-to-spot routed 호출 허용

현재 공통 정책 기준으로는 아래를 같이 지켜야 한다.

- `SpotNode`는 channel 이름을 직접 소유하지 않고, attach된 discovery view가 active
  channel 범위를 정한다.
- 역할은 `router`, `pub/sub`, route bridge channel socket, attach된 spot
  publisher client로 나눠서 설명한다.
- spot factory는 Spot type 기준으로 등록하고, 같은 Spot type 재등록은 덮어쓰지 않고
  예외로 본다.
- spot 생성은 Spot type 기준으로 설명하고, 운영 코드는 `spotRid`로 생성된 Spot을
  다시 조회할 수 있어야 한다.
- timer는 공용 scheduler보다 spot lifecycle registration 표면으로 두는 편이
  자연스럽다.
- 같은 runtime 안의 local managed session actor dispatch는 framework 내부 dispatch를
  사용한다. remote session actor dispatch는 Spot route channel이 아니라 SessionRelay
  attach를 사용한다.

## 2. 기본 등록

```java
@Configuration
@EnableZLinkFramework
public class SpotConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.useDiscovery().addRegistryEndpoint("tcp://registry1:5551");
        ZLinkSpotMeshBuilder node = framework.addSpotMesh("game.stage");
        node.enableRouter("tcp://0.0.0.0:9000");
        node.enablePubSub("tcp://0.0.0.0:9001");
        node.configureEntrySpot()
            .setRoutingId(RoutingId.from("play.entry"));
        node.addEntrySpot(GameEntrySpot.class);
        node.addSpotFactory(GameRoomSpot.class);
    }
}
```

## 3. Public surface

- `ZLinkSpotManager`
- `ZLinkEntrySpot`
- `ZLinkSpotActor*Handler`
- current channel publish/subscribe
- route bridge channel socket을 통한 다른 channel send/request
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
    .submit(StageReply.class);
```

## 5. Entry Spot과 user Spot

actor를 지원하려면 SpotNode에는 Entry Spot과 user Spot factory가 함께 있어야 한다.
Entry Spot은 actor 생성 직후의 기본 위치이며, 인증이나 target user Spot 선택 같은
입구 로직을 맡는다. user Spot은 room, stage, zone 같은 도메인 상태를 보관한다.

Entry Spot actor packet은 대상 actor의 mailbox에서 순서대로 처리된다. 같은 actor의
packet은 겹치지 않지만, 서로 다른 actor의 Entry Spot actor packet은 Entry Spot 하나의
실행 줄 때문에 서로 기다리지 않는다. Entry Spot lifecycle callback은 Entry Spot 자체의
입구 정책을 다루므로 actor mailbox로 옮기지 않는다.

user Spot의 message dispatch는 Spot 단위 실행 문맥 하나를 기준으로 직렬화한다.
route packet, subscription packet, user Spot actor packet, actor lifecycle callback,
framework managed timer callback은 같은 Spot 안에서 동시에 실행되지 않는다. handler가
`CompletionStage`를 반환하면 framework는 그 stage가 끝난 뒤 같은 Spot의 다음 dispatch를
시작한다. 이 규칙은 Java handler와 Kotlin `suspend fun` annotation handler에 같이
적용된다. Kotlin handler는 framework 소유 coroutine adapter에서 실행되고, adapter가
반환한 `CompletionStage`가 Spot serial queue의 완료 기준이 된다.

Entry Spot actor handler는 Entry Spot 인자를 받지만 Entry Spot 전체 실행 줄에 들어가지
않는다. handler는 actor별 상태를 다루는 곳이며, Entry Spot 객체의 가변 필드를 여러
actor가 공유하는 동기화 수단으로 쓰면 안 된다. Entry Spot lifecycle callback과 route 같은
Entry Spot 자체 상태 흐름은 별도 Entry Spot 실행 문맥에서 처리한다.

기본 `submit(...)`/`await(...)` 경로는 actor별 순서를 유지한다. `yield(...)`는
request, Spot outbound request, actor `joinSpot` / `joinEntrySpot`, bound session send
completion, worker completion에서만 현재 mailbox turn을 반납하고 completion 뒤 원래
mailbox에서 재개한다. `yield(...)` 중에도 같은 actor와 같은 timer는 재진입하지 않는다.
다른 actor나 다른 timer 작업은 interleave될 수 있으므로, await 전후에 공용 가변 상태를
이어 판단하는 handler는 기본 `await(...)`를 사용해야 한다.
Entry Spot actor handler 안에서 만든 call object의 `yield(...)`는 허용하지 않는다.
호출하면 시간 초과가 아니라 즉시 `IllegalStateException` 같은 계약 오류가 나야 한다.
request, actor join, worker completion의 cancellation-aware overload는 handler가 받은
`CancellationToken`을 대기 작업에 전달한다. token이 이미 취소되었거나 대기 중 취소되면
operation은 `ZLinkOperationCanceledException`을 cause로 둔 실패로 끝나고, handler continuation은
같은 mailbox 경로에서 그 오류를 관찰한다. `yield(...)` 같은 동기 terminator는 Java
`CompletableFuture.join()` 규칙에 따라 이 오류를 `CompletionException`으로 감싸서 던질 수 있다.

SPOT route request 에 handler 가 없거나 payload decode, handler 예외, invalid frame 이 발생하면 reply
path 가 있는 경우 error reply 를 반환한다. actor request 도 같은 원칙을 따른다. 같은 process 안의
local actor call 처럼 reply frame 이 없는 경로는 `CompletionStage` 를 framework error 로 완료한다.

SPOT route send, subscription, actor send 는 reply 를 만들 수 없으므로 실패한 메시지를 drop 한다.
route send 와 actor send 는 Warning 로그와 counter, subscription 은 Debug 로그 또는 counter 와
`outcome=ERROR` 메시지 흐름 이벤트를 남긴다. observer 실패는 dispatch loop 나 shutdown 을 깨지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Spring Boot Registry](spring-boot-registry.ko.md) | [다음: ZLink Framework Spring Boot STREAM](spring-boot-stream.ko.md)
<!-- framework-adapter-nav:bottom:end -->
