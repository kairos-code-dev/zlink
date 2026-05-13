using SampleCommon;
using System.Linq;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var registry = new Registry(ctx);
using var publisherDiscovery = new Discovery(ctx, AutoConnectType.SpotMesh,
    "sample");
using var subscriberDiscovery = new Discovery(ctx, AutoConnectType.SpotMesh,
    "sample");
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
publisherNode.SetRoutingId(SampleSupport.RoutingIdUtf8("z-sample-spot-pub"));
subscriberNode.SetRoutingId(SampleSupport.RoutingIdUtf8("a-sample-spot-sub"));
publisher.SetRoutingId(SampleSupport.RoutingIdUtf8("z-sample-spot-pub-spot"));
subscriber.SetRoutingId(SampleSupport.RoutingIdUtf8("a-sample-spot-sub-spot"));
registry.Bind(registryPub, registryRouter);
registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
publisherDiscovery.ConnectRegistry(registryRouter);
subscriberDiscovery.ConnectRegistry(registryRouter);
publisherNode.Bind(publisherEndpoint);
subscriberNode.Bind(subscriberEndpoint);
publisherNode.AttachDiscovery(publisherDiscovery);
subscriberNode.AttachDiscovery(subscriberDiscovery);
subscriber.SetSubscription(topic);

SampleSupport.WaitOrThrow(
    () => publisherNode.StatusSnapshot().ConnectedPeerCount > 0
        && subscriberNode.StatusSnapshot().ConnectedPeerCount > 0
        && subscriberNode.SubjectsSnapshot().Any(entry => entry.Subject == topic),
    5000,
    "spot discovery readiness");

TopicMessage? subscribed = null;
SampleSupport.WaitOrThrow(() =>
{
    try
    {
        using Message message = Message.FromString(payload);
        publisher.Publish(topic).Message(message).Submit();
        subscribed = subscriber.Subscribe(RecvFlags.DontWait);
        return subscribed is not null;
    }
    catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
    {
        return false;
    }
}, 5000, "spot recv sample");

using var subscribedMessage = subscribed!;
string receivedTopic = subscribedMessage.Topic;
string receivedPayload = subscribedMessage.SinglePartOrThrow().GetString();
Console.WriteLine(
    $"[spot/recv] service: \"{serviceName}\" tick: 1 publish: \"{topic}/{payload}\" -> recv: \"{receivedTopic}/{receivedPayload}\"");
