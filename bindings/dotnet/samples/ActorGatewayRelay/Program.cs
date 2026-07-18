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
        SampleSupport.StartConfiguredNode(node, "actor-gateway-relay");
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

        // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
        using var sessionService = SampleSupport.StartSessionService(node, stream);

        using var client = SampleSupport.ConnectRawClient(port);
        SampleSupport.SendStreamPacket(client.GetStream(), "hello-gateway"u8);
        if (!sessionReady.Wait(5000) || sessionRid == null)
            throw new TimeoutException("stream session");

        SampleSupport.BindSessionActor(node, sessionService, sessionRid.Value,
            actor);

        // actor가 play spot에 합류한다 (호스트가 admit).
        ulong epoch = SampleSupport.JoinLocalSpot(node, actor, spot, "join-play");

        // 게이트웨이가 클라이언트 입력을 바인딩된 actor로 relay한다.
        SampleSupport.RelaySessionMessage(sessionService, sessionRid.Value, actor,
            "client-input");
        SampleSupport.WaitOrThrow(() =>
        {
            SampleSupport.CollectActorMessages(node, ready, recv, payloads);
            return payloads.Contains("client-input");
        }, 5000, "actor relay");

        Console.WriteLine("[actor/gateway] stream payload: \"client-input\" -> actor: \"client-input\"");
        SampleSupport.LeaveLocalSpot(node, actor, epoch);
        SampleSupport.UnbindSessionActor(node, sessionService, sessionRid.Value,
            actor);
    }
}
