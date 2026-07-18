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
        ActorRef actor = node.CreateActor("play-session-actor");
        using var sessionReady = new ManualResetEventSlim(false);
        RoutingId? sessionRid = null;

        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        List<string> payloads = new();

        using var stream = ctx.CreateStreamSocket();
        string endpoint = SampleSupport.NewEndpoint("tcp", "actor-gateway");
        int port = SampleSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);
        // 원격 클라이언트가 접속하면 게이트웨이가 session routing id를 알려준다.
        stream.OnPacket((routingId, header, payload) =>
        {
            header.Dispose();
            payload.Dispose();
            sessionRid = routingId;
            sessionReady.Set();
        });

        using var client = SampleSupport.ConnectRawClient(port);
        SampleSupport.SendStreamPacket(client.GetStream(), "hello-gateway"u8);
        if (!sessionReady.Wait(5000) || sessionRid == null)
            throw new TimeoutException("stream session");

        Zlink.MultipartClose(await stream.BindActor(sessionRid.Value, actor)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));

        // actor가 play spot에 합류한다 (호스트가 admit).
        ulong epoch = SampleSupport.JoinLocalSpot(node, actor, spot, "join-play");

        // 게이트웨이가 클라이언트 입력을 바인딩된 actor로 relay한다.
        using Message relayed = Message.From("client-input");
        stream.SendBoundActor(sessionRid.Value, actor.ActorId)
            .Message(relayed)
            .Submit();
        SampleSupport.WaitOrThrow(() =>
        {
            SampleSupport.CollectActorMessages(node, ready, recv, payloads);
            return payloads.Contains("client-input");
        }, 5000, "actor relay");

        Console.WriteLine("[actor/gateway] stream payload: \"client-input\" -> actor: \"client-input\"");
        SampleSupport.LeaveLocalSpot(node, actor, epoch);
        Zlink.MultipartClose(await stream.UnbindActor(sessionRid.Value, actor.ActorId)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));
    }
}
