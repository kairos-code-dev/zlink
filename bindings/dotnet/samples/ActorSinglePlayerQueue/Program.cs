using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        if (!SampleSupport.IsNativeAvailable())
            return;

        static async Task JoinAndAccept(ISpot spot, IActor actor, string joinPayload)
        {
            using Message joinMessage = Message.From(joinPayload);
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
            }, 2000, "actor queue join request");
            using Message reply = Message.From("accepted");
            spot.ReplyActorJoin(request!, joinResultCode: 0).Message(reply).Submit();
            foreach (Message part in (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts)
                part.Dispose();
        }

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();
        using var actor = node.CreateActor("single-player");
        List<string> actorMessages = new();
        using var sessionReady = new ManualResetEventSlim(false);
        RoutingId? sessionRid = null;

        spot.SetDispatchHandler(info =>
        {
            ActorReceived? part;
            while ((part = info.RecvActor()) != null)
            {
                using (part)
                    actorMessages.Add(part.Message.GetString());
            }
        });

        using var stream = ctx.CreateStreamSocket();
        string endpoint = SampleSupport.NewEndpoint("tcp", "actor-queue");
        int port = SampleSupport.ExtractPort(endpoint);
        stream.Bind(endpoint);
        stream.OnPacket((routingId, header, payload) =>
        {
            header.Dispose();
            sessionRid = routingId;
            payload.Dispose();
            sessionReady.Set();
        });

        using var client = SampleSupport.ConnectRawClient(port);
        SampleSupport.SendStreamPacket(client.GetStream(), "open"u8);
        if (!sessionReady.Wait(5000) || sessionRid == null)
            throw new TimeoutException("stream session");
        Zlink.MultipartClose(await stream.BindActor(sessionRid.Value, actor.Ref)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));

        await JoinAndAccept(spot, actor, "join-first");

        using Message before = Message.From("before");
        stream.SendBoundActor(sessionRid.Value, actor.Ref.ActorId)
            .Message(before)
            .Submit();
        SampleSupport.WaitOrThrow(
            () => actorMessages.Contains("before"),
            5000,
            "first actor message");

        Zlink.MultipartClose(await actor.Leave(spot)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));
        using Message between = Message.From("between");
        stream.SendBoundActor(sessionRid.Value, actor.Ref.ActorId)
            .Message(between)
            .Submit();
        await JoinAndAccept(spot, actor, "join-second");
        SampleSupport.WaitOrThrow(
            () => actorMessages.Contains("between"),
            5000,
            "queued actor message");

        Console.WriteLine("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"");
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
