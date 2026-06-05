// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
//   dotnet run --project samples/SpotPubSubExample
using System.Net;
using System.Net.Sockets;
using Systems.Zlink;

internal static class Program
{
    private static void Main(string[] args)
    {
        // 빈 TCP 포트를 잡아 endpoint를 만든다.
        static string UniqueTcp()
        {
            var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            listener.Stop();
            return $"tcp://127.0.0.1:{port}";
        }

        static void WaitPeer(ISpotNode node)
        {
            DateTime deadline = DateTime.UtcNow.AddSeconds(15);
            while (DateTime.UtcNow < deadline)
            {
                if (node.Status().ConnectedPeerCount > 0)
                    return;
                Thread.Sleep(10);
            }
            throw new TimeoutException("spot peer not connected");
        }

        // --8<-- [start:doc]
        string topic = "room:lobby";
        string pubEndpoint = UniqueTcp();
        string subEndpoint = UniqueTcp();

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
        WaitPeer(publisherNode);
        WaitPeer(subscriberNode);

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
