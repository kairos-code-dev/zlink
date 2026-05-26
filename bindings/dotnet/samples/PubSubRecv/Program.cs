using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var publisher = new PubSocket(ctx);
using var subscriber = new SubSocket(ctx);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var publisherMonitor = publisher.MonitorOpen(SocketEvent.ConnectionReady);
using var subscriberMonitor = subscriber.MonitorOpen(SocketEvent.ConnectionReady);
publisher.Bind(endpoint);
subscriber.Connect(endpoint);
SampleSupport.WaitConnected(publisherMonitor, subscriberMonitor);
subscriber.SetSubscription("prices");

using (Message message = Message.From("101.25"))
    publisher.Publish("prices").Message(message).Submit();
string payload = SampleSupport.SubscribeUtf8(subscriber, out string topic, 2000);
Console.WriteLine(
    $"[pubsub/recv] publish: \"prices/101.25\" -> subscribe: \"{topic}/{payload}\"");
