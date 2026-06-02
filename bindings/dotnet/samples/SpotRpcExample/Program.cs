// 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
//   dotnet run --project samples/SpotRpcExample
using System.Net;
using System.Net.Sockets;
using Systems.Zlink;

// --8<-- [start:doc]
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

using var ctx = Zlink.CreateContext();
using var serverNode = ctx.CreateSpotNode();
using var clientNode = ctx.CreateSpotNode();
using var server = serverNode.CreateSpot();
using var client = clientNode.CreateSpot();

serverNode.SetRoutingId(RoutingId.From("rpc-server-node"));
clientNode.SetRoutingId(RoutingId.From("rpc-client-node"));
server.SetRoutingId(RoutingId.From("rpc-server-spot"));
client.SetRoutingId(RoutingId.From("rpc-client-spot"));
// 라우티드 평면은 ROUTER bind가 필요하다 (pub bind보다 먼저).
serverNode.SetRouterBind(UniqueTcp());
clientNode.SetRouterBind(UniqueTcp());
string serverEndpoint = UniqueTcp();
string clientEndpoint = UniqueTcp();
serverNode.SetPubBind(serverEndpoint);
clientNode.SetPubBind(clientEndpoint);
serverNode.ConnectPeer(clientEndpoint);
clientNode.ConnectPeer(serverEndpoint);

// 서버 Spot은 라우티드 요청을 받아 같은 평면으로 응답한다.
server.SetDispatchHandler(info =>
{
    if (info.Event != SpotDispatchEvent.RoutedReadable)
        return;
    var received = Received.Create();
    while (true)
    {
        bool got;
        try { got = server.RecvRouted(received, RecvFlags.DontWait); }
        catch { break; }
        if (!got)
            break;
        using Message reply = Message.From("pong");
        received.Reply().Message(reply).Submit();
    }
});

WaitPeer(serverNode);
WaitPeer(clientNode);

// 클라이언트 Spot이 서버 Spot으로 요청한다.
IReadOnlyList<Message> reply = await client
    .RequestToSpot(RoutingId.From("rpc-server-node"), RoutingId.From("rpc-server-spot"))
    .Message(Message.From("ping"))
    .Timeout(TimeSpan.FromSeconds(3))
    .SubmitAsync();
Console.WriteLine($"[spot/rpc] request \"ping\" -> reply \"{reply[0].GetString()}\"");
Zlink.MultipartClose(reply);
// --8<-- [end:doc]
