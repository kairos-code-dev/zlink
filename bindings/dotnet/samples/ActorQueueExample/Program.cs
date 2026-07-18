// 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// actor가 spot을 떠나 있는 동안 도착한 메시지는 큐잉되고, 다시 합류하면
// 순서대로 배달된다. (수신은 SampleCommon의 pull-dispatch 헬퍼로 회수한다.)
//   dotnet run --project samples/ActorQueueExample
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateMeshNode();
        node.SetBind("tcp://127.0.0.1:*");
        node.Start();
        using ISpot spot = node.CreateSpot();
        ActorRef actor = node.CreateActor("single-player");
        using var stream = ctx.CreateStreamSocket();
        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        List<string> payloads = new();

        // 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서 session은
        // 게이트웨이로 접속한 클라이언트의 라우팅 ID다 — 여기선 고정값으로 만든다.
        RoutingId session = RoutingId.From("single-player-session");
        Zlink.MultipartClose(await stream.BindActor(session, actor)
            .Timeout(TimeSpan.FromSeconds(2)).Async());

        void Send(string payload)
        {
            using Message m = Message.From(payload);
            stream.SendBoundActor(session, actor.ActorId).Message(m).Submit();
        }

        ulong epoch = SampleSupport.JoinLocalSpot(node, actor, spot, "join-first"); // 합류
        Send("before");            // joined 상태에서 도착
        SampleSupport.LeaveLocalSpot(node, actor, epoch);                           // 처리 위치 이탈
        Send("between");           // leave 사이에 도착 → 큐잉
        SampleSupport.JoinLocalSpot(node, actor, spot, "join-second");             // rejoin → 큐 배달

        SampleSupport.WaitOrThrow(() =>
        {
            SampleSupport.CollectActorMessages(node, ready, recv, payloads);
            return payloads.Count >= 2;
        }, 5000, "queued payloads");
        if (!payloads.SequenceEqual(new[] { "before", "between" }))
            throw new Exception($"queued payloads were not preserved: {string.Join(",", payloads)}");

        Console.WriteLine("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"");
    }
}
