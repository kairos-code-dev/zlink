using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Xunit;

namespace Systems.Zlink.Tests;

/// <summary>
///     A spot-to-spot request carrying more than one part.
///
///     The core takes the reply handler as the marker of the FINAL part: that submission is the
///     one that builds the request spec, so
///     <c>core/src/api/spot/request_reply/service_spot_request_reply_part_submit.cpp</c>
///     (<c>validate_request_part_handler</c>) rejects a non-final part that carries a handler
///     with <c>EINVAL</c>. A binding that attaches the handler to every part therefore cannot
///     send a multipart spot request at all — the first part is refused before the request
///     leaves the process.
///
///     It went unnoticed because a single-part request takes a separate path that always submits
///     FINAL, and nothing sent a multipart spot request until a cross-node actor transfer did:
///     its admission payload is exactly two parts, header and body.
/// </summary>
public sealed class test_spot_multipart_request
{
    private static readonly RoutingId ServerNodeRid = RoutingId.From("mp-server-node");
    private static readonly RoutingId ServerSpotRid = RoutingId.From("mp-server-spot");
    private static readonly RoutingId ClientNodeRid = RoutingId.From("mp-client-node");
    private static readonly RoutingId ClientSpotRid = RoutingId.From("mp-client-spot");

    [Fact]
    public async Task two_part_request_to_spot_round_trips()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        IReadOnlyList<string> received = await RoundTripAsync("header", "body");

        Assert.Equal(new[] { "header", "body" }, received);
    }

    [Fact]
    public async Task single_part_request_to_spot_round_trips()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        IReadOnlyList<string> received = await RoundTripAsync("only");

        Assert.Equal(new[] { "only" }, received);
    }

    /// <summary>
    ///     Sends the parts from one node's spot to another's and returns what the server spot
    ///     actually received, so a request that is silently truncated to its first part fails
    ///     here rather than passing.
    /// </summary>
    private static async Task<IReadOnlyList<string>> RoundTripAsync(params string[] parts)
    {
        using var ctx = Zlink.CreateContext();
        using var serverNode = ctx.CreateSpotNode();
        using var clientNode = ctx.CreateSpotNode();
        using var server = serverNode.CreateSpot();
        using var client = clientNode.CreateSpot();

        serverNode.SetRoutingId(ServerNodeRid);
        clientNode.SetRoutingId(ClientNodeRid);
        server.SetRoutingId(ServerSpotRid);
        client.SetRoutingId(ClientSpotRid);

        // The routed plane needs the ROUTER bind, and it has to come before the pub bind.
        serverNode.SetRouterBind(CoreTestSupport.NewEndpoint("tcp", "mp-server-router"));
        clientNode.SetRouterBind(CoreTestSupport.NewEndpoint("tcp", "mp-client-router"));
        string serverPub = CoreTestSupport.NewEndpoint("tcp", "mp-server-pub");
        string clientPub = CoreTestSupport.NewEndpoint("tcp", "mp-client-pub");
        serverNode.SetPubBind(serverPub);
        clientNode.SetPubBind(clientPub);
        serverNode.ConnectPeer(clientPub);
        clientNode.ConnectPeer(serverPub);

        var delivered = new List<string>();
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
                if (!got) break;

                foreach (Message part in received.Parts)
                    delivered.Add(part.GetString());

                using Message reply = Message.From("ok");
                received.Reply().Message(reply).Submit();
            }
        });

        Assert.True(CoreTestSupport.WaitUntil(
            () => serverNode.Peers().Length > 0 && clientNode.Peers().Length > 0, 5000));

        RequestSubmitOperation request = client
            .RequestToSpot(ServerNodeRid, ServerSpotRid)
            .Message(Message.From(parts[0]));
        for (int i = 1; i < parts.Length; i++)
            request = request.Message(Message.From(parts[i]));

        IReadOnlyList<Message> reply = await request
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();
        Assert.Equal("ok", reply[0].GetString());
        Zlink.MultipartClose(reply);

        return delivered;
    }
}
