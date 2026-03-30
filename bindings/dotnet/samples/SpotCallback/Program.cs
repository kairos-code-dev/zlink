using SampleCommon;
using Zlink;
using Zlink.Service;
using System.Threading;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var pubNode = new SpotNode(ctx);
using var subNode = new SpotNode(ctx);
using var publisher = new Spot(pubNode);
using var subscriber = new Spot(subNode);
const string topic = "spot:callback";
string endpoint = SampleSupport.NewEndpoint("tcp", "spot-callback");
pubNode.Bind(endpoint);
subNode.ConnectPeer(endpoint);
subscriber.SetSubscription(topic);
using var signal = new ManualResetEventSlim(false);
string? output = null;
subscriber.SubscribeHandler((receivedTopic, parts) =>
{
    try
    {
        using Message payload = parts[0];
        output = $"{receivedTopic}:{payload.GetString()}";
        signal.Set();
    }
    finally
    {
        for (int i = 1; i < parts.Length; i++)
            parts[i].Dispose();
    }
});

DateTime deadline = DateTime.UtcNow.AddMilliseconds(5000);
while (DateTime.UtcNow < deadline && !signal.IsSet)
{
    using Message message = Message.FromString("spot-callback");
    _ = publisher.TryPublish(topic, message);
    if (signal.Wait(100))
        break;
    Thread.Sleep(10);
}

SampleSupport.WaitOrThrow(() => signal.IsSet, 2000, "spot callback timeout");
Console.WriteLine(output);
