using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Codecs;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class SpotRouteDispatcherTests
{
    [Fact]
    public async Task MalformedSendPayload_IsDropped_WithoutBlockingTheNextMessage()
    {
        var probe = new DispatchProbe();
        using var services = new ServiceCollection()
            .AddSingleton(probe)
            .BuildServiceProvider();
        await using var handlerInstances = new ZLinkScopedHandlerInstanceOwner(services);
        var spot = new TestSpot();
        var packets = new ZLinkSpotPacketRegistry();
        packets.Add(typeof(TestHandler));
        packets.Bind(spot);
        var codecs = new ZLinkCodecRegistryBuilder();
        var invoker = new ZLinkSpotHandlerInvoker(handlerInstances, spot, codecs, null);
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(ZLinkMessageFlowLogMode.Off);
        var dispatcher = new ZLinkSpotRouteDispatcher(
            "route",
            "target-spot",
            packets,
            () => invoker,
            codecs,
            new ZLinkDispatchErrorReporter(options));

        var malformed = Encode(new TestMessage("ignored"));
        malformed[1].Dispose();
        malformed = [malformed[0], Message.From("{")];
        var valid = Encode(new TestMessage("next"));

        try
        {
            await dispatcher.DispatchAsync(CreateRoutedReceived(malformed), CancellationToken.None);
            await dispatcher.DispatchAsync(CreateRoutedReceived(valid), CancellationToken.None);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(malformed);
            ZLinkMessageParts.DisposeAll(valid);
        }

        Assert.Equal(["next"], probe.Values);
    }

    private static IReadOnlyList<Message> Encode(TestMessage message)
    {
        return ZLinkEnvelopeCodec.EncodeParts(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Command,
                "route",
                nameof(TestMessage),
                ZLinkEnvelopeCodec.DefaultContentType,
                null,
                null,
                null,
                null,
                null),
            message,
            typeof(TestMessage),
            null);
    }

    private static Received CreateRoutedReceived(IReadOnlyList<Message> parts)
    {
        using var context = global::Systems.Zlink.Zlink.CreateContext();
        using var node = context.CreateSpotNode();
        using var sender = node.CreateSpot();
        using var receiver = node.CreateSpot();
        var nodeRid = RoutingId.From("route-node");
        var senderRid = RoutingId.From("route-source");
        var receiverRid = RoutingId.From("route-target");
        node.SetRoutingId(nodeRid);
        sender.SetRoutingId(senderRid);
        receiver.SetRoutingId(receiverRid);
        sender.SendToSpot(nodeRid, receiverRid).Messages(parts).Submit();

        var received = Received.Create();
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (DateTime.UtcNow < deadline)
        {
            if (receiver.RecvRouted(received, RecvFlags.DontWait)) return received;
            Thread.Sleep(5);
        }

        received.Dispose();
        throw new TimeoutException("Timed out creating a routed test packet.");
    }

    private sealed record TestMessage(string Value);

    private sealed class DispatchProbe
    {
        public List<string> Values { get; } = [];
    }

    private sealed class TestSpot : IZLinkSpot
    {
        public IZLinkSpotContext Context => throw new NotSupportedException();
    }

    private sealed class TestHandler(DispatchProbe probe)
        : IZLinkSpotPacketHandler<TestSpot, TestMessage>
    {
        public ValueTask HandleAsync(
            TestSpot spot,
            TestMessage message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            cancellationToken.ThrowIfCancellationRequested();
            probe.Values.Add(message.Value);
            return ValueTask.CompletedTask;
        }
    }
}
