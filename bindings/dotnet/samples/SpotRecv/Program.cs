using SampleCommon;
using Zlink;
using Zlink.Service;
using System;
using System.Threading;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var pubNode = new SpotNode(ctx);
using var subNode = new SpotNode(ctx);
using var publisher = new Spot(pubNode);
using var subscriber = new Spot(subNode);
const string topic = "spot:sample";
string endpoint = SampleSupport.NewEndpoint("tcp", "spot-sample");
pubNode.Bind(endpoint);
subNode.ConnectPeer(endpoint);
subscriber.SetSubscription(topic);
DateTime deadline = DateTime.UtcNow.AddMilliseconds(5000);
string payload = string.Empty;
string receivedTopic = string.Empty;
while (DateTime.UtcNow < deadline)
{
    using (Message message = Message.FromString("spot-recv"))
    {
        _ = publisher.TryPublish(topic, message);
    }

    Subscribed? subscribed = subscriber.TrySubscribe();
    if (subscribed == null)
    {
        Thread.Sleep(10);
        continue;
    }

    try
    {
        receivedTopic = subscribed.Topic;
        payload = subscribed.SinglePartOrThrow().GetString();
        break;
    }
    finally
    {
        foreach (Message part in subscribed.Parts)
            part.Dispose();
    }
}

if (payload.Length == 0)
    throw new TimeoutException("spot recv timeout");

Console.WriteLine($"{receivedTopic}:{payload}");
