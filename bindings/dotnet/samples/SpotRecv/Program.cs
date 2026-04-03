using SampleCommon;
using Zlink;
using Zlink.Service;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var pubNode = new SpotNode(ctx);
using var subNode = new SpotNode(ctx);
using var publisher = new Spot(pubNode);
using var subscriber = new Spot(subNode);
const string topic = "room:lobby";
const string payload = "hello-spot";
pubNode.Bind("tcp://127.0.0.1:0");
string endpoint = pubNode.LastEndpoint;
subNode.ConnectPeer(endpoint);
subscriber.SetSubscription(topic);
SampleSupport.WaitOrThrow(
    () => subNode.StatusSnapshot().ConnectedPeerCount > 0,
    5000,
    "spot peer connection");

DateTime deadline = DateTime.UtcNow.AddSeconds(5);
while (DateTime.UtcNow < deadline)
{
    using (Message message = Message.FromString(payload))
        publisher.Publish(topic, message);
    if (subscriber.TrySubscribe(out Subscribed? subscribed))
    {
        using (subscribed)
        {
            string receivedTopic = subscribed.Topic;
            string receivedPayload = subscribed.SinglePartOrThrow().GetString();
            Console.WriteLine($"[spot/recv] publish: \"{topic}/{payload}\" -> subscribe: \"{receivedTopic}/{receivedPayload}\"");
            return;
        }
    }
}
throw new InvalidOperationException("spot delivery timed out");
