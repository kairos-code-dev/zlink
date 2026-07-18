// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 채널 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
//   dotnet run --project samples/SpotPubSubExample
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static void Main(string[] args)
    {
        // --8<-- [start:doc]
        const string channel = "room";
        const string topic = "room:lobby";
        string pubEndpoint = SampleSupport.NewEndpoint("tcp", "spot-pub");
        string subEndpoint = SampleSupport.NewEndpoint("tcp", "spot-sub");

        using var ctx = Zlink.CreateContext();
        // 토픽을 발행하는 노드와 구독하는 노드.
        using var publisherNode = ctx.CreateMeshNode(new MeshNodeOptions { MeshName = "spot-pubsub" });
        using var subscriberNode = ctx.CreateMeshNode(new MeshNodeOptions { MeshName = "spot-pubsub" });
        publisherNode.AddChannel(channel);
        subscriberNode.AddChannel(channel);
        publisherNode.SetBind(pubEndpoint);
        subscriberNode.SetBind(subEndpoint);
        publisherNode.Start();
        subscriberNode.Start();
        publisherNode.ConnectPeer(subEndpoint);
        subscriberNode.ConnectPeer(pubEndpoint);

        using var publisher = publisherNode.CreateSpot();
        using var subscriber = subscriberNode.CreateSpot();
        // 구독자는 받을 채널 토픽을 등록한다.
        subscriber.SetSubscription(channel, topic);
        SampleSupport.WaitSpotPeerConnected(publisherNode);
        SampleSupport.WaitSpotPeerConnected(subscriberNode);

        // 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
        using var ready = new MeshReadyBatch();
        using var recv = new MeshReceiveBatch();
        string? gotTopic = null;
        string? gotPayload = null;
        DateTime deadline = DateTime.UtcNow.AddSeconds(5);
        while (gotPayload == null && DateTime.UtcNow < deadline)
        {
            using (Message m = Message.From("hello-everyone"))
                publisher.Publish(channel, topic, new[] { m });

            // 구독자 노드의 ready 인덱스를 드레인해 멀티캐스트 레코드를 수신한다.
            SampleSupport.PumpReady(subscriberNode, ready, recv, (record, batch, index) =>
            {
                if (record.Kind != MeshRecordKind.SpotMulticast)
                    return;
                Message[] parts = batch.RetainMessage(index);
                gotTopic = record.Topic;
                gotPayload = parts[0].GetString();
                Zlink.MultipartClose(parts);
            });
            if (gotPayload != null)
                break;
            Thread.Sleep(10);
        }
        if (gotPayload == null)
            throw new Exception("spot delivery did not arrive");

        Console.WriteLine($"[spot/pubsub] topic \"{gotTopic}\" -> recv: \"{gotPayload}\"");
        // --8<-- [end:doc]
    }
}
