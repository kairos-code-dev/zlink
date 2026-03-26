using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var publisher = new Socket(ctx, SocketType.Pub);
using var subscriber = new Socket(ctx, SocketType.Sub);
string endpoint = SampleSupport.NewEndpoint("inproc", "pubsub-recv");
publisher.Bind(endpoint);
subscriber.Connect(endpoint);
subscriber.SetSubscription("prices");

SampleSupport.PublishUtf8UntilReady(publisher, "prices", "42", 2000);
string payload = SampleSupport.SubscribeUtf8(subscriber, out string topic, 2000);
Console.WriteLine($"{topic}:{payload}");
