using System;
using System.Text;
using System.Threading.Tasks;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_validation_contract
{
    [Fact]
    public void routing_id_accepts_255_byte_boundary()
    {
        string value = new string('r', 255);

        RoutingId routingId = RoutingId.FromBytes(Encoding.UTF8.GetBytes(value));

        Assert.Equal(routingId.ToHex(), routingId.ToString());
        Assert.Equal(510, routingId.ToString().Length);
    }

    [Fact]
    public void routing_id_rejects_256_byte_boundary()
    {
        string value = new string('r', 256);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            _ = RoutingId.FromBytes(Encoding.UTF8.GetBytes(value)));
    }

    [Fact]
    public void monitor_open_rejects_unknown_event_flags()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var socket = new PairSocket(ctx);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            socket.MonitorOpen((SocketEvent)0x10000));
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
            poller.Add(socket, (PollEventFlags)16));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            poller.AddFd(0, (PollEventFlags)16));
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

        Assert.Throws<ArgumentNullException>(() =>
            pair.Send().Message((Message)null!));
        Assert.Throws<ArgumentNullException>(() =>
            pub.Publish((string)null!).Message(publishedMessage).Submit());
    }

    [Fact]
    public void service_surface_rejects_overlong_fixed_utf8_inputs()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        string overlong = new string('a', 256);

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var registry = new Registry(ctx);
        using var query = new RegistryQueryClient(ctx);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            _ = new Discovery(ctx, AutoConnectType.SpotMesh, overlong));
        Assert.Throws<ArgumentOutOfRangeException>(() => node.Bind(overlong));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            registry.Bind(overlong, "tcp://127.0.0.1:5555"));
        Assert.Throws<ArgumentOutOfRangeException>(() => query.Connect(overlong));
    }

    [Fact]
    public void boundary_strings_reject_embedded_nul()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var pair = new PairSocket(ctx);
        using var pub = new PubSocket(ctx);
        using var sub = new SubSocket(ctx);
        using var dealer = new DealerSocket(ctx);
        using var node = new SpotNode(ctx);
        using var spot = node.CreateSpot();
        using var message = Message.FromString("x");

        Assert.Throws<ArgumentException>(() =>
            pair.Bind("tcp://127.0.0.1:5555\0"));
        Assert.Throws<ArgumentException>(() =>
            pair.Connect("tcp://127.0.0.1:5555\0"));
        Assert.Throws<ArgumentException>(() =>
            pub.Publish("topic\0").Message(message).Submit());
        Assert.Throws<ArgumentException>(() =>
            sub.SetSubscription("topic\0"));
        Assert.Throws<ArgumentException>(() =>
            spot.SetSubscription("topic\0"));
        Assert.Throws<ArgumentException>(() =>
            spot.Publish("topic\0").Message(message).Submit());
        Assert.Throws<ArgumentException>(() =>
            ctx.Options.ThreadNamePrefix = "io\0");
        Assert.Throws<ArgumentException>(() =>
            dealer.SetChannelName("channel\0"));
    }

    [Fact]
    public void topic_and_subscription_boundaries_reject_overlong_utf8_inputs()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        string overlong = new string('t', 256);

        using var ctx = new Context();
        using var pub = new PubSocket(ctx);
        using var sub = new SubSocket(ctx);
        using var node = new SpotNode(ctx);
        using var spot = node.CreateSpot();
        using var message = Message.FromString("x");

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            pub.Publish(overlong).Message(message).Submit());
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            sub.SetSubscription(overlong));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.SetSubscription(overlong));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            spot.Publish(overlong).Message(message).Submit());
    }

    [Fact]
    public void fixed_utf8_boundary_values_reject_overlong_inputs()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        string overlong = new string('a', 256);

        using var ctx = new Context();
        using var dealer = new DealerSocket(ctx);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            dealer.SetChannelName(overlong));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ctx.Options.ThreadNamePrefix = new string('a', 17));
    }

    [Fact]
    public void socket_endpoint_failures_use_function_category_exceptions()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var pair = new PairSocket(ctx);

        Assert.Throws<ZlinkBindException>(() => pair.Bind("invalid://endpoint"));
        Assert.Throws<ZlinkConnectException>(() =>
            pair.Connect("invalid://endpoint"));
        Assert.Throws<ZlinkConnectException>(() =>
            pair.Unbind("invalid://endpoint"));
        Assert.Throws<ZlinkConnectException>(() =>
            pair.Disconnect("invalid://endpoint"));
    }

    [Fact]
    public void service_surface_accepts_255_byte_fixed_utf8_inputs()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        string maxLength = new string('a', 255);

        using var ctx = new Context();
        using var discovery = new Discovery(ctx, AutoConnectType.SpotMesh, maxLength);
    }

    [Fact]
    public async Task request_timeouts_reject_negative_values()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var dealer = new DealerSocket(ctx);
        using var message = Message.FromString("x");
        TimeSpan negative = TimeSpan.FromMilliseconds(-1);

        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(() =>
            dealer.Request().Message(message).Timeout(negative).SubmitAsync());
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            dealer.Request().Message(message).Timeout(negative).Submit(
                (_, parts) => Zlink.MultipartClose(parts)));
    }
}
