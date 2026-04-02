using SampleCommon;
using Zlink;
using Zlink.Service;
using System.Threading;

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
using var signal = new ManualResetEventSlim(false);
string? output = null;
subscriber.OnSubscribe((receivedTopic, parts) =>
{
    try
    {
        using Message body = parts[0];
        output = $"{receivedTopic}/{body.GetString()}";
        signal.Set();
    }
    finally
    {
        for (int i = 1; i < parts.Length; i++)
            parts[i].Dispose();
    }
});
using (ServiceMonitor pubMonitor = publisher.MonitorOpen(
           ServiceMonitorEvents.SpotFirstDeliveryReadyChanged))
using (ServiceMonitor subMonitor = subscriber.MonitorOpen(
           ServiceMonitorEvents.SpotFilterApplied
           | ServiceMonitorEvents.SpotSubscriptionReadyChanged))
{
    subscriber.SetSubscription(topic);
    ServiceMonitorEvent filterEvent = subMonitor.Recv();
    if ((filterEvent.EventType & ServiceMonitorEvents.SpotFilterApplied)
        == ServiceMonitorEvents.None || filterEvent.Subject != topic)
        throw new InvalidOperationException("unexpected SPOT_FILTER_APPLIED event");
    ServiceMonitorEvent readyEvent = subMonitor.Recv();
    if ((readyEvent.EventType
            & ServiceMonitorEvents.SpotSubscriptionReadyChanged)
        == ServiceMonitorEvents.None || readyEvent.Endpoint != endpoint)
        throw new InvalidOperationException("unexpected SPOT_SUBSCRIPTION_READY_CHANGED event");
    if ((pubMonitor.Snapshot().StateFlags & MonitorState.SendReady) == 0)
        throw new InvalidOperationException("publisher spot monitor is not send-ready");
}
using (Message message = Message.FromString(payload))
    publisher.Publish(topic, message);
SampleSupport.WaitOrThrow(() => signal.IsSet, 5000, "spot callback timeout");
Console.WriteLine($"[spot/callback] publish: \"{topic}/{payload}\" -> subscribe: \"{output}\"");
