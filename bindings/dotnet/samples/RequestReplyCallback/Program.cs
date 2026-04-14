using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
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
using var replyHandled = new ManualResetEventSlim(false);
Exception? failure = null;

Task serverTask = Task.Run(() =>
{
    try
    {
        using Received received = routerSocket.Recv();
        RoutingId routingId = received.RoutingId
            ?? throw new InvalidOperationException("missing routing id");
        using Message part = received.Parts[0];
        SampleSupport.EnsureEqual("ping", part.GetString(), "request");
        using var reply = Message.FromString("pong");
        routerSocket.Reply(routingId, received.RequestSeq ?? 0UL, reply);
    }
    catch (Exception ex)
    {
        failure ??= ex;
    }
    finally
    {
        requestHandled.Set();
    }
});

using var sent = Message.FromString("ping");
dealerSocket.Request(
    sent,
    (result, reply) =>
    {
        try
        {
            if (result != RequestResult.Ok)
                throw new InvalidOperationException("request failed: " + result);
            using Message replyPart = reply[0];
            SampleSupport.EnsureEqual("pong", replyPart.GetString(), "reply");
        }
        catch (Exception ex)
        {
            failure ??= ex;
        }
        finally
        {
            for (int i = 1; i < reply.Count; i++)
                reply[i].Dispose();
            replyHandled.Set();
        }
    },
    SendFlags.None,
    TimeSpan.FromSeconds(2));

SampleSupport.WaitOrThrow(() => requestHandled.IsSet, 2000, "request/reply callback request");
SampleSupport.WaitOrThrow(() => replyHandled.IsSet, 2000, "request/reply callback reply");
await serverTask;
if (failure != null)
    throw failure;
Console.WriteLine("[dealer-router/request-reply/callback] send: \"ping\" -> recv: \"pong\"");
