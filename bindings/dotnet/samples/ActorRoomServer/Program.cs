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
        using var actor = node.CreateActor("room-player-1");

        List<string> payloads = new();
        spot.SetDispatchHandler(info =>
        {
            ActorReceived? part;
            while ((part = info.RecvActor()) != null)
                using (part)
                    payloads.Add(part.Message.GetString());
        });

        using var stream = ctx.CreateStreamSocket();
        stream.AttachActorGateway(node);
        RoutingId sessionRid = SampleSupport.RoutingIdUtf8("room-session");
        Zlink.MultipartClose(await stream.BindActor(sessionRid, actor.Ref)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));

        using Message joinMessage = Message.From("enter-room");
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

        using (request!.Message)
        {
            SampleSupport.EnsureEqual("enter-room", request.Message.GetString(),
                "join message");
        }

        using Message reply = Message.From("accepted");
        spot.ReplyActorJoin(request, joinResultCode: 0).Message(reply).Submit();
        IReadOnlyList<Message> replies =
            (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts;
        using (replies[0])
        {
            SampleSupport.EnsureEqual("accepted", replies[0].GetString(),
                "join reply");
        }

        using Message inbound = Message.From("move:north");
        stream.SendBoundActor(sessionRid, actor.Ref.ActorId).Message(inbound).Submit();
        SampleSupport.WaitOrThrow(
            () => payloads.Contains("move:north"),
            5000,
            "actor payload");

        Console.WriteLine("[actor/room] stream payload: \"move:north\" -> actor: \"move:north\"");
        Zlink.MultipartClose(await actor.Leave(spot)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async()
            .WaitAsync(TimeSpan.FromSeconds(5)));
    }
}
