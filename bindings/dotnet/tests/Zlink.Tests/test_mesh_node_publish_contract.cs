namespace Systems.Zlink.Tests;

using Xunit;

public sealed class test_mesh_node_publish_contract
{
    [Fact]
    public void mesh_node_without_channel_membership_uses_node_peer_and_shutdown_surfaces()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        string suffix = Guid.NewGuid().ToString("N");
        using var context = Zlink.CreateContext();
        using var node = context.CreateMeshNode(
            new MeshNodeOptions { MeshName = $"dotnet-zero-membership-{suffix}" });
        node.SetRoutingId(RoutingId.From($"zero-node-{suffix}"));
        node.SetBind(CoreTestSupport.NewEndpoint("inproc", "dotnet-zero-membership"));

        // No AddChannel call: a caller-only MeshNode is a valid configuration.
        node.Start();
        MeshNodeStatus ready = node.Status();
        Assert.Equal(MeshNodeState.Ready, ready.State);
        Assert.Equal((uint)0, ready.ChannelCount);

        string missingEndpoint =
            CoreTestSupport.NewEndpoint("inproc", "dotnet-zero-peer");
        ulong intent = node.ConnectPeer(missingEndpoint);
        Assert.NotEqual((ulong)0, intent);
        MeshNodePeer peer = Assert.Single(node.Peers());
        Assert.Equal(intent, peer.ConnectionIntentId);
        Assert.Equal((uint)0, peer.ChannelCount);

        using var payload = Message.From("zero-direct");
        Assert.Equal(SubmitResult.NotConnected,
            node.SendToNode(RoutingId.From("missing-node"), [payload]));

        node.Shutdown(TimeSpan.FromSeconds(1));
        Assert.Equal(MeshNodeState.Stopped, node.Status().State);
    }

    [Fact]
    public void mesh_node_transport_options_round_trip_before_start()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var context = Zlink.CreateContext();
        using var node = context.CreateMeshNode(
            new MeshNodeOptions { MeshName = "dotnet-options-contract" });

        node.RouterHighWaterMark = 7;
        node.MailboxMessageBudget = 3;
        node.MailboxByteBudget = 8192;
        node.SendTimeout = TimeSpan.FromMilliseconds(125);
        node.MaxMessageSize = 4096;

        Assert.Equal(7, node.RouterHighWaterMark);
        Assert.Equal((ulong)3, node.MailboxMessageBudget);
        Assert.Equal((ulong)8192, node.MailboxByteBudget);
        Assert.Equal(TimeSpan.FromMilliseconds(125), node.SendTimeout);
        Assert.Equal(4096, node.MaxMessageSize);

        node.SetRoutingId(RoutingId.From("dotnet-options-node"));
        node.SetBind(CoreTestSupport.NewEndpoint("tcp", "dotnet-options"));
        node.AddChannel("dotnet-options-channel");
        node.Start();

        Assert.Throws<ZlinkConfigException>(
            () => node.RouterHighWaterMark = 8);
        Assert.Throws<ZlinkConfigException>(
            () => node.MailboxMessageBudget = 4);
        Assert.Throws<ZlinkConfigException>(
            () => node.MailboxByteBudget = 16384);
        Assert.Throws<ZlinkConfigException>(
            () => node.SendTimeout = TimeSpan.FromMilliseconds(250));

        node.MaxMessageSize = -1;
        Assert.Equal(-1, node.MaxMessageSize);
    }

    [Fact]
    public void mesh_publish_returns_result_with_local_target_detail()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var context = Zlink.CreateContext();
        using var node = context.CreateMeshNode(
            new MeshNodeOptions { MeshName = "dotnet-publish-result" });
        node.SetRoutingId(RoutingId.From("dotnet-publish-result-node"));
        node.SetBind(CoreTestSupport.NewEndpoint("tcp", "dotnet-publish-result"));
        node.AddChannel("events");
        node.Start();

        using var subscriber = node.CreateSpot();
        subscriber.SetSubscription("events", "orders");
        using var publisher = node.CreatePublisher();
        using var message = Message.From("payload");

        var result = publisher.Publish(
            "events", "orders", [message], SendFlags.DontWait);

        Assert.Equal(SubmitResult.Ok, result.Result);
        Assert.Equal((uint)0, result.Detail.SnapshotRemoteTargets);
        Assert.Equal((uint)1, result.Detail.SnapshotLocalSpots);
        Assert.Equal((uint)1, result.Detail.AdmittedLocalSpots);
        Assert.Equal((uint)0, result.Detail.DroppedLocalSpots);
    }

    [Fact]
    public void mesh_claim_receive_grows_batch_to_the_reported_record_size()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var context = Zlink.CreateContext();
        using var node = context.CreateMeshNode(
            new MeshNodeOptions { MeshName = "dotnet-receive-growth" });
        node.SetRoutingId(RoutingId.From("dotnet-receive-growth-node"));
        node.SetBind(CoreTestSupport.NewEndpoint("tcp", "dotnet-receive-growth"));
        node.AddChannel("events");
        node.Start();

        using var subscriber = node.CreateSpot();
        subscriber.SetSubscription("events", "large");
        using var publisher = node.CreatePublisher();
        using var message = Message.From(new byte[4096]);
        var result = publisher.Publish("events", "large", [message]);
        Assert.Equal(SubmitResult.Ok, result.Result);

        using var ready = new MeshReadyBatch();
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            ready.Reset();
            node.DrainReady(
                MeshReadyDomains.Application,
                ready,
                RecvFlags.DontWait);
            return ready.Count > 0;
        }, 5000));

        using var claim = ready.TakeClaim(0);
        using var receive = new MeshReceiveBatch(
            messageCapacity: 1,
            partCapacity: 1,
            byteCapacity: 8);
        Assert.True(claim.Receive(receive, RecvFlags.DontWait));
        Assert.Equal(1, receive.Count);
        var retained = receive.RetainMessage(0);
        try
        {
            Assert.Single(retained);
            Assert.Equal(4096, retained[0].Size);
        }
        finally
        {
            Zlink.MultipartClose(retained);
        }
    }
}
