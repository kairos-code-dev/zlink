// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
//   dotnet run --project samples/SpotPubSubExample
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static void Main(string[] args)
    {
        // --8<-- [start:doc]
        string topic = "room:lobby";
        string pubEndpoint = SampleSupport.NewEndpoint("tcp", "spot-pub");
        string subEndpoint = SampleSupport.NewEndpoint("tcp", "spot-sub");

        using var ctx = Zlink.CreateContext();
        // 토픽을 발행하는 노드와 구독하는 노드.
        using var publisherNode = ctx.CreateSpotNode();
        using var subscriberNode = ctx.CreateSpotNode();
        publisherNode.SetPubBind(pubEndpoint);
        subscriberNode.SetPubBind(subEndpoint);
        publisherNode.ConnectPeer(subEndpoint);
        subscriberNode.ConnectPeer(pubEndpoint);

        using var publisher = publisherNode.CreateSpot();
        using var subscriber = subscriberNode.CreateSpot();
        // 구독자는 받을 토픽을 등록한다.
        subscriber.SetSubscription(topic);
        SampleSupport.WaitSpotPeerConnected(publisherNode);
        SampleSupport.WaitSpotPeerConnected(subscriberNode);

        // 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
        using var received = new TopicMessage();
        bool delivered = false;
        DateTime deadline = DateTime.UtcNow.AddSeconds(5);
        while (DateTime.UtcNow < deadline)
        {
            using (Message m = Message.From("hello-everyone"))
                publisher.Publish(topic).Message(m).Submit();
            if (subscriber.Subscribe(received, RecvFlags.DontWait))
            {
                delivered = true;
                break;
            }
            Thread.Sleep(10);
        }
        if (!delivered)
            throw new Exception("spot delivery did not arrive");

        Console.WriteLine($"[spot/pubsub] topic \"{received.Topic}\" -> recv: \"{received.SinglePartOrThrow().GetString()}\"");
        // --8<-- [end:doc]
    }
}
