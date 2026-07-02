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
        using var requesterRouter = ctx.CreateRouterSocket();
        using var responderRouter = ctx.CreateRouterSocket();
        using var bridge = requesterNode.CreateRouteBridge();
        string endpoint = SampleSupport.NewEndpoint("tcp", "spot-request-async");
        const string channelName = "orders";
        RoutingId responderNodeRid = RoutingId.From("responder-node");
        requesterRouter.SetRoutingId(RoutingId.From("requester-node"));
        responderRouter.SetRoutingId(responderNodeRid);
        responderRouter.Bind(endpoint);
        requesterRouter.Connect(endpoint);
        bridge.AttachRouterChannel(channelName, requesterRouter);

        Task responderTask = Task.Run(() =>
        {
            using var received = Received.Create();
            if (!responderRouter.Recv(received))
                throw new InvalidOperationException("recv failed");
            RoutingId routingId = received.RoutingId
                ?? throw new InvalidOperationException("missing routing id");
            string requestPayload = received.Parts[2].GetString();
            SampleSupport.EnsureEqual("spot-ping", requestPayload, "request");
            using var reply = Message.From("spot-pong");
            responderRouter.Reply(routingId, received.RequestSeq ?? 0UL)
                .Message(reply).Submit();
            foreach (Message part in received.Parts)
                part.Dispose();
        });

        using var request = Message.From("spot-ping");
        var completion =
            new TaskCompletionSource<IReadOnlyList<Message>>(
                TaskCreationOptions.RunContinuationsAsynchronously);
        bridge.Request(channelName, responderNodeRid, RoutingId.From("requester-spot"),
            new[] { request }, (result, parts) =>
            {
                if (result == RequestResult.Ok)
                    completion.SetResult(parts);
                else
                    completion.SetException(
                        new InvalidOperationException($"request failed: {result}"));
            }, timeout: TimeSpan.FromSeconds(2));
        var replyParts = await completion.Task;
        using Message replyPart = replyParts[0];
        SampleSupport.EnsureEqual("spot-pong", replyPart.GetString(), "reply");
        for (int i = 1; i < replyParts.Count; i++)
            replyParts[i].Dispose();
        await responderTask;
        Console.WriteLine(
            "[spot/request/async] request: \"spot-ping\" -> reply: \"spot-pong\"");
    }
}
