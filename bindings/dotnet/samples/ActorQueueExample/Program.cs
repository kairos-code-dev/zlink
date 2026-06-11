// 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// 한 파일 안에 전체 흐름이 들어 있다 — 별도 헬퍼 없이 빌드·실행된다:
//   dotnet run --project samples/ActorQueueExample
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using ISpot spot = node.CreateSpot();
        using var actor = node.CreateActor("single-player");
        using var stream = ctx.CreateStreamSocket();

        List<string> payloads = new();

        // 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서 session은
        // 게이트웨이로 접속한 클라이언트의 라우팅 ID다 — 여기선 고정값으로 만든다.
        stream.AttachActorGateway(node);
        RoutingId session = RoutingId.From("single-player-session");
        Zlink.MultipartClose(await stream.BindActor(session, actor.Ref)
            .Timeout(TimeSpan.FromSeconds(2)).Async());

        // dispatch 핸들러: actor에게 온 메시지를 모은다.
        spot.SetDispatchHandler(info =>
        {
            ActorReceived? part;
            while ((part = info.RecvActor()) != null)
                using (part)
                    payloads.Add(part.Message.GetString());
        });

        // join 요청을 수신해 "accepted"로 응답한다.
        void Accept()
        {
            for (int i = 0; i < 200; i++)
            {
                ActorJoinRequest? request = spot.RecvActorJoin(RecvFlags.DontWait);
                if (request != null)
                {
                    using Message reply = Message.From("accepted");
                    spot.ReplyActorJoin(request, joinResultCode: 0).Message(reply).Submit();
                    return;
                }
                Thread.Sleep(10);
            }
            throw new TimeoutException("actor join request");
        }

        async Task Join(string payload)
        {
            using Message m = Message.From(payload);
            var task = actor.Join(spot).Message(m).Timeout(TimeSpan.FromSeconds(2)).Async();
            Accept();
            Zlink.MultipartClose((await task).Parts);
        }

        void Send(string payload)
        {
            using Message m = Message.From(payload);
            stream.SendBoundActor(session, actor.Ref.ActorId).Message(m).Submit();
        }

        await Join("join-first");  // actor가 spot에 합류
        Send("before");            // joined 상태에서 도착
        Zlink.MultipartClose(await actor.Leave(spot).Timeout(TimeSpan.FromSeconds(2)).Async()); // 처리 위치 이탈
        Send("between");           // leave 사이에 도착 → 큐잉
        await Join("join-second"); // rejoin → 큐된 메시지가 핸들러로 배달된다

        for (int i = 0; i < 200 && payloads.Count < 2; i++)
            Thread.Sleep(10);
        if (!payloads.SequenceEqual(new[] { "before", "between" }))
            throw new Exception($"queued payloads were not preserved: {string.Join(",", payloads)}");

        Zlink.MultipartClose(await actor.Leave(spot).Timeout(TimeSpan.FromSeconds(2)).Async());
        Console.WriteLine("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"");
    }
}
