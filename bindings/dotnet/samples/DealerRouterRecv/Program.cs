using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var dealer = new Socket(ctx, SocketType.Dealer);
using var router = new Socket(ctx, SocketType.Router);
string endpoint = SampleSupport.NewEndpoint("inproc", "dealer-router-recv");
router.Bind(endpoint);
dealer.Connect(endpoint);

SampleSupport.SendUtf8UntilReady(dealer, "dealer-request", 2000);
router.Receive(out string routingId, out Message request);
using (request)
{
    Console.WriteLine($"request:{request.GetString()}");
}

using var reply = Message.FromString("dealer-reply");
router.Send(routingId, reply);
Console.WriteLine(SampleSupport.ReceiveUtf8(dealer, 2000));
