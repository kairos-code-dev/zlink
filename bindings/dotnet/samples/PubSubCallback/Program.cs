using System.Threading;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var publisher = new PubSocket(ctx);
using var subscriber = new SubSocket(ctx);
string endpoint = SampleSupport.NewEndpoint("inproc", "pubsub-callback");
publisher.Bind(endpoint);
subscriber.Connect(endpoint);
subscriber.SetSubscription("prices");

using var signal = new ManualResetEventSlim(false);
string? output = null;
subscriber.SubscribeHandler((routingId, topic, parts) =>
{
    using (parts[0])
        output = $"{topic}:{parts[0].GetString()}";
    signal.Set();
});

SampleSupport.PublishUtf8UntilReady(publisher, "prices", "84", 2000);
SampleSupport.WaitOrThrow(() => signal.IsSet, 2000,
    "pubsub callback timeout");
Console.WriteLine(output);
