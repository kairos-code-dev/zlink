using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var pubNode = new SpotNode(ctx);
using var subNode = new SpotNode(ctx);
using var publisher = pubNode.CreateSpot();
using var subscriber = subNode.CreateSpot();
const string serviceName = "room:lobby";
const string topic = "room:lobby";
const string payload = "hello-spot";
pubNode.Bind("tcp://127.0.0.1:0");
string endpoint = pubNode.LastEndpoint;
subNode.ConnectPeer(endpoint);
subscriber.SetSubscription(topic);
SampleSupport.WaitSpotPeerConnected(subNode);

using (Message message = Message.FromString(payload))
    publisher.Publish(serviceName, topic, message);

using TopicMessage subscribed = subscriber.Subscribe();
string receivedTopic = subscribed.Topic;
string receivedPayload = subscribed.SinglePartOrThrow().GetString();
Console.WriteLine(
    $"[spot/recv] publish: \"{serviceName}/{topic}/{payload}\" -> subscribe: \"{subscribed.ServiceName}/{receivedTopic}/{receivedPayload}\"");
