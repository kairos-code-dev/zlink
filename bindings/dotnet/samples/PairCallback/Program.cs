using System.Threading;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var sender = new PairSocket(ctx);
using var receiver = new PairSocket(ctx);
string endpoint = SampleSupport.NewEndpoint("tcp", "pair-callback");
sender.Bind(endpoint);
receiver.Connect(endpoint);

using var signal = new ManualResetEventSlim(false);
string? payload = null;
receiver.RecvHandler((routingId, parts) =>
{
    using (parts[0])
        payload = parts[0].GetString();
    signal.Set();
});

SampleSupport.SendUtf8UntilReady(sender, "pair-callback", 2000);
SampleSupport.WaitOrThrow(() => signal.IsSet, 2000, "pair callback timeout");
Console.WriteLine(payload);
