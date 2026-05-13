using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var dealerSocket = new DealerSocket(ctx);
using var routerSocket = new RouterSocket(ctx);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var dealerMonitor = dealerSocket.MonitorOpen(SocketEvent.ConnectionReady);
using var routerMonitor = routerSocket.MonitorOpen(SocketEvent.ConnectionReady);
dealerSocket.SetRoutingId(RoutingId.FromBytes(Encoding.UTF8.GetBytes("request-reply-client")));
routerSocket.Bind(endpoint);
dealerSocket.Connect(endpoint);
SampleSupport.WaitConnected(routerMonitor, dealerMonitor);

using var requestHandled = new ManualResetEventSlim(false);
Task serverTask = Task.Run(() =>
{
    try
    {
        using var received = new Received();
        if (!routerSocket.Recv(received))
            throw new InvalidOperationException("recv failed");
        RoutingId routingId = received.RoutingId
            ?? throw new InvalidOperationException("missing routing id");
        string requestPayload = received.Parts[0].GetString();
        SampleSupport.EnsureEqual("ping", requestPayload, "request");
        using var reply = Message.FromString("pong");
        routerSocket.Reply(routingId, received.RequestSeq ?? 0UL)
            .Message(reply).Submit();
    }
    finally
    {
        requestHandled.Set();
    }
});

using var sent = Message.FromString("ping");
IReadOnlyList<Message> replyReceived = await dealerSocket.Request()
    .Message(sent)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync();
using Message replyPart = replyReceived[0];
SampleSupport.EnsureEqual("pong", replyPart.GetString(), "reply");
for (int i = 1; i < replyReceived.Count; i++)
    replyReceived[i].Dispose();
SampleSupport.WaitOrThrow(() => requestHandled.IsSet, 2000, "request/reply async sample");
await serverTask;
Console.WriteLine("[dealer-router/request-reply/async] send: \"ping\" -> recv: \"pong\"");
