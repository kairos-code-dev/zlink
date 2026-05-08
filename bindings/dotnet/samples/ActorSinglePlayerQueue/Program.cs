using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

static async Task JoinAndAccept(Spot spot, Actor actor)
{
    using Message joinMessage = Message.FromString("join:queue");
    Task<IReadOnlyList<Message>> joinTask = actor.Join(spot, joinMessage,
        TimeSpan.FromSeconds(2));
    ActorJoinRequest? request = null;
    SampleSupport.WaitOrThrow(() =>
    {
        request = spot.RecvActorJoin(RecvFlags.DontWait);
        return request != null;
    }, 2000, "actor queue join request");
    using Message reply = Message.FromString("accepted:queue");
    spot.ReplyActorJoin(request!, accepted: true, reply);
    foreach (Message part in await joinTask.WaitAsync(TimeSpan.FromSeconds(5)))
        part.Dispose();
}

using var ctx = new Context();
using var node = new SpotNode(ctx);
using var spot = node.CreateSpot();
using var actor = node.CreateActor("solo-player-1");
List<string> actorMessages = new();
using var sessionReady = new ManualResetEventSlim(false);
RoutingId? sessionRid = null;

spot.OnDispatchEvent(info =>
{
    ActorPart? part;
    while ((part = info.RecvActorPart()) != null)
    {
        using (part.Message)
            actorMessages.Add(part.Message.GetString());
    }
});

using var stream = new StreamSocket(ctx);
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
stream.BindActor(node, sessionRid.Value, actor.Ref, TimeSpan.FromSeconds(2));

await JoinAndAccept(spot, actor);

using Message first = Message.FromString("queue:first");
stream.SendBoundActor(node, sessionRid.Value, actor.Ref.ActorId, first);
SampleSupport.WaitOrThrow(
    () => actorMessages.Contains("queue:first"),
    5000,
    "first actor message");

actor.Leave(spot);
using Message second = Message.FromString("queue:second");
stream.SendBoundActor(node, sessionRid.Value, actor.Ref.ActorId, second);
await JoinAndAccept(spot, actor);
SampleSupport.WaitOrThrow(
    () => actorMessages.Contains("queue:second"),
    5000,
    "queued actor message");

Console.WriteLine("[actor/queue] preserved actor message across rejoin");
actor.Leave(spot);
stream.UnbindActor(node, sessionRid.Value, actor.Ref.ActorId,
    TimeSpan.FromSeconds(2));
