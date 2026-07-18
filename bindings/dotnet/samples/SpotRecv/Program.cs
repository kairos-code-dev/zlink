using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static void Main(string[] args)
    {
        if (!SampleSupport.IsNativeAvailable())
            return;

        using var publisherContext = Zlink.CreateContext();
        using var subscriberContext = Zlink.CreateContext();
        using var publisherNode = publisherContext.CreateMeshNode(
            new MeshNodeOptions { MeshName = "spot-recv" });
        using var subscriberNode = subscriberContext.CreateMeshNode(
            new MeshNodeOptions { MeshName = "spot-recv" });
        const string serviceName = "direct";
        const string channel = "room";
        const string topic = "room:lobby";
        const string payload = "hello-spot";
        string publisherEndpoint = SampleSupport.NewEndpoint("tcp", "sample");
        string subscriberEndpoint = SampleSupport.NewEndpoint("tcp", "sample");
        publisherNode.AddChannel(channel);
        subscriberNode.AddChannel(channel);
        publisherNode.SetBind(publisherEndpoint);
        subscriberNode.SetBind(subscriberEndpoint);
        publisherNode.SetRoutingId(RoutingId.From("spot-recv-publisher"));
        subscriberNode.SetRoutingId(RoutingId.From("spot-recv-subscriber"));
        publisherNode.Start();
        subscriberNode.Start();
        publisherNode.ConnectPeer(subscriberEndpoint);
        subscriberNode.ConnectPeer(publisherEndpoint);
        using var publisher = publisherNode.CreateSpot();
        using var subscriber = subscriberNode.CreateSpot();
        subscriber.SetSubscription(channel, topic);

        SampleSupport.WaitOrThrow(
            () => publisherNode.Status().AdmittedPeerCount > 0
                && subscriberNode.Status().AdmittedPeerCount > 0,
            5000,
            "spot peer readiness");

        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        string? receivedTopic = null;
        string? receivedPayload = null;
        SampleSupport.WaitOrThrow(() =>
        {
            using (Message message = Message.From(payload))
                publisher.Publish(channel, topic, new[] { message });
            SampleSupport.PumpReady(subscriberNode, ready, recv, (record, batch, index) =>
            {
                if (record.Kind != MeshRecordKind.SpotMulticast)
                    return;
                Message[] parts = batch.RetainMessage(index);
                receivedTopic = record.Topic;
                receivedPayload = parts[0].GetString();
                Zlink.MultipartClose(parts);
            });
            return receivedPayload != null;
        }, 5000, "spot recv sample");

        Console.WriteLine(
            $"[spot/recv] service: \"{serviceName}\" tick: 1 publish: \"{topic}/{payload}\" -> recv: \"{receivedTopic}/{receivedPayload}\"");
    }
}
