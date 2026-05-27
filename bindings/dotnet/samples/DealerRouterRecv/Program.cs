using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = Zlink.CreateContext();
using var dealer = ctx.CreateDealerSocket();
using var router = ctx.CreateRouterSocket();
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var dealerMonitor = dealer.MonitorOpen(SocketEvent.ConnectionReady);
using var routerMonitor = router.MonitorOpen(SocketEvent.ConnectionReady);
router.Bind(endpoint);
dealer.Connect(endpoint);
SampleSupport.WaitConnected(routerMonitor, dealerMonitor);

using (Message request = Message.From("ping"))
    dealer.Send().Message(request).Submit();
using var received = new Received();
if (!router.Recv(received))
    throw new InvalidOperationException("recv failed");
string requestPayload = received.Parts[0].GetString();
SampleSupport.EnsureEqual("ping", requestPayload, "request");

using var reply = Message.From("pong");
received.Send().Message(reply).Submit();
string payload = SampleSupport.ReceiveUtf8(dealer, 2000);
Console.WriteLine($"[dealer-router/recv] send: \"ping\" -> recv: \"{payload}\"");
