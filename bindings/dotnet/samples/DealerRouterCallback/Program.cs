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

using var signal = new ManualResetEventSlim(false);
string? payload = null;
router.OnReceive((routingId, parts) =>
{
    using (parts[0])
    {
        SampleSupport.EnsureEqual("ping", parts[0].GetString(), "request");
        using var reply = Message.FromString("pong");
        router.Send(routingId, reply);
    }
    signal.Set();
});

using (Message request = Message.FromString("ping"))
    dealer.Send(request);
SampleSupport.WaitOrThrow(() => signal.IsSet, 2000,
    "dealer/router callback timeout");
payload = SampleSupport.ReceiveUtf8(dealer, 2000);
Console.WriteLine(
    $"[dealer-router/callback] send: \"ping\" -> recv: \"{payload}\"");
