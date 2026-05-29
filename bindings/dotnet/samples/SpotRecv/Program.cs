using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var publisherContext = Zlink.CreateContext();
using var subscriberContext = Zlink.CreateContext();
using var publisherNode = publisherContext.CreateSpotNode();
using var subscriberNode = subscriberContext.CreateSpotNode();
const string serviceName = "direct";
const string topic = "room:lobby";
const string payload = "hello-spot";
string publisherEndpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string subscriberEndpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
publisherNode.SetRoutingId(SampleSupport.RoutingIdUtf8("z-sample-spot-pub"));
subscriberNode.SetRoutingId(SampleSupport.RoutingIdUtf8("a-sample-spot-sub"));
publisherNode.SetPubBind(publisherEndpoint);
subscriberNode.SetPubBind(subscriberEndpoint);
publisherNode.ConnectPeer(subscriberEndpoint);
subscriberNode.ConnectPeer(publisherEndpoint);
using var publisher = publisherNode.CreateSpot();
using var subscriber = subscriberNode.CreateSpot();
publisher.SetRoutingId(SampleSupport.RoutingIdUtf8("z-sample-spot-pub-spot"));
subscriber.SetRoutingId(SampleSupport.RoutingIdUtf8("a-sample-spot-sub-spot"));
subscriber.SetSubscription(topic);

SampleSupport.WaitOrThrow(
    () => publisherNode.Status().ConnectedPeerCount > 0
        && subscriberNode.Status().ConnectedPeerCount > 0,
    5000,
    "spot peer readiness");

using var subscribed = new TopicMessage();
SampleSupport.WaitOrThrow(() =>
{
    try
    {
        using Message message = Message.From(payload);
        publisher.Publish(topic).Message(message).Submit();
        return subscriber.Subscribe(subscribed, RecvFlags.DontWait);
    }
    catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
    {
        return false;
    }
}, 5000, "spot recv sample");

string receivedTopic = subscribed.Topic;
string receivedPayload = subscribed.SinglePartOrThrow().GetString();
Console.WriteLine(
    $"[spot/recv] service: \"{serviceName}\" tick: 1 publish: \"{topic}/{payload}\" -> recv: \"{receivedTopic}/{receivedPayload}\"");
