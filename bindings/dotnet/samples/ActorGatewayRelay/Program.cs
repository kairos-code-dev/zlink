using System.Text;
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        if (!SampleSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();
        using var actor = node.CreateActor("play-session-actor");
        using var sessionReady = new ManualResetEventSlim(false);
        RoutingId? sessionRid = null;
        string receivedPayload = "";

        spot.SetDispatchHandler(info =>
        {
            ActorReceived? part = info.RecvActor();
            if (part == null)
                return;
            using (part)
            {
                receivedPayload = part.Message.GetString();
            }
        });

        using var stream = ctx.CreateStreamSocket();
        string endpoint = SampleSupport.NewEndpoint("tcp", "actor-gateway");
        int port = SampleSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);
        stream.OnPacket((routingId, header, payload) =>
        {
            header.Dispose();
            sessionRid = routingId;
            using (payload)
            {
                receivedPayload = Encoding.UTF8.GetString(payload.AsReadOnlySpan());
            }
            sessionReady.Set();
        });

        using var client = SampleSupport.ConnectRawClient(port);
        SampleSupport.SendStreamPacket(client.GetStream(), "hello-gateway"u8);
        if (!sessionReady.Wait(5000) || sessionRid == null)
            throw new TimeoutException("stream session");

        stream.AttachActorGateway(node);
        Zlink.MultipartClose(await stream.BindActor(sessionRid.Value, actor.Ref)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));

        using Message joinMessage = Message.From("join-play");
        Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> joinTask =
            actor.Join(spot)
                .Message(joinMessage)
                .Timeout(TimeSpan.FromSeconds(2))
                .Async();
        ActorJoinRequest? request = null;
        SampleSupport.WaitOrThrow(() =>
        {
            request = spot.RecvActorJoin(RecvFlags.DontWait);
            return request != null;
        }, 2000, "actor join request");
        using Message joinReply = Message.From("accepted");
        spot.ReplyActorJoin(request!, joinResultCode: 0).Message(joinReply).Submit();
        foreach (Message reply in (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts)
            reply.Dispose();

        using Message relayed = Message.From("client-input");
        stream.SendBoundActor(sessionRid.Value, actor.Ref.ActorId)
            .Message(relayed)
            .Submit();
        SampleSupport.WaitOrThrow(
            () => receivedPayload == "client-input",
            5000,
            "actor relay");

        Console.WriteLine("[actor/gateway] stream payload: \"client-input\" -> actor: \"client-input\"");
        Zlink.MultipartClose(await actor.Leave(spot)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));
        Zlink.MultipartClose(await stream.UnbindActor(sessionRid.Value, actor.Ref.ActorId)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));
    }
}
