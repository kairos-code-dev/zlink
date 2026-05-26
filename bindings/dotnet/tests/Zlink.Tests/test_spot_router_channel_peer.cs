using System;
using System.Text;
using System.Threading;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_spot_router_channel_peer
{
    [Fact]
    public void spot_node_router_channel_peer_manual_send_delivers_to_spot()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using Spot spot = node.CreateSpot();
        using var router = new RouterSocket(ctx);

        RoutingId nodeRid = CoreTestSupport.RoutingIdUtf8("dotnet-node");
        RoutingId spotRid = CoreTestSupport.RoutingIdUtf8("dotnet-spot");
        RoutingId routerRid = CoreTestSupport.RoutingIdUtf8("dotnet-router");
        const string bindEndpoint = "tcp://127.0.0.1:*";

        node.SetRoutingId(nodeRid);
        spot.SetRoutingId(spotRid);
        router.SetChannelName("api");
        router.SetRoutingId(routerRid);
        router.Bind(bindEndpoint);
        string endpoint = router.Options.LastEndpoint;
        node.ConnectRouterChannelPeer("api", endpoint);
        node.ConnectRouterChannelPeer("api", endpoint);

        Assert.True(CoreTestSupport.WaitUntil(
            () => HasRouterChannelPeer(node, endpoint),
            timeoutMs: 5000),
            string.Join(Environment.NewLine,
                Array.ConvertAll(node.PeersSnapshot(),
                    entry => entry.ToString())));
        using var received = new Message();
        RoutingId? sourceRid = null;
        RoutingId? sourceSpotRid = null;
        ulong? requestSeq = null;
        bool hasMore = false;
        Assert.True(CoreTestSupport.WaitUntil(
            () =>
            {
                using Message sent = Message.From("hello-spot-route");
                _ = router.SendToSpot(nodeRid, spotRid).Message(sent).Submit();
                return spot.RecvRoutedPart(received, out sourceRid,
                    out sourceSpotRid, out requestSeq, out hasMore,
                    RecvFlags.DontWait);
            },
            timeoutMs: 5000));

        Assert.Equal("hello-spot-route",
            Encoding.UTF8.GetString(received.AsReadOnlySpan()).Trim('\0'));
        Assert.Equal(routerRid, sourceRid);
        Assert.Null(sourceSpotRid);
        Assert.Null(requestSeq);
        Assert.False(hasMore);
    }

    [Fact]
    public async Task spot_node_router_channel_peer_manual_request_replies_to_router()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using Spot spot = node.CreateSpot();
        using var router = new RouterSocket(ctx);

        RoutingId nodeRid = CoreTestSupport.RoutingIdUtf8("dotnet-req-node");
        RoutingId spotRid = CoreTestSupport.RoutingIdUtf8("dotnet-req-spot");
        RoutingId routerRid = CoreTestSupport.RoutingIdUtf8("dotnet-req-router");

        node.SetRoutingId(nodeRid);
        spot.SetRoutingId(spotRid);
        router.SetChannelName("api");
        router.SetRoutingId(routerRid);
        router.Bind("tcp://127.0.0.1:*");
        string endpoint = router.Options.LastEndpoint;
        node.ConnectRouterChannelPeer("api", endpoint);

        spot.OnRoutedReceive(received =>
        {
            using (received)
            using (Message reply = Message.From("reply-from-spot"))
            {
                received.Reply().Message(reply).Submit();
            }
        });

        Assert.True(CoreTestSupport.WaitUntil(
            () => HasRouterChannelPeer(node, endpoint),
            timeoutMs: 5000));
        using Message request = Message.From("request-to-spot");

        IReadOnlyList<Message> reply = await router
            .RequestToSpot(nodeRid, spotRid)
            .Message(request)
            .Timeout(TimeSpan.FromSeconds(3))
            .SubmitAsync();
        try
        {
            Message part = Assert.Single(reply);
            Assert.Equal("reply-from-spot",
                Encoding.UTF8.GetString(part.AsReadOnlySpan()).Trim('\0'));
        }
        finally
        {
            foreach (Message part in reply)
            {
                part.Dispose();
            }
        }
    }

    [Fact]
    public async Task spot_node_router_channel_peer_dispatch_event_request_replies_to_router()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using Spot spot = node.CreateSpot();
        using var router = new RouterSocket(ctx);

        RoutingId nodeRid = CoreTestSupport.RoutingIdUtf8("dotnet-dispatch-node");
        RoutingId spotRid = CoreTestSupport.RoutingIdUtf8("dotnet-dispatch-spot");
        RoutingId routerRid = CoreTestSupport.RoutingIdUtf8("dotnet-dispatch-router");

        node.SetRoutingId(nodeRid);
        spot.SetRoutingId(spotRid);
        router.SetChannelName("api");
        router.SetRoutingId(routerRid);
        router.Bind("tcp://127.0.0.1:*");
        string endpoint = router.Options.LastEndpoint;
        node.ConnectRouterChannelPeer("api", endpoint);

        spot.OnDispatchEvent(info =>
        {
            if (info.Event != SpotDispatchEvent.RoutedReadable)
                return;

            while (true)
            {
                using var received = new Received();
                if (!spot.RecvRouted(received, RecvFlags.DontWait))
                    return;
                using Message reply = Message.From("dispatch-reply");
                received.Reply().Message(reply).Submit();
            }
        });

        Assert.True(CoreTestSupport.WaitUntil(
            () => HasRouterChannelPeer(node, endpoint),
            timeoutMs: 5000));
        using Message requestHeader = Message.From("dispatch-header");
        using Message requestBody = Message.From("dispatch-request");

        IReadOnlyList<Message> reply = await router
            .RequestToSpot(nodeRid, spotRid)
            .Message(requestHeader)
            .Message(requestBody)
            .Timeout(TimeSpan.FromSeconds(3))
            .SubmitAsync();
        try
        {
            Message part = Assert.Single(reply);
            Assert.Equal("dispatch-reply",
                Encoding.UTF8.GetString(part.AsReadOnlySpan()).Trim('\0'));
        }
        finally
        {
            foreach (Message part in reply)
            {
                part.Dispose();
            }
        }
    }

    private static bool HasRouterChannelPeer(SpotNode node, string endpoint)
    {
        foreach (SpotNodePeerEntry entry in node.PeersSnapshot())
        {
            if (entry.Kind == SpotPeerKind.RouterChannel
                && entry.Source == SpotPeerSource.Manual
                && entry.PeerEndpoint == endpoint
                && (entry.State == SpotPeerState.Connected
                    || entry.State == SpotPeerState.Connecting))
            {
                return true;
            }
        }
        Thread.Sleep(10);
        return false;
    }
}
