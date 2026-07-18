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

        using var client = SampleSupport.ConnectRawClient(port);
        SampleSupport.SendStreamPacket(client.GetStream(), "open"u8);
        if (!sessionReady.Wait(5000) || sessionRid == null)
            throw new TimeoutException("stream session");
        Zlink.MultipartClose(await stream.BindActor(sessionRid.Value, actor)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));

        void Collect() => SampleSupport.CollectActorMessages(node, ready, recv, actorMessages);

        ulong epoch = SampleSupport.JoinLocalSpot(node, actor, spot, "join-first");

        using (Message before = Message.From("before"))
            stream.SendBoundActor(sessionRid.Value, actor.ActorId).Message(before).Submit();
        SampleSupport.WaitOrThrow(() => { Collect(); return actorMessages.Contains("before"); },
            5000, "first actor message");

        SampleSupport.LeaveLocalSpot(node, actor, epoch);
        using (Message between = Message.From("between"))
            stream.SendBoundActor(sessionRid.Value, actor.ActorId).Message(between).Submit();
        epoch = SampleSupport.JoinLocalSpot(node, actor, spot, "join-second");
        SampleSupport.WaitOrThrow(() => { Collect(); return actorMessages.Contains("between"); },
            5000, "queued actor message");

        Console.WriteLine("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"");
        SampleSupport.LeaveLocalSpot(node, actor, epoch);
        Zlink.MultipartClose(await stream.UnbindActor(sessionRid.Value, actor.ActorId)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));
    }
}
