using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

static async Task JoinAndAccept(ISpot spot, IActor actor)
{
    using Message joinMessage = Message.From("join:queue");
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
    }, 2000, "actor queue join request");
    using Message reply = Message.From("accepted:queue");
    spot.ReplyActorJoin(request!, joinResultCode: 0).Message(reply).Submit();
    foreach (Message part in (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts)
        part.Dispose();
}

using var ctx = Zlink.CreateContext();
using var node = ctx.CreateSpotNode();
using var spot = node.CreateSpot();
using var actor = node.CreateActor("solo-player-1");
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
stream.AttachActorGateway(node);
Zlink.MultipartClose(await stream.BindActor(sessionRid.Value, actor.Ref)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync()
    .WaitAsync(TimeSpan.FromSeconds(5)));

await JoinAndAccept(spot, actor);

using Message first = Message.From("queue:first");
stream.SendBoundActor(sessionRid.Value, actor.Ref.ActorId)
    .Message(first)
    .Submit();
SampleSupport.WaitOrThrow(
    () => actorMessages.Contains("queue:first"),
    5000,
    "first actor message");

Zlink.MultipartClose(await actor.Leave(spot)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync()
    .WaitAsync(TimeSpan.FromSeconds(5)));
using Message second = Message.From("queue:second");
stream.SendBoundActor(sessionRid.Value, actor.Ref.ActorId)
    .Message(second)
    .Submit();
await JoinAndAccept(spot, actor);
SampleSupport.WaitOrThrow(
    () => actorMessages.Contains("queue:second"),
    5000,
    "queued actor message");

Console.WriteLine("[actor/queue] preserved actor message across rejoin");
Zlink.MultipartClose(await actor.Leave(spot)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync()
    .WaitAsync(TimeSpan.FromSeconds(5)));
Zlink.MultipartClose(await stream.UnbindActor(sessionRid.Value, actor.Ref.ActorId)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync()
    .WaitAsync(TimeSpan.FromSeconds(5)));
