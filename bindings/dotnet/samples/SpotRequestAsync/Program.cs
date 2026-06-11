using System.Threading.Tasks;
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        if (!SampleSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var requesterNode = ctx.CreateSpotNode();
        using var requesterDealer = ctx.CreateDealerSocket();
        using var responderRouter = ctx.CreateRouterSocket();
        using var requester = requesterNode.CreateSpot();
        string endpoint = SampleSupport.NewEndpoint("tcp", "spot-request-async");
        const string channelName = "orders";
        responderRouter.Bind(endpoint);
        requesterDealer.Connect(endpoint);
        requesterNode.AttachChannelDealerManual(channelName, requesterDealer);

        Task responderTask = Task.Run(() =>
        {
            using var received = Received.Create();
            if (!responderRouter.Recv(received))
                throw new InvalidOperationException("recv failed");
            RoutingId routingId = received.RoutingId
                ?? throw new InvalidOperationException("missing routing id");
            string requestPayload = received.Parts[0].GetString();
            SampleSupport.EnsureEqual("spot-ping", requestPayload, "request");
            using var reply = Message.From("spot-pong");
            responderRouter.Reply(routingId, received.RequestSeq ?? 0UL)
                .Message(reply).Submit();
        });

        using var request = Message.From("spot-ping");
        var replyParts = await requester.RequestToChannel(channelName)
            .Message(request)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async();
        using Message replyPart = replyParts[0];
        SampleSupport.EnsureEqual("spot-pong", replyPart.GetString(), "reply");
        for (int i = 1; i < replyParts.Count; i++)
            replyParts[i].Dispose();
        await responderTask;
        Console.WriteLine(
            "[spot/request/async] request: \"spot-ping\" -> reply: \"spot-pong\"");
    }
}
