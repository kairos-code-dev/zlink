using System.Threading;
using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var dealerSocket = new DealerSocket(ctx);
using var routerSocket = new RouterSocket(ctx);
using var dealer = new RequestDealer(dealerSocket);
using var router = new RequestRouter(routerSocket);
string endpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
using var dealerMonitor = dealerSocket.MonitorOpen(SocketEvent.ConnectionReady);
using var routerMonitor = routerSocket.MonitorOpen(SocketEvent.ConnectionReady);
dealerSocket.DealerOptions.RoutingId = new RoutingId("request-reply-client");
routerSocket.Bind(endpoint);
dealerSocket.Connect(endpoint);
SampleSupport.WaitConnected(routerMonitor, dealerMonitor);

using var requestHandled = new ManualResetEventSlim(false);
using var replyHandled = new ManualResetEventSlim(false);
Exception? failure = null;

router.OnReceive(received =>
{
    try
    {
        if (!received.HasRequestSequence)
            throw new InvalidOperationException("missing request sequence");
        using Message part = received.Parts[0];
        SampleSupport.EnsureEqual("ping", part.GetString(), "request");
        using var reply = Message.FromString("pong");
        router.Reply(received.RoutingIdValue!, received.RequestSequence, reply);
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
dealer.Request(
    sent,
    reply =>
    {
        try
        {
            using Message replyPart = reply.Parts[0];
            SampleSupport.EnsureEqual("pong", replyPart.GetString(), "reply");
        }
        catch (Exception ex)
        {
            failure ??= ex;
        }
        finally
        {
            replyHandled.Set();
        }
    },
    error =>
    {
        failure ??= error;
        replyHandled.Set();
    },
    TimeSpan.FromSeconds(2));

SampleSupport.WaitOrThrow(() => requestHandled.IsSet, 2000, "request/reply callback request");
SampleSupport.WaitOrThrow(() => replyHandled.IsSet, 2000, "request/reply callback reply");
if (failure != null)
    throw failure;
Console.WriteLine("[dealer-router/request-reply/callback] send: \"ping\" -> recv: \"pong\"");
