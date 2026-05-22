using System.Text;
using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var node = new SpotNode(ctx);
using var spot = node.CreateSpot();
using var actor = node.CreateActor("gateway-player-1");
using var sessionReady = new ManualResetEventSlim(false);
RoutingId? sessionRid = null;
string receivedPayload = "";

spot.OnDispatchEvent(info =>
{
    ActorPart? part = info.RecvActorPart();
    if (part == null)
        return;
    using (part.Message)
    {
        receivedPayload = part.Message.GetString();
    }
});

using var stream = new StreamSocket(ctx);
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
    .SubmitAsync()
    .WaitAsync(TimeSpan.FromSeconds(5)));

using Message joinMessage = Message.FromString("join:gateway");
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
using Message joinReply = Message.FromString("accepted:gateway");
spot.ReplyActorJoin(request!, accepted: true).Message(joinReply).Submit();
foreach (Message reply in (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts)
    reply.Dispose();

using Message relayed = Message.FromString("relay:hello-gateway");
stream.SendBoundActor(sessionRid.Value, actor.Ref.ActorId)
    .Message(relayed)
    .Submit();
SampleSupport.WaitOrThrow(
    () => receivedPayload == "relay:hello-gateway",
    5000,
    "actor relay");

Console.WriteLine("[actor/gateway] relayed stream session to actor");
Zlink.MultipartClose(await actor.Leave(spot)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync()
    .WaitAsync(TimeSpan.FromSeconds(5)));
Zlink.MultipartClose(await stream.UnbindActor(sessionRid.Value, actor.Ref.ActorId)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync()
    .WaitAsync(TimeSpan.FromSeconds(5)));
