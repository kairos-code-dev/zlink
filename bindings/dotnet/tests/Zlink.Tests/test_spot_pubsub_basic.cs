using System;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_spot_pubsub_basic
{
    [Fact]
    public void logical_multicast_is_received_through_mesh_pull_dispatch()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = CreateStartedNode(ctx, "dotnet-spot-local");
        using var spot = node.EntrySpot();
        using var publisher = node.CreatePublisher();
        using var ready = new MeshReadyBatch();
        using var received = new MeshReceiveBatch();

        spot.SetSubscription("events", "orders");

        using (var payload = Message.From("created"))
        {
            var result = publisher.Publish(
                "events", "orders", [payload], SendFlags.DontWait);
            Assert.Equal(SubmitResult.Ok, result.Result);
            Assert.Equal((uint)1, result.Detail.SnapshotLocalSpots);
            Assert.Equal((uint)1, result.Detail.AdmittedLocalSpots);
        }

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
        Assert.True(claim.Receive(received, RecvFlags.DontWait));
        Assert.Equal(1, received.Count);
        Assert.Equal(MeshRecordKind.SpotMulticast, received[0].Kind);
        Assert.Equal("events", received[0].ChannelName);
        Assert.Equal("orders", received[0].Topic);

        Message[] retained = received.RetainMessage(0);
        try
        {
            Assert.Single(retained);
            Assert.Equal("created", retained[0].GetString());
        }
        finally
        {
            Zlink.MultipartClose(retained);
        }
    }

    [Fact]
    public void logical_multicast_validates_channel_and_topic()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = CreateStartedNode(ctx, "dotnet-spot-validation");
        using var spot = node.EntrySpot();
        using var message = Message.From("payload");

        Assert.ThrowsAny<ArgumentException>(() =>
            spot.Publish("", "orders", [message]));
        Assert.ThrowsAny<ArgumentException>(() =>
            spot.Publish("events", "", [message]));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.Publish("events", new string('a', 256), [message]));
        Assert.ThrowsAny<ArgumentException>(() =>
            spot.SetSubscription("", "orders"));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.SetSubscription("events", new string('a', 256)));
    }

    [Fact]
    public void failed_direct_spot_submit_preserves_message_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = CreateStartedNode(ctx, "dotnet-spot-direct");
        using var sender = node.EntrySpot();
        byte[] payload = "hello-direct-spot-send"u8.ToArray();
        using var sent = Message.From(payload);

        var result = sender.SendToSpot(
            RoutingId.From("missing-node"),
            RoutingId.From("missing-spot"),
            1,
            [sent],
            SendFlags.DontWait);

        Assert.Equal(SubmitResult.NotConnected, result);
        Assert.Equal(payload.Length, sent.Size);
        Assert.Equal(payload, sent.ToArray());
    }

    private static IMeshNode CreateStartedNode(IContext context, string name)
    {
        var node = context.CreateMeshNode(
            new MeshNodeOptions { MeshName = name });
        try
        {
            node.SetRoutingId(RoutingId.From($"{name}-rid"));
            node.SetBind(CoreTestSupport.NewEndpoint("tcp", name));
            node.AddChannel("events");
            node.Start();
            return node;
        }
        catch
        {
            node.Dispose();
            throw;
        }
    }
}
