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
        SampleSupport.StartConfiguredNode(node, "actor-single-player-queue");
        using var spot = node.CreateSpot();
        ActorRef actor = node.CreateActor("single-player");
        List<string> actorMessages = new();
        using var sessionReady = new ManualResetEventSlim(false);
        RoutingId? sessionRid = null;

        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();

        using var stream = ctx.CreateStreamSocket();
        string endpoint = SampleSupport.NewEndpoint("tcp", "actor-queue");
        int port = SampleSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);
        stream.OnPacket((routingId, header, payload) =>
        {
            header.Dispose();
            payload.Dispose();
            sessionRid = routingId;
            sessionReady.Set();
        });

        // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
        using var sessionService = SampleSupport.StartSessionService(node, stream);

        using var client = SampleSupport.ConnectRawClient(port);
        SampleSupport.SendStreamPacket(client.GetStream(), "open"u8);
        if (!sessionReady.Wait(5000) || sessionRid == null)
            throw new TimeoutException("stream session");
        SampleSupport.BindSessionActor(node, sessionService, sessionRid.Value,
            actor);

        void Collect() => SampleSupport.CollectActorMessages(node, ready, recv, actorMessages);

        ulong epoch = SampleSupport.JoinLocalSpot(node, actor, spot, "join-first");

        SampleSupport.RelaySessionMessage(sessionService, sessionRid.Value, actor,
            "before");
        SampleSupport.WaitOrThrow(() => { Collect(); return actorMessages.Contains("before"); },
            5000, "first actor message");

        SampleSupport.LeaveLocalSpot(node, actor, epoch);
        SampleSupport.RelaySessionMessage(sessionService, sessionRid.Value, actor,
            "between");
        epoch = SampleSupport.JoinLocalSpot(node, actor, spot, "join-second");
        SampleSupport.WaitOrThrow(() => { Collect(); return actorMessages.Contains("between"); },
            5000, "queued actor message");

        Console.WriteLine("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"");
        SampleSupport.LeaveLocalSpot(node, actor, epoch);
        SampleSupport.UnbindSessionActor(node, sessionService, sessionRid.Value,
            actor);
    }
}
