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
        SampleSupport.StartConfiguredNode(node, "actor-room-server");
        using var spot = node.CreateSpot();
        ActorRef actor = node.CreateActor("room-player-1");

        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        List<string> payloads = new();

        using var stream = ctx.CreateStreamSocket();
        RoutingId sessionRid = SampleSupport.RoutingIdUtf8("room-session");
        // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
        using var sessionService = SampleSupport.StartSessionService(node, stream);
        SampleSupport.BindSessionActor(node, sessionService, sessionRid, actor);

        // actor가 spot에 합류한다 — 호스트가 join 요청을 받아 admit한다.
        ulong epoch = SampleSupport.JoinLocalSpot(node, actor, spot, "enter-room",
            onHostObservedJoin: msg =>
                SampleSupport.EnsureEqual("enter-room", msg, "join message"));

        // 바인딩된 STREAM 세션으로 actor에게 메시지를 relay한다.
        SampleSupport.RelaySessionMessage(sessionService, sessionRid, actor,
            "move:north");
        SampleSupport.WaitOrThrow(() =>
        {
            SampleSupport.CollectActorMessages(node, ready, recv, payloads);
            return payloads.Contains("move:north");
        }, 5000, "actor payload");

        Console.WriteLine("[actor/room] stream payload: \"move:north\" -> actor: \"move:north\"");
        SampleSupport.LeaveLocalSpot(node, actor, epoch);
    }
}
