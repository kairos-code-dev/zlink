// 자립형 가이드 예제: SPOT → 채널(DEALER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
//   dotnet run --project samples/SpotChannelExample
using System.Net;
using System.Net.Sockets;
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        static string UniqueTcp()
        {
            var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            listener.Stop();
            return $"tcp://127.0.0.1:{port}";
        }

        // --8<-- [start:doc]
        using var ctx = Zlink.CreateContext();
        using var roomNode = ctx.CreateSpotNode();
        using var room = roomNode.CreateSpot();
        using var roomDealer = ctx.CreateDealerSocket();
        using var apiRouter = ctx.CreateRouterSocket();

        const string channel = "api";
        string endpoint = UniqueTcp();
        apiRouter.Bind(endpoint);
        roomDealer.Connect(endpoint);
        // "api" 채널 호출을 이 DEALER로 내보내도록 노드에 등록한다.
        roomNode.AttachChannelDealerManual(channel, roomDealer);

        // API 서버(ROUTER)는 별도 스레드에서 요청을 받아 응답한다.
        var server = Task.Run(() =>
        {
            var received = Received.Create();
            if (apiRouter.Recv(received))
            {
                using Message reply = Message.From("profile:level-7");
                received.Reply().Message(reply).Submit();
            }
        });

        // 게임룸이 API 채널로 outgame 요청을 보낸다.
        IReadOnlyList<Message> reply = await room.RequestToChannel(channel)
            .Message(Message.From("get-profile"))
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();
        await server;

        Console.WriteLine($"[spot/channel] request \"get-profile\" -> reply \"{reply[0].GetString()}\"");
        Zlink.MultipartClose(reply);
        // --8<-- [end:doc]
    }
}
