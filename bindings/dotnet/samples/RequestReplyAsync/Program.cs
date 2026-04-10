using System.Threading;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var dealerSocket = new DealerSocket(ctx);
using var routerSocket = new RouterSocket(ctx);
using var dealer = new RequestDealer(dealerSocket);
using var router = new RequestRouter(routerSocket);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var dealerMonitor = dealerSocket.MonitorOpen(SocketEvent.ConnectionReady);
using var routerMonitor = routerSocket.MonitorOpen(SocketEvent.ConnectionReady);
dealerSocket.DealerOptions.RoutingId = new RoutingId("request-reply-client");
routerSocket.Bind(endpoint);
dealerSocket.Connect(endpoint);
SampleSupport.WaitConnected(routerMonitor, dealerMonitor);

using var requestHandled = new ManualResetEventSlim(false);
router.OnReceive(received =>
{
    if (!received.HasRequestSequence)
        throw new InvalidOperationException("missing request sequence");
    using Message part = received.Parts[0];
    SampleSupport.EnsureEqual("ping", part.GetString(), "request");
    using var reply = Message.FromString("pong");
    router.Reply(received.RoutingIdValue!, received.RequestSequence, reply);
    requestHandled.Set();
});

using var sent = Message.FromString("ping");
Received replyReceived = await dealer.RequestAsync(sent, TimeSpan.FromSeconds(2));
using Message replyPart = replyReceived.Parts[0];
SampleSupport.EnsureEqual("pong", replyPart.GetString(), "reply");
SampleSupport.WaitOrThrow(() => requestHandled.IsSet, 2000, "request/reply async sample");
Console.WriteLine("[dealer-router/request-reply/async] send: \"ping\" -> recv: \"pong\"");
