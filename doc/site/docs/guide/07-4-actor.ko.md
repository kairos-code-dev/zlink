[English](./07-4-actor.md) | [한국어](./07-4-actor.ko.md)

# SPOT Actor 사용 가이드

이 문서는 개념(역할·언제 쓰나)을 먼저 언어 무관으로 설명하고, **하나의 전체
프로그램**을 **9개 언어 코드 탭**으로 이어서 보인다. 단계 설명은 코드 안 주석으로
단다 — 자기 언어 탭 하나만 위에서 아래로 읽으면 시나리오 전체가 한 흐름으로
이어진다. Kotlin은 Java 바인딩 런타임을, JavaScript는 Node 바인딩 런타임을
공유하지만 언어가 다르므로 별도 탭으로 보인다.

!!! note "파일럿 문서"
    이 문서는 [문서화 원칙](../../../principal/documentation/documentation-principles.ko.md)에
    따라 "전체 프로그램 + 9언어 탭" 형식을 시험 적용한 첫 문서다. 코드는 각 언어의
    `samples/actor_single_player_queue` 샘플에서 가져왔고 9언어가 같은 시나리오·값·
    출력(`"before/between"`)을 낸다(세션 배선·검증 등 테스트 스캐폴딩은 가독성을
    위해 생략). 정확한 함수 계약은 [SPOT spec](../spec/core/service/spot.ko.md)을 본다.

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

1. 노드·spot·actor를 만들고, actor를 spot에 **join**한다.
2. actor가 spot을 **leave**한다(연결은 살아 있되 처리 위치만 빠짐).
3. **그 사이에 도착한 메시지는 유실되지 않고 큐잉**된다.
4. actor가 다시 **join**(rejoin)하면, 큐된 메시지를 받는다.

메시지는 STREAM 게이트웨이에 묶인 세션(`session`)으로 보낸다. `"before"`는 actor가
join한 상태에서, `"between"`은 leave한 사이에 도착해 큐잉되고, rejoin하면 둘 다
순서대로 배달돼 결과는 `"before/between"`이 된다.

## 전체 프로그램

=== "C++"
    ```cpp
    zlink::context_t ctx;
    zlink::service::spot_node_t node(ctx);
    zlink::service::spot_t spot = node.create_spot();
    zlink::service::actor_t actor = node.create_actor("single-player");
    std::string payloads;

    // 스트림 게이트웨이에 세션을 붙이고 actor를 바인딩한다.
    // session: 게이트웨이로 연결된 클라이언트의 세션 라우팅 ID (획득은 샘플 참조)
    zlink::stream_socket_t stream(ctx);
    stream.attach_actor_gateway(node);
    stream.bind_actor(session, actor.ref()).submit_async().get();

    // dispatch 핸들러: join 요청 수락 + actor 메시지 수신
    spot.set_dispatch_handler(
        [&](zlink::service::spot_t &s, const zlink::spot_dispatch_info_t &info) {
            if (info.event == zlink::spot_dispatch_event_t::actor_join_readable) {
                auto request = s.recv_actor_join(zlink::recv_flags_t::dontwait);
                zlink::message_t reply = zlink::message_t::from("accepted");
                s.reply_actor_join(*request, 0).message(reply).submit();
            } else if (info.event == zlink::spot_dispatch_event_t::actor_readable) {
                while (auto part = node.recv_actor_part(
                           *info.actor, zlink::recv_flags_t::dontwait))
                    payloads += part->message().to_string();
            }
        });

    // join-first → "before"는 joined 상태에서 도착
    zlink::message_t join_first = zlink::message_t::from("join-first");
    actor.join(spot).message(join_first).submit(
        [](const zlink::actor_join_result_t &, std::vector<zlink::message_t>) {});
    zlink::message_t before = zlink::message_t::from("before");
    stream.send_bound_actor(session, "single-player").message(before).submit();

    // leave → "between"은 leave 사이에 도착 → 큐잉
    actor.leave(spot).submit_async().get();
    zlink::message_t between = zlink::message_t::from("between");
    stream.send_bound_actor(session, "single-player").message(between).submit();

    // rejoin → 큐된 "before"/"between"이 핸들러로 배달 → "before/between"
    zlink::message_t join_second = zlink::message_t::from("join-second");
    actor.join(spot).message(join_second).submit(
        [](const zlink::actor_join_result_t &, std::vector<zlink::message_t>) {});

    actor.leave(spot).submit_async().get();
    actor.close();
    ```
=== "C#/.NET"
    ```csharp
    using var ctx = Zlink.CreateContext();
    using var node = ctx.CreateSpotNode();
    using var spot = node.CreateSpot();
    using var actor = node.CreateActor("single-player");
    using var stream = ctx.CreateStreamSocket();
    List<string> payloads = new();

    // 스트림 게이트웨이에 세션을 붙이고 actor를 바인딩한다.
    // session: 게이트웨이로 연결된 클라이언트 세션 ID (획득은 샘플 참조)
    stream.AttachActorGateway(node);
    Zlink.MultipartClose(await stream.BindActor(session, actor.Ref)
        .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync());

    // dispatch 핸들러: actor 메시지 수신
    spot.SetDispatchHandler(info =>
    {
        ActorReceived? part;
        while ((part = info.RecvActor()) != null)
            using (part) payloads.Add(part.Message.GetString());
    });

    // join 요청을 수락하고 "accepted"로 응답한다.
    void Accept()
    {
        ActorJoinRequest? request = spot.RecvActorJoin(RecvFlags.DontWait);
        using Message reply = Message.From("accepted");
        spot.ReplyActorJoin(request!, joinResultCode: 0).Message(reply).Submit();
    }

    // join-first → "before"는 joined 상태에서 도착
    var join1 = actor.Join(spot).Message(Message.From("join-first"))
        .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync();
    Accept();
    Zlink.MultipartClose((await join1).Parts);
    stream.SendBoundActor(session, actor.Ref.ActorId)
        .Message(Message.From("before")).Submit();

    // leave → "between"은 leave 사이에 도착 → 큐잉
    Zlink.MultipartClose(await actor.Leave(spot)
        .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync());
    stream.SendBoundActor(session, actor.Ref.ActorId)
        .Message(Message.From("between")).Submit();

    // rejoin → 큐된 "before"/"between" 배달 → "before/between"
    var join2 = actor.Join(spot).Message(Message.From("join-second"))
        .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync();
    Accept();
    Zlink.MultipartClose((await join2).Parts);

    Zlink.MultipartClose(await actor.Leave(spot)
        .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync());
    ```
=== "Java"
    ```java
    try (Context ctx = Zlink.createContext();
         SpotNode node = ctx.createSpotNode();
         Spot spot = node.createSpot();
         StreamSocket stream = ctx.createStreamSocket()) {
        Actor actor = node.createActor("single-player");
        List<String> payloads = new ArrayList<>();

        // 스트림 게이트웨이에 세션을 붙이고 actor를 바인딩한다.
        // session: 게이트웨이로 연결된 클라이언트 세션 ID (획득은 샘플 참조)
        stream.attachActorGateway(node);
        stream.bindActor(session, actor.ref())
            .submitAsync().join().forEach(Message::close);

        // dispatch 핸들러: join 수락 + actor 메시지 수신
        spot.setDispatchHandler(info -> {
            if (info.event() == SpotDispatchEvent.ACTOR_JOIN_READABLE) {
                try (ActorJoinRequest request = spot.recvActorJoin(RecvFlags.DONT_WAIT);
                     Message reply = Message.from("accepted")) {
                    spot.replyActorJoin(request, 0).message(reply).submit();
                }
            } else if (info.event() == SpotDispatchEvent.ACTOR_READABLE) {
                for (ActorReceived part : info.actorMessages()) {
                    try (part) {
                        payloads.add(part.message().toUtf8String());
                    }
                }
            }
        });

        // join-first → "before"는 joined 상태에서 도착
        try (Message m = Message.from("join-first")) {
            actor.join(spot).message(m).timeout(Duration.ofSeconds(2))
                .submit((result, messages) -> messages.forEach(Message::close));
        }
        try (Message m = Message.from("before")) {
            stream.sendBoundActor(session, "single-player").message(m).submit();
        }

        // leave → "between"은 leave 사이에 도착 → 큐잉
        actor.leave(spot).submitAsync().join().forEach(Message::close);
        try (Message m = Message.from("between")) {
            stream.sendBoundActor(session, "single-player").message(m).submit();
        }

        // rejoin → 큐된 "before"/"between" 배달 → "before/between"
        try (Message m = Message.from("join-second")) {
            actor.join(spot).message(m).timeout(Duration.ofSeconds(2))
                .submit((result, messages) -> messages.forEach(Message::close));
        }

        actor.leave(spot).submitAsync().join().forEach(Message::close);
        actor.close();
    }
    ```
=== "Kotlin"
    ```kotlin
    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { node ->
            node.createSpot().use { spot ->
                ctx.createStreamSocket().use { stream ->
                    val actor = node.createActor("single-player")
                    val payloads = mutableListOf<String>()

                    // 스트림 게이트웨이에 세션을 붙이고 actor를 바인딩한다.
                    // session: 게이트웨이로 연결된 클라이언트 세션 ID (획득은 샘플 참조)
                    stream.attachActorGateway(node)
                    stream.bindActor(session, actor.ref())
                        .submitAsync().join().forEach(Message::close)

                    // dispatch 핸들러: join 수락 + actor 메시지 수신
                    spot.setDispatchHandler { info ->
                        when (info.event()) {
                            SpotDispatchEvent.ACTOR_JOIN_READABLE ->
                                spot.recvActorJoin(RecvFlags.DONT_WAIT)?.use { request ->
                                    Message.from("accepted").use { reply ->
                                        spot.replyActorJoin(request, 0).message(reply).submit()
                                    }
                                }
                            SpotDispatchEvent.ACTOR_READABLE ->
                                for (part in info.actorMessages()) {
                                    part.use { payloads.add(it.message().toUtf8String()) }
                                }
                            else -> {}
                        }
                    }

                    // join-first → "before"는 joined 상태에서 도착
                    Message.from("join-first").use { m ->
                        actor.join(spot).message(m).timeout(Duration.ofSeconds(2))
                            .submit { _, messages -> messages.forEach(Message::close) }
                    }
                    Message.from("before").use { m ->
                        stream.sendBoundActor(session, "single-player").message(m).submit()
                    }

                    // leave → "between"은 leave 사이에 도착 → 큐잉
                    actor.leave(spot).submitAsync().join().forEach(Message::close)
                    Message.from("between").use { m ->
                        stream.sendBoundActor(session, "single-player").message(m).submit()
                    }

                    // rejoin → 큐된 "before"/"between" 배달 → "before/between"
                    Message.from("join-second").use { m ->
                        actor.join(spot).message(m).timeout(Duration.ofSeconds(2))
                            .submit { _, messages -> messages.forEach(Message::close) }
                    }

                    actor.leave(spot).submitAsync().join().forEach(Message::close)
                    actor.close()
                }
            }
        }
    }
    ```
=== "Python"
    ```python
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as node, \
         node.create_spot() as spot, \
         zlink.create_stream_socket(ctx) as stream:
        actor = node.actor("single-player")
        actor_ref = actor.ref()
        payloads = []

        # 스트림 게이트웨이에 세션을 붙이고 actor를 바인딩한다.
        # session: 게이트웨이로 연결된 클라이언트 세션 ID (획득은 샘플 참조)
        stream.attach_actor_gateway(node)
        stream.bind_actor(session, actor_ref).timeout(2).submit(
            lambda result, messages: [m.close() for m in messages])

        # dispatch 핸들러: join 수락 + actor 메시지 수신
        def on_dispatch(current_spot, info):
            if info.event == zlink.SpotDispatchEvent.ACTOR_JOIN_READABLE:
                request = current_spot.recv_actor_join(flags=zlink.RecvFlags.DONT_WAIT)
                request.message.close()
                current_spot.reply_actor_join(request, 0).message(b"accepted").submit()
            elif info.event == zlink.SpotDispatchEvent.ACTOR_READABLE:
                while True:
                    part = info.recv_actor_part(flags=zlink.RecvFlags.DONT_WAIT)
                    if part is None:
                        return
                    payloads.append(part.message.to_bytes())
                    part.message.close()

        spot.on_dispatch_event(on_dispatch)

        # join-first → "before"는 joined 상태에서 도착
        actor.join(spot).message(b"join-first").timeout(2).submit(
            lambda result, messages: [m.close() for m in messages])
        stream.send_bound_actor(session, "single-player").message(b"before").submit()

        # leave → "between"은 leave 사이에 도착 → 큐잉
        actor.leave(spot).timeout(2).submit(
            lambda result, messages: [m.close() for m in messages])
        stream.send_bound_actor(session, "single-player").message(b"between").submit()

        # rejoin → 큐된 "before"/"between" 배달 → "before/between"
        actor.join(spot).message(b"join-second").timeout(2).submit(
            lambda result, messages: [m.close() for m in messages])

        actor.leave(spot).timeout(2).submit(
            lambda result, messages: [m.close() for m in messages])
        actor.close()
    ```
=== "Node/TypeScript"
    ```typescript
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const spot = node.createSpot();
    const actor = node.createActor('single-player');
    const stream = zlink.createStreamSocket(ctx);
    const payloads: string[] = [];

    // 스트림 게이트웨이에 세션을 붙이고 actor를 바인딩한다.
    // session: 게이트웨이로 연결된 클라이언트 세션 ID (획득은 stream.setPacketHandler — 샘플 참조)
    stream.attachActorGateway(node);
    await stream.bindActor(session, actor.ref()).timeout(2000).submitAsync();

    // dispatch 핸들러: actor 메시지 수신
    spot.setDispatchHandler((info) => {
        if (info.event !== zlink.SpotDispatchEvent.ActorReadable) return;
        for (;;) {
            const part = info.recvActorPart(zlink.RecvFlags.DontWait);
            if (!part) return;
            payloads.push(part.message.data().toString());
        }
    });

    // join 요청을 수신해 "accepted"로 응답하는 헬퍼
    async function joinAndAccept(payload: string) {
        const reply = actor.join(spot).message(Buffer.from(payload)).timeout(2000).submitAsync();
        const request = spot.recvActorJoin(zlink.RecvFlags.DontWait)!;
        spot.replyActorJoin(request, 0).message(Buffer.from('accepted')).submit();
        await reply;
    }

    // join-first → "before"는 joined 상태에서 도착
    await joinAndAccept('join-first');
    stream.sendBoundActor(session, 'single-player').message(Buffer.from('before')).submit();

    // leave → "between"은 leave 사이에 도착 → 큐잉
    await actor.leave(spot).timeout(2000).submitAsync();
    stream.sendBoundActor(session, 'single-player').message(Buffer.from('between')).submit();

    // rejoin → 큐된 "before"/"between" 배달 → "before/between"
    await joinAndAccept('join-second');

    await actor.leave(spot).timeout(2000).submitAsync();
    actor.close(2000);
    ```
=== "JavaScript"
    ```javascript
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const spot = node.createSpot();
    const actor = node.createActor('single-player');
    const stream = zlink.createStreamSocket(ctx);
    const payloads = [];

    // 스트림 게이트웨이에 세션을 붙이고 actor를 바인딩한다.
    // session: 게이트웨이로 연결된 클라이언트 세션 ID (획득은 stream.setPacketHandler — 샘플 참조)
    stream.attachActorGateway(node);
    await stream.bindActor(session, actor.ref()).timeout(2000).submitAsync();

    // dispatch 핸들러: actor 메시지 수신
    spot.setDispatchHandler((info) => {
        if (info.event !== zlink.SpotDispatchEvent.ActorReadable) return;
        for (;;) {
            const part = info.recvActorPart(zlink.RecvFlags.DontWait);
            if (!part) return;
            payloads.push(part.message.data().toString());
        }
    });

    // join 요청을 수신해 "accepted"로 응답하는 헬퍼
    async function joinAndAccept(payload) {
        const reply = actor.join(spot).message(Buffer.from(payload)).timeout(2000).submitAsync();
        const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
        spot.replyActorJoin(request, 0).message(Buffer.from('accepted')).submit();
        await reply;
    }

    // join-first → "before"는 joined 상태에서 도착
    await joinAndAccept('join-first');
    stream.sendBoundActor(session, 'single-player').message(Buffer.from('before')).submit();

    // leave → "between"은 leave 사이에 도착 → 큐잉
    await actor.leave(spot).timeout(2000).submitAsync();
    stream.sendBoundActor(session, 'single-player').message(Buffer.from('between')).submit();

    // rejoin → 큐된 "before"/"between" 배달 → "before/between"
    await joinAndAccept('join-second');

    await actor.leave(spot).timeout(2000).submitAsync();
    actor.close(2000);
    ```
=== "Go"
    ```go
    ctx, _ := zlink.NewContext()
    node, _ := ctx.SpotNode()
    spot, _ := node.Spot()
    actor, _ := node.Actor("single-player")
    stream, _ := ctx.StreamSocket()
    var mu sync.Mutex
    var payloads []string

    // 스트림 게이트웨이에 세션을 붙이고 actor를 바인딩한다.
    // session: 게이트웨이로 연결된 클라이언트 세션 ID (획득은 샘플 참조)
    stream.AttachActorGateway(node)
    session := zlink.NewRoutingIDString("single-player-session")
    bindCh, _ := stream.BindActor(session, actor.Ref()).Timeout(time.Second).SubmitAsync(nil)
    zlink.MultipartClose((<-bindCh).Parts)

    // dispatch 핸들러: join 수락 + actor 메시지 수신
    spot.OnDispatchEvent(func(s *zlink.Spot, info zlink.SpotDispatchInfo) {
        switch info.Event {
        case zlink.SpotDispatchEventActorJoinReadable:
            request, _ := s.RecvActorJoin(zlink.RecvFlagsDontWait)
            request.Message.Close()
            s.ReplyActorJoin(request, 0).Message(samplecommon.Message("accepted")).Submit(nil)
        case zlink.SpotDispatchEventActorReadable:
            for {
                part, _ := info.RecvActorPart(zlink.RecvFlagsDontWait)
                if part == nil {
                    return
                }
                mu.Lock()
                payloads = append(payloads, string(part.Message.Data()))
                mu.Unlock()
                part.Message.Close()
            }
        }
    })

    // join-first → "before"는 joined 상태에서 도착
    actor.Join(spot).Message(samplecommon.Message("join-first")).Timeout(time.Second).
        Submit(nil, func(r zlink.ActorJoinResult, p []*zlink.Message) { zlink.MultipartClose(p) })
    stream.SendBoundActor(session, "single-player").Message(samplecommon.Message("before")).Submit(nil)

    // leave → "between"은 leave 사이에 도착 → 큐잉
    leaveCh, _ := actor.Leave(spot).Timeout(time.Second).SubmitAsync(nil)
    zlink.MultipartClose((<-leaveCh).Parts)
    stream.SendBoundActor(session, "single-player").Message(samplecommon.Message("between")).Submit(nil)

    // rejoin → 큐된 "before"/"between" 배달 → "before/between"
    actor.Join(spot).Message(samplecommon.Message("join-second")).Timeout(time.Second).
        Submit(nil, func(r zlink.ActorJoinResult, p []*zlink.Message) { zlink.MultipartClose(p) })

    leaveCh2, _ := actor.Leave(spot).Timeout(time.Second).SubmitAsync(nil)
    zlink.MultipartClose((<-leaveCh2).Parts)
    actor.Close()
    ```
=== "Rust"
    ```rust
    let ctx = Context::new()?;
    let node = SpotNode::new(&ctx)?;
    let mut spot = node.create_spot()?;
    let mut actor = node.create_actor("single-player")?;
    let stream = ctx.stream_socket()?;
    let payloads = Arc::new(Mutex::new(Vec::<String>::new()));

    // 스트림 게이트웨이에 세션을 붙이고 actor를 바인딩한다.
    // session: 게이트웨이로 연결된 클라이언트 세션 ID (획득은 샘플 참조)
    stream.attach_actor_gateway(&node)?;
    let session = zlink::RoutingId::from(b"single-player-session");
    stream.bind_actor(&session, &actor.ref_()).submit(|_| {})?;

    // dispatch 핸들러: actor 메시지 수신
    let sink = Arc::clone(&payloads);
    spot.on_dispatch_event(move |info| {
        if info.event != SpotDispatchEvent::ActorReadable { return; }
        let mut received = ActorReceived::empty();
        while let Ok(true) = info.recv_actor(&mut received, RecvFlags::DONT_WAIT) {
            sink.lock().unwrap().push(
                received.first_part().unwrap().as_str().unwrap().to_owned());
        }
    })?;

    // join 요청을 수신해 "accepted"로 응답한다.
    let accept = |spot: &Spot| -> zlink::Result<()> {
        if let Ok(Some(request)) = spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            spot.reply_actor_join(&request, 0)
                .message(Message::try_from(b"accepted")?).submit()?;
        }
        Ok(())
    };

    // join-first → "before"는 joined 상태에서 도착
    actor.join(&spot).message(Message::try_from(b"join-first")?).submit(|_, _| {})?;
    accept(&spot)?;
    stream.send_bound_actor(&session, "single-player")
        .message(Message::try_from(b"before")?).submit()?;

    // leave → "between"은 leave 사이에 도착 → 큐잉
    actor.leave(&spot).submit(|_| {})?;
    stream.send_bound_actor(&session, "single-player")
        .message(Message::try_from(b"between")?).submit()?;

    // rejoin → 큐된 "before"/"between" 배달 → "before/between"
    actor.join(&spot).message(Message::try_from(b"join-second")?).submit(|_, _| {})?;
    accept(&spot)?;

    actor.leave(&spot).submit(|_| {})?;
    actor.close()?;
    ```

## 더 보기

- 위 코드는 각 언어 `samples/`의 `actor_single_player_queue`에서 가져왔다 — 세션
  획득(STREAM 게이트웨이 연결·라우팅)·타이밍 대기·검증을 포함한 실행 가능한 전체
  프로그램이 거기 있다.
- 다른 actor 패턴: `actor_room_server`(방 디스패치), `actor_gateway_relay`(외부
  세션 릴레이).
- 정확한 계약: [SPOT spec](../spec/core/service/spot.ko.md). 개념·언제 쓰나:
  [서비스 개요 §멘탈 모델](./07-0-services.ko.md).
