using System.Threading;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var dealer = new Socket(ctx, SocketType.Dealer);
using var router = new Socket(ctx, SocketType.Router);
string endpoint = SampleSupport.NewEndpoint("inproc",
    "dealer-router-callback");
router.Bind(endpoint);
dealer.Connect(endpoint);

using var signal = new ManualResetEventSlim(false);
string? output = null;
router.RecvHandler((routingId, parts) =>
{
    using (parts[0])
        output = $"{routingId}:{parts[0].GetString()}";
    signal.Set();
});

SampleSupport.SendUtf8UntilReady(dealer, "dealer-callback", 2000);
SampleSupport.WaitOrThrow(() => signal.IsSet, 2000,
    "dealer/router callback timeout");
Console.WriteLine(output);
