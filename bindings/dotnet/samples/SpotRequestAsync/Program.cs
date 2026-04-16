using System.Threading.Tasks;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var requesterNode = new SpotNode(ctx);
using var responderNode = new SpotNode(ctx);
using var requester = requesterNode.CreateSpot();
using var responder = responderNode.CreateSpot();
string endpoint = SampleSupport.NewEndpoint("tcp", "spot-request-async");
requesterNode.SetRoutingId(RoutingId.FromBytes("spot-requester-node"u8.ToArray()));
responderNode.SetRoutingId(RoutingId.FromBytes("spot-responder-node"u8.ToArray()));
requester.SetRoutingId(RoutingId.FromBytes("spot-requester"u8.ToArray()));
responder.SetRoutingId(RoutingId.FromBytes("spot-responder"u8.ToArray()));
responderNode.Bind(endpoint);
requesterNode.ConnectPeer(endpoint);
SampleSupport.WaitSpotPeerConnected(requesterNode);

Task responderTask = Task.Run(() =>
{
    using Received received = responder.RecvRouted();
    using Message requestPart = received.Parts[0];
    SampleSupport.EnsureEqual("spot-ping", requestPart.GetString(), "request");
    using var reply = Message.FromString("spot-pong");
    received.Reply(reply);
});

using var request = Message.FromString("spot-ping");
var replyParts = await requester.RequestToSpotAsync(
    responderNode.RoutingId,
    responder.RoutingId,
    request,
    TimeSpan.FromSeconds(2));
using Message replyPart = replyParts[0];
SampleSupport.EnsureEqual("spot-pong", replyPart.GetString(), "reply");
for (int i = 1; i < replyParts.Count; i++)
    replyParts[i].Dispose();
await responderTask;
Console.WriteLine(
    "[spot/request/async] request: \"spot-ping\" -> reply: \"spot-pong\"");
