using System.Text;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

static RoutingId PublicRoutingId(string value)
{
    return value.StartsWith("hex:", StringComparison.OrdinalIgnoreCase)
        ? RoutingId.FromString(value[4..])
        : SampleSupport.RoutingIdUtf8(value);
}

using var ctx = new Context();
using var node = new SpotNode(ctx);
using var spot = node.CreateSpot();
using var actor = node.Actor("gateway-player-1");
using var sessionReady = new ManualResetEventSlim(false);
RoutingId? sessionRid = null;
string receivedPayload = "";

spot.OnDispatchEvent((_, info) =>
{
    ActorPart? part = info.RecvActorPart();
    if (part == null)
        return;
    using (part.Message)
    {
        receivedPayload = part.Message.GetString();
    }
});

using Message joinMessage = Message.FromString("join:gateway");
Task<IReadOnlyList<Message>> joinTask = actor.JoinAsync(spot, joinMessage,
    TimeSpan.FromSeconds(2));
ActorJoinRequest? request = null;
SampleSupport.WaitOrThrow(() =>
{
    request = spot.RecvActorJoin(RecvFlags.DontWait);
    return request != null;
}, 2000, "actor join request");
using Message joinReply = Message.FromString("accepted:gateway");
spot.ReplyActorJoin(request!.Info, accepted: true, joinReply);
foreach (Message reply in await joinTask.WaitAsync(TimeSpan.FromSeconds(5)))
    reply.Dispose();

using var stream = new StreamSocket(ctx);
string endpoint = SampleSupport.NewEndpoint("tcp", "actor-gateway");
int port = SampleSupport.ExtractPort(endpoint);
stream.Bind(endpoint);
stream.OnPacket((routingId, payload) =>
{
    sessionRid = PublicRoutingId(routingId);
    using (payload)
    {
        receivedPayload = Encoding.UTF8.GetString(payload.AsReadOnlySpan());
    }
    sessionReady.Set();
    return 0;
});

using var client = SampleSupport.ConnectRawClient(port);
SampleSupport.SendAll(client.GetStream(), "hello-gateway"u8);
if (!sessionReady.Wait(5000) || sessionRid == null)
    throw new TimeoutException("stream session");

stream.BindActor(node, sessionRid.Value, actor.Ref, TimeSpan.FromSeconds(2));
using Message relayed = Message.FromString("relay:hello-gateway");
stream.SendBoundActor(node, sessionRid.Value, actor.Ref.ActorId, relayed);
SampleSupport.WaitOrThrow(
    () => receivedPayload == "relay:hello-gateway",
    5000,
    "actor relay");

Console.WriteLine("[actor/gateway] relayed stream session to actor");
stream.UnbindActor(node, sessionRid.Value, actor.Ref.ActorId,
    TimeSpan.FromSeconds(2));
actor.Leave(spot);
