using SampleCommon;
using System;
using System.Threading.Tasks;
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
Exception? attachError = null;
DateTime attachDeadline = DateTime.UtcNow.AddSeconds(5);
while (DateTime.UtcNow < attachDeadline)
{
    try
    {
        node.AttachPubSub(serviceName, pubSocket, subSocket);
        attachError = null;
        break;
    }
    catch (Exception ex)
    {
        attachError = ex;
        Task.Delay(10).Wait();
    }
}
if (attachError != null)
    throw attachError;

var delivered = new TaskCompletionSource<(string Service, string Topic, string Payload)>(
    TaskCreationOptions.RunContinuationsAsynchronously);
spot.OnDispatchEvent(dispatchEvent =>
{
    if (dispatchEvent != SpotDispatchEvent.SubscribeReadable)
        return;
    try
    {
        using TopicMessage subscribed = spot.Subscribe(RecvFlags.DontWait);
        delivered.TrySetResult((
            subscribed.ServiceName ?? string.Empty,
            subscribed.Topic,
            subscribed.SinglePartOrThrow().GetString()));
    }
    catch (Exception ex)
    {
        delivered.TrySetException(ex);
    }
});
spot.SetSubscription(topic);

using (Message message = Message.FromString(payload))
{
    spot.Publish(serviceName, topic, message);
}
if (!delivered.Task.Wait(TimeSpan.FromSeconds(5)))
    throw new InvalidOperationException("spot dispatch callback did not deliver");
var received = delivered.Task.GetAwaiter().GetResult();
if (received.Service != serviceName)
    throw new InvalidOperationException("unexpected spot service");
Console.WriteLine(
    $"[spot/recv] service: \"{serviceName}\" tick: 1 publish: \"{topic}/{payload}\" -> recv: \"{received.Topic}/{received.Payload}\"");
