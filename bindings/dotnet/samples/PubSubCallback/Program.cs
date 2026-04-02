using System.Threading;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var publisher = new XPubSocket(ctx);
using var subscriber = new SubSocket(ctx);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var publisherMonitor = publisher.MonitorOpen(SocketEvent.ConnectionReady);
using var subscriberMonitor = subscriber.MonitorOpen(SocketEvent.ConnectionReady);
publisher.Bind(endpoint);
subscriber.Connect(endpoint);
SampleSupport.WaitConnected(publisherMonitor, subscriberMonitor);
subscriber.SetSubscription("prices");

SubscriptionEvent subscriptionEvent = publisher.ReceiveSubscriptionEvent();
if (!subscriptionEvent.Subscribed || subscriptionEvent.Topic != "prices")
    throw new InvalidOperationException("unexpected subscription event");

using var signal = new ManualResetEventSlim(false);
string? output = null;
subscriber.OnSubscribe((routingId, topic, parts) =>
{
    using (parts[0])
        output = $"{topic}/{parts[0].GetString()}";
    signal.Set();
});

using (Message message = Message.FromString("101.25"))
    publisher.Publish("prices", message);
SampleSupport.WaitOrThrow(() => signal.IsSet, 2000,
    "pubsub callback timeout");
Console.WriteLine(
    $"[pubsub/callback] publish: \"prices/101.25\" -> subscribe: \"{output}\"");
