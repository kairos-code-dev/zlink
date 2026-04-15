using System.Threading;
using SampleCommon;
using Zlink;

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
Received received = router.Recv();
try
{
    RoutingId routingId = received.RoutingId
        ?? throw new InvalidOperationException("missing routing id");
    Message requestPart = received.FirstPart();
    SampleSupport.EnsureEqual("ping", requestPart.GetString(), "request");
    using var reply = Message.FromString("pong");
    router.Send(routingId, reply);
}
finally
{
    foreach (Message part in received.Parts)
        part.Dispose();
}

string payload = SampleSupport.ReceiveUtf8(dealer, 2000);
Console.WriteLine(
    $"[dealer-router/recv] send: \"ping\" -> recv: \"{payload}\"");
