using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var pubSocket = new PubSocket(ctx);
using var subSocket = new SubSocket(ctx);
using var node = new SpotNode(ctx);
using var spot = node.CreateSpot();
const string serviceName = "sample";
const string topic = "room:lobby";
const string payload = "hello-spot";
string endpoint = SampleSupport.NewEndpoint("tcp", "spot-recv-service");
pubSocket.Bind(endpoint);
subSocket.Connect(endpoint);
node.AttachPubSub(serviceName, pubSocket, subSocket);
spot.SetSubscription(topic);

TopicMessage? subscribed = null;
SampleSupport.WaitOrThrow(() =>
{
    using Message message = Message.FromString(payload);
    spot.Publish(serviceName, topic, message);
    try
    {
        subscribed = spot.Subscribe(RecvFlags.DontWait);
        return true;
    }
    catch (ZlinkRecvException)
    {
        return false;
    }
}, 5000, "spot recv sample");

using var subscribedMessage = subscribed!;
string receivedTopic = subscribedMessage.Topic;
string receivedPayload = subscribedMessage.SinglePartOrThrow().GetString();
Console.WriteLine(
    $"[spot/recv] service: \"{serviceName}\" tick: 1 publish: \"{topic}/{payload}\" -> recv: \"{receivedTopic}/{receivedPayload}\"");
