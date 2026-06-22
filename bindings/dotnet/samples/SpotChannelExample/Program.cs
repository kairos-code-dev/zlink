// 자립형 가이드 예제: SPOT → 채널(DEALER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
//   dotnet run --project samples/SpotChannelExample
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        // --8<-- [start:doc]
        using var ctx = Zlink.CreateContext();
        using var roomNode = ctx.CreateSpotNode();
        using var roomDealer = ctx.CreateDealerSocket();
        using var apiRouter = ctx.CreateRouterSocket();
        using var bridge = roomNode.CreateRouteBridge();

        const string channel = "api";
        string endpoint = SampleSupport.NewEndpoint("tcp", "spot-channel");
        roomDealer.SetRoutingId(RoutingId.From("room-channel-client"));
        apiRouter.Bind(endpoint);
        roomDealer.Connect(endpoint);
        bridge.AttachDealerChannel(channel, roomDealer);

        // API 서버(ROUTER)는 별도 스레드에서 요청을 받아 응답한다.
        var server = Task.Run(() =>
        {
            using var received = Received.Create();
            if (apiRouter.Recv(received))
            {
                string request = received.Parts[2].GetString();
                SampleSupport.EnsureEqual("get-profile", request, "request");
                using Message reply = Message.From("profile:level-7");
                received.Reply().Message(reply).Submit();
            }
            foreach (Message part in received.Parts)
                part.Dispose();
        });

        // 게임룸이 API 채널로 outgame 요청을 보낸다.
        var completion =
            new TaskCompletionSource<IReadOnlyList<Message>>(
                TaskCreationOptions.RunContinuationsAsynchronously);
        using Message requestMessage = Message.From("get-profile");
        RoutingId targetSpot = RoutingId.From("room");
        bridge.Request(channel, targetSpot, new[] { requestMessage },
            (result, replyParts) =>
            {
                if (result == RequestResult.Ok)
                    completion.SetResult(replyParts);
                else
                    completion.SetException(
                        new InvalidOperationException($"request failed: {result}"));
            }, timeout: TimeSpan.FromSeconds(5));
        IReadOnlyList<Message> reply = await completion.Task;
        await server;

        Console.WriteLine($"[spot/channel] request \"get-profile\" -> reply \"{reply[0].GetString()}\"");
        Zlink.MultipartClose(reply);
        // --8<-- [end:doc]
    }
}
