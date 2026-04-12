using System;
using System.Threading;
using Xunit;
using Zlink.Service;

namespace Zlink.Tests;

public sealed class test_spot_pubsub_basic
{
    [Fact]
    public void spot_subscription_roundtrip()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);

        spot.SetSubscription("zone:12:*");
        spot.UnsetSubscription("zone:12:*");
    }

    [Fact]
    public void spot_single_part_publish_moves_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);
        spot.SetSubscription("own:topic");

        using var msg = Message.FromString("owned-spot");
        spot.Publish("own:topic", msg);

        Assert.Throws<ObjectDisposedException>(() => _ = msg.Size);
    }

    [Fact]
    public void spot_subscribe_handler_blocks_direct_subscribe_path()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);
        spot.SetSubscription("cb:topic");
        spot.OnSubscribe((topic, parts) =>
        {
            foreach (Message part in parts)
                part.Dispose();
        });

        Assert.Throws<ZlinkRecvException>(() =>
            spot.Subscribe(RecvFlags.DontWait));
    }

    [Fact]
    public void spot_topic_input_validation()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);

        string tooLongTopic = new string('a', 256);

        Assert.Throws<ArgumentException>(() => spot.SetSubscription(""));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.SetSubscription(tooLongTopic));
        Assert.Throws<ArgumentException>(() => spot.UnsetSubscription(""));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.UnsetSubscription(tooLongTopic));
        Assert.Throws<ArgumentException>(() =>
            spot.Publish("", Message.FromString("x")));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.Publish(tooLongTopic, Message.FromString("x")));
    }

    [Fact]
    public void spot_node_backed_spot_roundtrip_over_manual_peer()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("tcp"))
            return;

        using var ctx = new Context();
        using var pubNode = new SpotNode(ctx);
        using var subNode = new SpotNode(ctx);
        using var publisher = new Spot(pubNode);
        using var subscriber = new Spot(subNode);

        string endpoint = CoreTestSupport.NewEndpoint("tcp", "spot-node-manual");
        pubNode.Bind(endpoint);
        subNode.ConnectPeer(endpoint);
        subscriber.SetSubscription("spot:test");

        DateTime deadline = DateTime.UtcNow.AddMilliseconds(5000);
        while (DateTime.UtcNow < deadline)
        {
            using Message message = Message.FromString("node-backed");
            publisher.Publish("spot:test", message);
            string? received = TryReceiveSpotUtf8(subscriber);
            if (received == "node-backed")
                return;
            Thread.Sleep(10);
        }

        throw new TimeoutException("spot node-backed roundtrip timeout");
    }

    [Fact]
    public void spot_node_backed_subscribe_handler_blocks_direct_subscribe_path()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = new Spot(node);
        spot.SetSubscription("node:cb");
        spot.OnSubscribe((topic, parts) =>
        {
            foreach (Message part in parts)
                part.Dispose();
        });

        Assert.Throws<ZlinkRecvException>(() =>
            spot.Subscribe(RecvFlags.DontWait));
    }

    [Fact]
    public void spot_node_backed_callback_roundtrip_over_manual_peer()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("tcp"))
            return;

        using var ctx = new Context();
        using var pubNode = new SpotNode(ctx);
        using var subNode = new SpotNode(ctx);
        using var publisher = new Spot(pubNode);
        using var subscriber = new Spot(subNode);
        using var receivedSignal = new ManualResetEventSlim(false);

        string? receivedPayload = null;
        subscriber.OnSubscribe((_, parts) =>
        {
            try
            {
                if (parts.Length == 1)
                    receivedPayload = parts[0].GetString();
            }
            finally
            {
                foreach (Message part in parts)
                    part.Dispose();
                receivedSignal.Set();
            }
        });

        string endpoint = CoreTestSupport.NewEndpoint("tcp", "spot-node-callback");
        pubNode.Bind(endpoint);
        subNode.ConnectPeer(endpoint);
        subscriber.SetSubscription("spot:callback");

        DateTime deadline = DateTime.UtcNow.AddMilliseconds(5000);
        while (DateTime.UtcNow < deadline && !receivedSignal.IsSet)
        {
            using Message message = Message.FromString("node-callback");
            publisher.Publish("spot:callback", message);
            receivedSignal.Wait(10);
        }

        Assert.True(receivedSignal.IsSet, "spot node-backed callback timeout");
        Assert.Equal("node-callback", receivedPayload);
    }

    [Fact]
    public void spot_subscribe_callback_hops_to_registered_context()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("tcp"))
            return;

        using var ctx = new Context();
        using var pubNode = new SpotNode(ctx);
        using var subNode = new SpotNode(ctx);
        using var publisher = new Spot(pubNode);
        using var subscriber = new Spot(subNode);
        using var receivedSignal = new ManualResetEventSlim(false);
        using var callbackContext = new SingleThreadSynchronizationContext();

        string? receivedPayload = null;
        int callbackThreadId = -1;

        callbackContext.Invoke(() =>
        {
            subscriber.OnSubscribe((topic, parts) =>
            {
                callbackThreadId = Environment.CurrentManagedThreadId;
                try
                {
                    if (parts.Length == 1)
                        receivedPayload = parts[0].GetString();
                }
                finally
                {
                    foreach (Message part in parts)
                        part.Dispose();
                    receivedSignal.Set();
                }
            });
        });

        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "spot-callback-context");
        pubNode.Bind(endpoint);
        subNode.ConnectPeer(endpoint);
        subscriber.SetSubscription("spot:callback");

        DateTime deadline = DateTime.UtcNow.AddMilliseconds(5000);
        while (DateTime.UtcNow < deadline && !receivedSignal.IsSet)
        {
            using Message message = Message.FromString("node-callback");
            publisher.Publish("spot:callback", message);
            receivedSignal.Wait(10);
        }

        Assert.True(receivedSignal.IsSet, "spot subscribe callback timeout");
        Assert.Equal(callbackContext.ThreadId, callbackThreadId);
        Assert.Equal("node-callback", receivedPayload);
    }

    private static string? TryReceiveSpotUtf8(Spot subscriber)
    {
        Subscribed subscribed;
        try
        {
            subscribed = subscriber.Subscribe(RecvFlags.DontWait);
        }
        catch (ZlinkRecvException)
        {
            return null;
        }

        try
        {
            return subscribed!.Parts.Count == 0
                ? string.Empty
                : subscribed.SinglePartOrThrow().GetString();
        }
        finally
        {
            foreach (Message part in subscribed!.Parts)
                part.Dispose();
        }
    }

    private static string ReceiveSpotUtf8WithTimeout(Spot subscriber, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            string? received = TryReceiveSpotUtf8(subscriber);
            if (received == null)
            {
                Thread.Sleep(10);
                continue;
            }
            return received;
        }

        throw new TimeoutException("spot subscribe timeout");
    }
}
