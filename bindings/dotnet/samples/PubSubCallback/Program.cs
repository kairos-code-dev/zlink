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

using (Message message = Message.FromString("101.25"))
    publisher.Publish("prices", message);
string output = SampleSupport.SubscribeUtf8(subscriber, out string topic, 2000);
Console.WriteLine(
    $"[pubsub/recv] publish: \"prices/101.25\" -> subscribe: \"{topic}/{output}\"");
