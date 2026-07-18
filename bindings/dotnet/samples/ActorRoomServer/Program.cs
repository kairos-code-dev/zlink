using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        if (!SampleSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateMeshNode();
        node.SetBind("tcp://127.0.0.1:*");
        node.Start();
        using var spot = node.CreateSpot();
        ActorRef actor = node.CreateActor("room-player-1");

        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        List<string> payloads = new();

        using var stream = ctx.CreateStreamSocket();
        RoutingId sessionRid = SampleSupport.RoutingIdUtf8("room-session");
        Zlink.MultipartClose(await stream.BindActor(sessionRid, actor)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));

        // actor가 spot에 합류한다 — 호스트가 join 요청을 받아 admit한다.
        ulong epoch = SampleSupport.JoinLocalSpot(node, actor, spot, "enter-room",
            onHostObservedJoin: msg =>
                SampleSupport.EnsureEqual("enter-room", msg, "join message"));

        // 바인딩된 STREAM 세션으로 actor에게 메시지를 relay한다.
        using Message inbound = Message.From("move:north");
        stream.SendBoundActor(sessionRid, actor.ActorId).Message(inbound).Submit();
        SampleSupport.WaitOrThrow(() =>
        {
            SampleSupport.CollectActorMessages(node, ready, recv, payloads);
            return payloads.Contains("move:north");
        }, 5000, "actor payload");

        Console.WriteLine("[actor/room] stream payload: \"move:north\" -> actor: \"move:north\"");
        SampleSupport.LeaveLocalSpot(node, actor, epoch);
    }
}
