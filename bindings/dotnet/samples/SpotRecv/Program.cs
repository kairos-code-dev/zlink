using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var registry = new Registry(ctx);
using var discovery = new Discovery(ctx, ServiceType.Spot, "sample");
using var publisherNode = new SpotNode(ctx);
using var subscriberNode = new SpotNode(ctx);
using var publisher = publisherNode.CreateSpot();
using var subscriber = subscriberNode.CreateSpot();
const string serviceName = "sample";
const string topic = "room:lobby";
const string payload = "hello-spot";
string registryPub = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string registryRouter = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string publisherEndpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string subscriberEndpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
registry.Bind(registryPub, registryRouter);
discovery.ConnectRegistry(registryRouter);
publisherNode.AttachDiscovery(discovery);
subscriberNode.AttachDiscovery(discovery);
publisherNode.Bind(publisherEndpoint);
subscriberNode.Bind(subscriberEndpoint);
subscriber.SetSubscription(topic);

TopicMessage? subscribed = null;
SampleSupport.WaitOrThrow(() =>
{
    try
    {
        using Message message = Message.FromString(payload);
        publisher.Publish(serviceName, topic, message);
        subscribed = subscriber.Subscribe(RecvFlags.DontWait);
        return true;
    }
    catch (ZlinkRecvException ex) when (ex.Result == RecvResult.NoData)
    {
        return false;
    }
}, 5000, "spot recv sample");

using var subscribedMessage = subscribed!;
if (subscribedMessage.ServiceName != serviceName)
    throw new InvalidOperationException("unexpected service name");
string receivedTopic = subscribedMessage.Topic;
string receivedPayload = subscribedMessage.SinglePartOrThrow().GetString();
Console.WriteLine(
    $"[spot/recv] service: \"{serviceName}\" tick: 1 publish: \"{topic}/{payload}\" -> recv: \"{receivedTopic}/{receivedPayload}\"");
