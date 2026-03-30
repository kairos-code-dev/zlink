using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var dealer = new DealerSocket(ctx);
using var router = new RouterSocket(ctx);
string endpoint = SampleSupport.NewEndpoint("inproc", "dealer-router-recv");
router.Bind(endpoint);
dealer.Connect(endpoint);

SampleSupport.SendUtf8UntilReady(dealer, "dealer-request", 2000);
Received received = router.Receive();
string routingId = received.RoutingId;
using (Message request = received.Parts[0])
{
    Console.WriteLine($"request:{request.GetString()}");
}

using var reply = Message.FromString("dealer-reply");
router.Send(routingId, reply);
Console.WriteLine(SampleSupport.ReceiveUtf8(dealer, 2000));
