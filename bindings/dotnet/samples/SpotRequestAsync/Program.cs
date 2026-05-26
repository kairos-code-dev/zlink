using System.Threading.Tasks;
using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var requesterNode = new SpotNode(ctx);
using var requesterDealer = new DealerSocket(ctx);
using var responderRouter = new RouterSocket(ctx);
using var requester = requesterNode.CreateSpot();
string endpoint = SampleSupport.NewEndpoint("tcp", "spot-request-async");
const string channelName = "orders";
responderRouter.Bind(endpoint);
requesterDealer.Connect(endpoint);
requesterNode.AttachChannelDealerManual(channelName, requesterDealer);

Task responderTask = Task.Run(() =>
{
    using var received = new Received();
    if (!responderRouter.Recv(received))
        throw new InvalidOperationException("recv failed");
    RoutingId routingId = received.RoutingId
        ?? throw new InvalidOperationException("missing routing id");
    string requestPayload = received.Parts[0].GetString();
    SampleSupport.EnsureEqual("spot-ping", requestPayload, "request");
    using var reply = Message.From("spot-pong");
    responderRouter.Reply(routingId, received.RequestSeq ?? 0UL)
        .Message(reply).Submit();
});

using var request = Message.From("spot-ping");
var replyParts = await requester.RequestChannel(channelName)
    .Message(request)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync();
using Message replyPart = replyParts[0];
SampleSupport.EnsureEqual("spot-pong", replyPart.GetString(), "reply");
for (int i = 1; i < replyParts.Count; i++)
    replyParts[i].Dispose();
await responderTask;
Console.WriteLine(
    "[spot/request/async] request: \"spot-ping\" -> reply: \"spot-pong\"");
