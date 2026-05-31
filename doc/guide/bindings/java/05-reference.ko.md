[← 운영](./04-operations.ko.md) · [Java 가이드](./index.ko.md)

# 레퍼런스

에러 처리, 코덱, C API↔Java 대응표, API 레퍼런스 생성, 샘플 목록을 다룹니다.

---

## 에러 처리

Java 바인딩은 `ZlinkException` 계층 구조로 예외를 던집니다.

```java
try (Message msg = Message.from("data")) {
    socket.send().message(msg).submit();
} catch (ZlinkSubmitException e) {
    switch (e.getResult()) {
        case BACKPRESSURED -> { /* 잠시 후 재시도 */ }
        case NOT_CONNECTED -> { /* 연결된 피어 없음 */ }
        default -> throw e;
    }
}
```

예외 타입:

| 예외 클래스 | 발생 시점 | 결과 필드 |
|------------|----------|-----------|
| `ZlinkSubmitException` | 전송/발행 실패 | `getResult(): SubmitResult` |
| `ZlinkRequestException` | 요청 실패 | `getResult(): RequestResult` |
| `ZlinkRecvException` | 수신 실패 | `getResult(): RecvResult` |
| `ZlinkBindException` | 바인드 실패 | `getResult(): BindResult` |
| `ZlinkConnectException` | 연결 실패 | `getResult(): ConnectResult` |
| `ZlinkConfigException` | 옵션 설정 실패 | `getResult(): ConfigResult` |
| `ZlinkCloseException` | 닫기 실패 | `getResult(): CloseResult` |
| `ZlinkHandlerException` | 핸들러 등록 실패 | `getResult(): HandlerResult` |

모든 예외는 `ZlinkException`을 상속하며, `getCode()`와 `getInternalErrno()`로
네이티브 코드를 확인할 수 있습니다.

---

## 코덱

Java 바인딩은 선택적 코덱 모듈을 제공합니다.

**Gradle:**
```groovy
dependencies {
    implementation 'systems.zlink:zlink-codec-json:6.0.4'
    implementation 'systems.zlink:zlink-codec-messagepack:6.0.4'
    implementation 'systems.zlink:zlink-codec-protobuf:6.0.4'
}
```

---

## C API ↔ Java 대응표

| C API | Java API |
|-------|----------|
| `zlink_ctx_new()` | `Zlink.createContext()` |
| `zlink_ctx_term()` | `ctx.close()` |
| `zlink_socket(ctx, type)` | `ctx.createPairSocket()` 등 |
| `zlink_close(socket)` | `socket.close()` |
| `zlink_bind(socket, ep)` | `socket.bind(ep)` |
| `zlink_connect(socket, ep)` | `socket.connect(ep)` |
| `zlink_send_part(...)` | `socket.send().message(m).submit()` |
| `zlink_recv(...)` | `socket.recv(received, flags)` |
| `zlink_msg_data(msg)` | `msg.data()` |
| `zlink_msg_size(msg)` | `msg.size()` |
| `zlink_msg_close(msg)` | `msg.close()` |
| `zlink_routing_id_t` | `RoutingId` |
| `zlink_socket_monitor_open(...)` | `socket.monitorOpen(...)` |
| `zlink_poller_new()` | `Zlink.createPoller()` |
| `zlink_timer_new()` | `Zlink.createTimer()` |
| `zlink_spot_node_new(ctx)` | `ctx.createSpotNode()` |
| `zlink_spot_node_create_spot(...)` | `node.createSpot()` |
| `zlink_spot_node_actor_new(...)` | `node.createActor("id")` |
| `zlink_registry_new(ctx)` | `ctx.createRegistry()` |
| `zlink_discovery_new(ctx, ...)` | `ctx.createDiscovery(type, channel)` |

---

## API 레퍼런스 생성

Javadoc을 로컬에서 생성합니다:

```bash
cd bindings/java
./gradlew javadoc
# bindings/java/build/docs/javadoc/index.html 에서 확인
```

---

## 샘플 목록

`bindings/java/samples/Zlink.Samples/src/main/java/systems/zlink/samples/` 에 있는
검증된 샘플 코드입니다.

| 샘플 클래스 | 설명 |
|------------|------|
| `PairRecvSample` | PAIR 소켓 송수신 |
| `DealerRouterRecvSample` | DEALER/ROUTER 송수신 |
| `RequestReplyAsyncSample` | 비동기 요청/응답 |
| `PubSubRecvSample` | PUB/SUB 발행·구독 |
| `StreamRecvSample` | STREAM 원시 TCP |
| `StreamPacketCallbackSample` | STREAM 패킷 콜백 |
| `MonitorRecvSample` | 모니터 이벤트 수신 |
| `DiscoveryRegistrySample` | Registry + Discovery 기본 |
| `RegistryQuerySample` | Registry 토폴로지 쿼리 |
| `SpotRecvSample` | SpotNode/Spot PUB/SUB |
| `SpotRequestAsyncSample` | SpotNode 비동기 요청 |
| `ActorSinglePlayerQueueSample` | 액터 조인/이동/메시지 큐 |
| `ActorRoomServerSample` | 방 서버 액터 패턴 |
| `ActorGatewayRelaySample` | 게이트웨이 릴레이 |

샘플 빌드 및 실행:

```bash
cd bindings/java
./gradlew :samples:build
./gradlew :samples:run -PmainClass=systems.zlink.samples.PairRecvSample
```
