using System;
using Xunit;

namespace Zlink.Tests;

public sealed class test_validation_contract
{
    [Fact]
    public void routing_id_accepts_255_byte_boundary()
    {
        string value = new string('r', 255);

        RoutingId routingId = new(value);

        Assert.Equal(value, routingId.Value);
    }

    [Fact]
    public void monitor_open_rejects_unknown_event_flags()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var socket = new PairSocket(ctx);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            socket.OpenMonitor((SocketEvent)0x10000));
    }

    [Fact]
    public void poller_rejects_unknown_event_flags()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var poller = new Poller();
        using var ctx = new Context();
        using var socket = new PairSocket(ctx);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            poller.Add(socket, (PollEvents)16));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            poller.AddFd(0, (PollEvents)16));
    }

    [Fact]
    public void message_and_publish_entrypoints_fail_fast_on_null_and_empty_inputs()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var pair = new PairSocket(ctx);
        using var pub = new PubSocket(ctx);
        using var router = new RouterSocket(ctx);
        using var routedMessage = Message.FromString("x");
        using var publishedMessage = Message.FromString("x");

        Assert.Throws<ArgumentNullException>(() => pair.Send((Message)null!));
        Assert.Throws<ArgumentException>(() => pair.Send(Array.Empty<Message>()));
        Assert.Throws<ArgumentNullException>(() =>
            router.Send((string)null!, routedMessage));
        Assert.Throws<ArgumentNullException>(() =>
            pub.Publish((string)null!, publishedMessage));
    }
}
