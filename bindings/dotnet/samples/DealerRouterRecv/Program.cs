using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var dealer = new DealerSocket(ctx);
using var router = new RouterSocket(ctx);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var dealerMonitor = dealer.MonitorOpen(SocketEvent.ConnectionReady);
using var routerMonitor = router.MonitorOpen(SocketEvent.ConnectionReady);
router.Bind(endpoint);
dealer.Connect(endpoint);
SampleSupport.WaitConnected(routerMonitor, dealerMonitor);

using (Message request = Message.FromString("ping"))
    dealer.Send(request);
using Received received = router.Recv();
RoutingId routingId = received.RoutingId
    ?? throw new InvalidOperationException("missing routing id");
string requestPayload = received.Parts[0].GetString();
SampleSupport.EnsureEqual("ping", requestPayload, "request");

using var reply = Message.FromString("pong");
router.Send(routingId, reply);
string payload = SampleSupport.ReceiveUtf8(dealer, 2000);
Console.WriteLine($"[dealer-router/recv] send: \"ping\" -> recv: \"{payload}\"");
