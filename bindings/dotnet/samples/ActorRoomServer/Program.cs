using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var node = new SpotNode(ctx);
using var spot = node.CreateSpot();
using var actor = node.CreateActor("room-player-1");
using var stream = new StreamSocket(ctx);
stream.AttachActorGateway(node);
RoutingId sessionRid = SampleSupport.RoutingIdUtf8("room-session");
Zlink.MultipartClose(await stream.BindActor(sessionRid, actor.Ref)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync()
    .WaitAsync(TimeSpan.FromSeconds(5)));
using Message joinMessage = Message.FromString("join:lobby");

Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> joinTask =
    actor.Join(spot)
        .Message(joinMessage)
        .Timeout(TimeSpan.FromSeconds(2))
        .SubmitAsync();
ActorJoinRequest? request = null;
SampleSupport.WaitOrThrow(() =>
{
    request = spot.RecvActorJoin(RecvFlags.DontWait);
    return request != null;
}, 2000, "actor join request");

using (request!.Message)
{
    SampleSupport.EnsureEqual("join:lobby", request.Message.GetString(),
        "join message");
}

using Message reply = Message.FromString("accepted:lobby");
spot.ReplyActorJoin(request, joinResultCode: 0).Message(reply).Submit();
IReadOnlyList<Message> replies =
    (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts;
using (replies[0])
{
    SampleSupport.EnsureEqual("accepted:lobby", replies[0].GetString(),
        "join reply");
}

Console.WriteLine("[actor/room] joined actor room-player-1");
Zlink.MultipartClose(await actor.Leave(spot)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync()
    .WaitAsync(TimeSpan.FromSeconds(5)));
