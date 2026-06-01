[English](./07-4-actor.md) | [한국어](./07-4-actor.ko.md)

# SPOT Actor 사용 가이드

이 문서는 Actor의 핵심 흐름을 **9개 언어 코드 탭**으로 보인다. 개념(역할·언제
쓰나)은 언어 무관이라 탭 밖에 한 번 설명하고, 코드는 탭에서 자기 언어만 본다.
Kotlin은 Java 바인딩 런타임을, JavaScript는 Node 바인딩 런타임을 공유하지만
언어가 다르므로 별도 탭으로 보인다.

!!! note "파일럿 문서"
    이 문서는 [문서화 원칙](../../../principal/documentation/documentation-principles.ko.md)에
    따라 9언어 탭 형식을 시험 적용한 첫 문서다. 코드는 각 언어의
    `samples/actor_single_player_queue` 샘플에서 가져왔다(테스트용 동기화·검증
    코드는 가독성을 위해 생략). 정확한 함수 계약은
    [SPOT spec](../spec/core/service/spot.ko.md)을 본다.

## Actor란 — 무엇이고 언제 쓰나

Actor는 Spot에 합류(join)해 그 Spot으로 들어온 메시지를 받는 **상태 보유
엔티티**다(세션·게임 플레이어·작업 큐). 핵심은 **세션 위치와 처리 단위의 분리** —
클라이언트가 어느 연결 서버에 붙어 있든, 메시지는 그와 묶인 Actor로 전달된다.

**왜 Actor인가 — 재접속 이전성.** Actor는 actor id로 식별되며 세션 연결과 별개로
존재한다. 클라이언트가 끊겼다 다른 연결 서버로 재접속해도 같은 Actor로 다시
묶인다 — "어느 서버에 붙어 있었는지"를 외부 저장소(Redis 등)로 관리하던 일을
라이브러리가 가져간다. Actor는 raw 소켓의 대안이 아니라 **Spot 위에 얹는 한 단계
더 높은 모델**이며, Actor 메시지도 결국 Spot routed 평면 위로 흐른다.

## 시나리오 — single-player queue

아래 전체 프로그램은 Actor의 **재접속 이전성**을 한 흐름으로 보인다:

1. 노드·spot·actor를 만들고, actor를 첫 spot에 **join**한다.
2. actor가 spot을 **leave**한다(연결은 살아 있되 처리 위치만 빠짐).
3. **그 사이에 도착한 메시지는 유실되지 않고 큐잉**된다.
4. actor가 다시 **join**(rejoin)하면, 큐된 메시지를 받는다.

핵심 단계를 차례로 본다. 각 탭은 자기 언어의 실제 샘플 흐름이다.

### 1. 노드·Actor 생성

=== "C++"
    ```cpp
    zlink::context_t ctx;
    zlink::service::spot_node_t node(ctx);
    zlink::service::spot_t spot = node.create_spot();
    zlink::service::actor_t actor = node.create_actor("single-player");
    zlink::actor_ref_t ref = actor.ref();
    ```
=== "C#/.NET"
    ```csharp
    using var ctx = Zlink.CreateContext();
    using var node = ctx.CreateSpotNode();
    using var spot = node.CreateSpot();
    using var actor = node.CreateActor("single-player");
    ActorRef reference = actor.Ref;
    ```
=== "Java"
    ```java
    try (Context ctx = Zlink.createContext();
         SpotNode node = ctx.createSpotNode();
         Spot spot = node.createSpot()) {
        Actor actor = node.createActor("single-player");
        ActorRef ref = actor.ref();
    }
    ```
=== "Kotlin"
    ```kotlin
    val ctx = Zlink.createContext()
    val node = ctx.createSpotNode()
    val spot = node.createSpot()
    val actor = node.createActor("single-player")
    val ref = actor.ref()
    ```
=== "Python"
    ```python
    with zlink.create_context() as ctx:
        with zlink.create_spot_node(ctx) as node:
            with node.create_spot() as spot:
                actor = node.actor("single-player")
                actor_ref = actor.ref()
    ```
=== "Node/TypeScript"
    ```typescript
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const spot = node.createSpot();
    const actor = node.createActor('single-player');
    const ref = actor.ref();
    ```
=== "JavaScript"
    ```javascript
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const spot = node.createSpot();
    const actor = node.createActor('single-player');
    const ref = actor.ref();
    ```
=== "Go"
    ```go
    ctx, _ := zlink.NewContext()
    defer ctx.Close()
    node, _ := ctx.SpotNode()
    defer node.Close()
    spot, _ := node.Spot()
    defer spot.Close()
    actor, _ := node.Actor("single-player")
    ref := actor.Ref()
    ```
=== "Rust"
    ```rust
    let ctx = Context::new()?;
    let node = SpotNode::new(&ctx)?;
    let spot = node.create_spot()?;
    let mut actor = node.create_actor("single-player")?;
    let reference = actor.ref_();
    ```

### 2. Spot join

Actor가 spot에 join한다. join은 비동기 제출이며 완료는 콜백/future로 받는다.

=== "C++"
    ```cpp
    zlink::message_t join_msg = zlink::message_t::from("join-first");
    actor.join(spot)
        .message(join_msg)
        .timeout(std::chrono::seconds(1))
        .submit([](const zlink::actor_join_result_t &result,
                   std::vector<zlink::message_t> parts) {
        });
    ```
=== "C#/.NET"
    ```csharp
    using Message joinMsg = Message.From("join-first");
    var joinTask = actor.Join(spot)
        .Message(joinMsg)
        .Timeout(TimeSpan.FromSeconds(2))
        .SubmitAsync();
    ```
=== "Java"
    ```java
    try (Message joinMsg = Message.from("join-first")) {
        var joinFuture = actor.join(spot)
            .message(joinMsg)
            .timeout(Duration.ofSeconds(2))
            .submitAsync();
    }
    ```
=== "Kotlin"
    ```kotlin
    Message.from("join-first").use { joinMsg ->
        actor.join(spot)
            .message(joinMsg)
            .timeout(Duration.ofSeconds(2))
            .submit { result, messages -> messages.forEach(Message::close) }
    }
    ```
=== "Python"
    ```python
    actor.join(spot).message(b"join-first").timeout(2).submit(
        lambda result, messages: [m.close() for m in messages]
    )
    ```
=== "Node/TypeScript"
    ```typescript
    const replyPromise = actor.join(spot)
        .message(Buffer.from('join-first'))
        .timeout(2000)
        .submitAsync();
    ```
=== "JavaScript"
    ```javascript
    const replyPromise = actor.join(spot)
        .message(Buffer.from('join-first'))
        .timeout(2000)
        .submitAsync();
    ```
=== "Go"
    ```go
    actor.Join(firstSpot).
        Message(samplecommon.Message("join-first")).
        Flags(zlink.SendFlagsDontWait).
        Timeout(time.Second).
        Submit(nil, func(result zlink.ActorJoinResult, parts []*zlink.Message) {
            for _, part := range parts { part.Close() }
        })
    ```
=== "Rust"
    ```rust
    actor.join(&first_spot)
        .message(Message::try_from(b"join-first")?)
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, parts| { })?;
    ```

대상 Spot은 join 요청을 받아 수락(0) 또는 거부 코드로 응답한다. 보통 dispatch
핸들러 안에서 처리한다.

=== "C++"
    ```cpp
    if (auto request = spot.recv_actor_join(zlink::recv_flags_t::dontwait)) {
        zlink::message_t reply = zlink::message_t::from("accepted");
        spot.reply_actor_join(*request, 0).message(reply).submit();
    }
    ```
=== "C#/.NET"
    ```csharp
    ActorJoinRequest? request = spot.RecvActorJoin(RecvFlags.DontWait);
    if (request != null)
    {
        using Message reply = Message.From("accepted");
        spot.ReplyActorJoin(request, joinResultCode: 0).Message(reply).Submit();
    }
    ```
=== "Java"
    ```java
    try (ActorJoinRequest request = spot.recvActorJoin(RecvFlags.DONT_WAIT)) {
        try (Message reply = Message.from("accepted")) {
            spot.replyActorJoin(request, 0).message(reply).submit();
        }
    }
    ```
=== "Kotlin"
    ```kotlin
    spot.recvActorJoin(RecvFlags.DONT_WAIT)?.use { request ->
        Message.from("accepted").use { reply ->
            spot.replyActorJoin(request, 0).message(reply).submit()
        }
    }
    ```
=== "Python"
    ```python
    request = current_spot.recv_actor_join(flags=zlink.RecvFlags.DONT_WAIT)
    if request is not None:
        request.message.close()
        current_spot.reply_actor_join(request, 0).message(b"accepted").submit()
    ```
=== "Node/TypeScript"
    ```typescript
    const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
    if (request) {
        spot.replyActorJoin(request, 0).message(Buffer.from('accepted')).submit();
    }
    ```
=== "JavaScript"
    ```javascript
    const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
    if (request) {
        spot.replyActorJoin(request, 0).message(Buffer.from('accepted')).submit();
    }
    ```
=== "Go"
    ```go
    request, _ := spot.RecvActorJoin(zlink.RecvFlagsDontWait)
    request.Message.Close()
    spot.ReplyActorJoin(request, 0).Message(samplecommon.Message("accepted")).Submit(nil)
    ```
=== "Rust"
    ```rust
    if let Ok(request) = spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
        spot.reply_actor_join(&request, 0)
            .message(Message::try_from(b"accepted")?)
            .submit()?;
    }
    ```

### 3. Actor 메시지 수신 (dispatch 핸들러)

Actor에게 향한 메시지는 Spot의 dispatch 핸들러에서 `ACTOR_READABLE` 이벤트로
받아 `EAGAIN`까지 소진(drain)한다.

=== "C++"
    ```cpp
    spot.set_dispatch_handler(
        [&](zlink::service::spot_t &, const zlink::spot_dispatch_info_t &info) {
            if (info.event != zlink::spot_dispatch_event_t::actor_readable)
                return;
            while (auto part = node.recv_actor_part(
                       *info.actor, zlink::recv_flags_t::dontwait)) {
                payloads.push_back(part->message().to_string());
            }
        });
    ```
=== "C#/.NET"
    ```csharp
    spot.SetDispatchHandler(info =>
    {
        ActorReceived? part;
        while ((part = info.RecvActor()) != null)
            using (part)
                actorMessages.Add(part.Message.GetString());
    });
    ```
=== "Java"
    ```java
    spot.setDispatchHandler(info -> {
        if (info.event() != SpotDispatchEvent.ACTOR_READABLE) return;
        for (ActorReceived part : info.actorMessages()) {
            try (part) {
                payloads.add(part.message().toUtf8String());
            }
        }
    });
    ```
=== "Kotlin"
    ```kotlin
    spot.setDispatchHandler { info ->
        if (info.event() != SpotDispatchEvent.ACTOR_READABLE) return@setDispatchHandler
        for (part in info.actorMessages()) {
            part.use { payloads.add(it.message().toUtf8String()) }
        }
    }
    ```
=== "Python"
    ```python
    def on_dispatch(current_spot, info):
        if info.event == zlink.SpotDispatchEvent.ACTOR_READABLE:
            while True:
                part = info.recv_actor_part(flags=zlink.RecvFlags.DONT_WAIT)
                if part is None:
                    return
                payloads.append(part.message.to_bytes())
                part.message.close()

    spot.on_dispatch_event(on_dispatch)
    ```
=== "Node/TypeScript"
    ```typescript
    spot.setDispatchHandler((info) => {
        if (info.event !== zlink.SpotDispatchEvent.ActorReadable) return;
        for (;;) {
            const part = info.recvActorPart(zlink.RecvFlags.DontWait);
            if (!part) return;
            payloads.push(part.message.data().toString());
        }
    });
    ```
=== "JavaScript"
    ```javascript
    spot.setDispatchHandler((info) => {
        if (info.event !== zlink.SpotDispatchEvent.ActorReadable) return;
        for (;;) {
            const part = info.recvActorPart(zlink.RecvFlags.DontWait);
            if (!part) return;
            payloads.push(part.message.data().toString());
        }
    });
    ```
=== "Go"
    ```go
    part, _ := actor.RecvPart(zlink.RecvFlagsDontWait)
    if part != nil {
        payloads = append(payloads, string(part.Message.Data()))
        part.Message.Close()
    }
    ```
=== "Rust"
    ```rust
    second_spot.on_dispatch_event(move |info| {
        if info.event != SpotDispatchEvent::ActorReadable { return; }
        let mut received = ActorReceived::empty();
        loop {
            match info.recv_actor(&mut received, RecvFlags::DONT_WAIT) {
                Ok(true)  => payloads.push(received.first_part().unwrap()
                                                   .as_str().unwrap().to_owned()),
                Ok(false) => break,
                Err(_)    => break,
            }
        }
    })?;
    ```

### 4. Leave와 종료

actor가 leave하면 처리 위치만 Entry Spot으로 빠지고 세션 연결은 살아 있다. 그
사이 도착한 메시지는 유실되지 않고 큐잉되며, 다시 join하면 받는다(앞 단계에서
보인 흐름). 마지막에 actor를 닫는다 — Entry Spot에 있을 때만 종료가 성공한다.

=== "C++"
    ```cpp
    actor.leave(spot).submit_async().get();
    actor.close();
    ```
=== "C#/.NET"
    ```csharp
    Zlink.MultipartClose(await actor.Leave(spot)
        .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync());
    ```
=== "Java"
    ```java
    actor.leave(spot).submitAsync().join().forEach(Message::close);
    actor.close();
    ```
=== "Kotlin"
    ```kotlin
    actor.leave(spot).submitAsync().join().forEach(Message::close)
    actor.close()
    ```
=== "Python"
    ```python
    actor.leave(spot)
    actor.close()
    ```
=== "Node/TypeScript"
    ```typescript
    await actor.leave(spot).timeout(2000).submitAsync();
    actor.close(2000);
    ```
=== "JavaScript"
    ```javascript
    await actor.leave(spot).timeout(2000).submitAsync();
    actor.close(2000);
    ```
=== "Go"
    ```go
    leaveCh, _ := actor.Leave(spot).Timeout(time.Second).SubmitAsync(nil)
    leave := <-leaveCh
    zlink.MultipartClose(leave.Parts)
    actor.Close()
    ```
=== "Rust"
    ```rust
    actor.leave(&spot)
        .timeout(Duration::from_secs(1))
        .submit(move |result| { })?;
    actor.close()?;
    ```

## 더 보기

- 전체 실행 프로그램(STREAM 게이트웨이 bind, 세션 라우팅 포함)은 각 언어
  `samples/`의 `actor_single_player_queue`를 본다 — 위 코드의 출처다.
- 다른 actor 패턴: `actor_room_server`(방 디스패치), `actor_gateway_relay`(외부
  세션 릴레이).
- 정확한 계약: [SPOT spec](../spec/core/service/spot.ko.md). 개념·언제 쓰나:
  [서비스 개요 §멘탈 모델](./07-0-services.ko.md).
