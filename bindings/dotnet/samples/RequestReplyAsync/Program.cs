using System.Collections.Generic;
using System.Text;
using System.Threading;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var dealerSocket = new DealerSocket(ctx);
using var routerSocket = new RouterSocket(ctx);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var dealerMonitor = dealerSocket.MonitorOpen(SocketEvent.ConnectionReady);
using var routerMonitor = routerSocket.MonitorOpen(SocketEvent.ConnectionReady);
dealerSocket.DealerOptions.RoutingId =
    RoutingId.FromBytes(Encoding.UTF8.GetBytes("request-reply-client"));
routerSocket.Bind(endpoint);
dealerSocket.Connect(endpoint);
SampleSupport.WaitConnected(routerMonitor, dealerMonitor);

using var requestHandled = new ManualResetEventSlim(false);
routerSocket.OnReceive((routingId, parts) =>
{
    try
    {
        using Message part = parts[0];
        SampleSupport.EnsureEqual("ping", part.GetString(), "request");
        using var reply = Message.FromString("pong");
        routerSocket.Send(routingId, reply);
    }
    finally
    {
        for (int i = 1; i < parts.Length; i++)
            parts[i].Dispose();
        requestHandled.Set();
    }
});

using var sent = Message.FromString("ping");
IReadOnlyList<Message> replyReceived = await dealerSocket.RequestAsync(sent,
    TimeSpan.FromSeconds(2));
using Message replyPart = replyReceived[0];
SampleSupport.EnsureEqual("pong", replyPart.GetString(), "reply");
for (int i = 1; i < replyReceived.Count; i++)
    replyReceived[i].Dispose();
SampleSupport.WaitOrThrow(() => requestHandled.IsSet, 2000, "request/reply async sample");
Console.WriteLine("[dealer-router/request-reply/async] send: \"ping\" -> recv: \"pong\"");
