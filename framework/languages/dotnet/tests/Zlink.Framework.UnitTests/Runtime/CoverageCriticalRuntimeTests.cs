using System.Text;
using Zlink.Framework.AspNetCore.Monitoring;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class CoverageCriticalRuntimeTests
{
    [Fact]
    public void RegistryRoutePayloadCodec_RoundTripsIdentityStringsRoutingIdsAndUInt64()
    {
        byte[] identity = ZLinkRegistryRoutePayloadCodec.EncodeIdentity(
            version: 3,
            namespaceName: "game",
            identity: "room-7",
            tooLargeMessage: "too large");

        var decoded = ZLinkRegistryRoutePayloadCodec.DecodeIdentity(
            identity,
            expectedVersion: 3,
            invalidPayloadMessage: "invalid");

        Assert.True(decoded.Matches("game", "room-7"));
        Assert.Equal(identity.Length, decoded.Offset);
        Assert.Equal("game\0room-7", Encoding.UTF8.GetString(
            ZLinkRegistryRoutePayloadCodec.EncodeNamespacedKey("game", "room-7")));

        var rid = RoutingId.From("spot-rid");
        var buffer = new byte[
            ZLinkRegistryRoutePayloadCodec.EncodedStringLength("payload", "too large")
            + ZLinkRegistryRoutePayloadCodec.EncodedRoutingIdLength(rid, "rid too large")
            + sizeof(ulong)];
        var offset = 0;
        ZLinkRegistryRoutePayloadCodec.WriteString(buffer, ref offset, "payload", "too large");
        ZLinkRegistryRoutePayloadCodec.WriteRoutingId(buffer, ref offset, rid, "rid too large");
        ZLinkRegistryRoutePayloadCodec.WriteUInt64(buffer, ref offset, 0x0102030405060708UL);

        offset = 0;
        Assert.Equal("payload", ZLinkRegistryRoutePayloadCodec.ReadString(buffer, ref offset, "invalid"));
        Assert.Equal(rid, ZLinkRegistryRoutePayloadCodec.ReadRoutingId(buffer, ref offset, "invalid"));
        Assert.Equal(0x0102030405060708UL, ZLinkRegistryRoutePayloadCodec.ReadUInt64(buffer, ref offset, "invalid"));
        ZLinkRegistryRoutePayloadCodec.EnsureFullyRead(buffer, offset, "invalid");
    }

    [Fact]
    public void RegistryRoutePayloadCodec_RejectsMalformedPayloadsAndOversizedValues()
    {
        Assert.Throws<FormatException>(() => ZLinkRegistryRoutePayloadCodec.DecodeIdentity(
            [9],
            expectedVersion: 3,
            invalidPayloadMessage: "invalid"));

        Assert.Throws<FormatException>(() =>
        {
            var offset = 0;
            _ = ZLinkRegistryRoutePayloadCodec.ReadString([0, 4, 1], ref offset, "invalid");
        });

        Assert.Throws<FormatException>(() => ZLinkRegistryRoutePayloadCodec.EnsureFullyRead(
            [1, 2, 3],
            offset: 2,
            invalidPayloadMessage: "invalid"));

        var large = new string('x', ushort.MaxValue + 1);
        Assert.Throws<ZLinkConfigurationException>(() => ZLinkRegistryRoutePayloadCodec.EncodeIdentity(
            version: 1,
            namespaceName: large,
            identity: "id",
            tooLargeMessage: "too large"));

    }

    [Fact]
    public void RoutedSpotRelayPackets_CreateAndDecodeRelayParts()
    {
        using Message payloadA = Message.From("move");
        using Message payloadB = Message.From("north");
        var targetSpotRid = RoutingId.From("target-spot");

        var parts = ZLinkRoutedSpotRelayPackets.CreateRelayParts(
            ZLinkMessageKind.Request,
            "play.route",
            targetSpotRid,
            [payloadA, payloadB],
            TimeSpan.FromSeconds(5));

        try
        {
            var header = ZLinkEnvelopeCodec.DecodeHeader(parts);
            Assert.Equal(ZLinkMessageKind.Request, header.Kind);
            Assert.Equal("play.route", header.ChannelName);
            Assert.Equal(ZLinkRoutedSpotRelayPackets.RequestPacketName, header.MessageName);

            var metadata = ZLinkEnvelopeCodec.DecodePart<ZLinkRoutedSpotRelayMetadata>(parts[1]);
            Assert.Equal(targetSpotRid, RoutingId.From(metadata.TargetSpotRid));

            var reply = ZLinkRoutedSpotRelayReply.FromMessages(parts.Skip(2).ToArray());
            var copied = reply.ToMessages();
            try
            {
                Assert.Equal(
                    new[] { "move", "north" },
                    copied.Select(static message => message.GetString()).ToArray());
            }
            finally
            {
                global::Systems.Zlink.Zlink.MultipartClose(copied);
            }
        }
        finally
        {
            global::Systems.Zlink.Zlink.MultipartClose(parts);
        }

        var commandHeader = ZLinkRoutedSpotRelayPackets.CreateRelayHeader(
            ZLinkMessageKind.Command,
            "play.route");
        Assert.Equal(ZLinkRoutedSpotRelayPackets.SendPacketName, commandHeader.MessageName);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ZLinkRoutedSpotRelayPackets.CreateRelayHeader(ZLinkMessageKind.Response, "play.route"));
    }

    [Fact]
    public void MonitoringEventMapper_MapsAndFiltersSocketEvents()
    {
        var source = new ZLinkSocketMonitoringRegistration
        {
            SourceName = "orders",
        };
        source.Events.Add(ZLinkSocketEventKind.HandshakeFailed);

        var mapped = ZLinkMonitoringEventMapper.MapSocketEvent(
            source,
            new ZLinkBackendSocketMonitorEvent(
                ZLinkSocketNativeEventType.HandshakeFailedAuth,
                RoutingId.From("peer"),
                "tcp://127.0.0.1:1",
                "tcp://127.0.0.1:2",
                42));

        Assert.NotNull(mapped);
        Assert.Equal("orders", mapped.Value.SourceName);
        Assert.Equal(ZLinkSocketEventKind.HandshakeFailed, mapped.Value.Event);
        Assert.Equal(ZLinkSocketNativeEventType.HandshakeFailedAuth, mapped.Value.Diagnostic!.Value.NativeEvent);
        Assert.Equal(42U, mapped.Value.Diagnostic!.Value.NativeValue);

        Assert.Null(ZLinkMonitoringEventMapper.MapSocketEvent(
            source,
            new ZLinkBackendSocketMonitorEvent(
                ZLinkSocketNativeEventType.Connected,
                null,
                "",
                "",
                0)));

        source.Events.Clear();
        var internalEvent = ZLinkMonitoringEventMapper.MapSocketEvent(
            source,
            new ZLinkBackendSocketMonitorEvent(
                (ZLinkSocketNativeEventType)0xDEAD,
                null,
                "",
                "",
                0));
        Assert.Equal(ZLinkSocketEventKind.Internal, internalEvent?.Event);
    }

    [Fact]
    public void SpotTimerFailureEventFactory_MapsStoppedAndContinuingFailures()
    {
        var descriptor = new ZLinkSpotTimerDescriptor
        {
            Name = "tick",
            Period = TimeSpan.FromSeconds(1),
            HandlerType = typeof(CoverageCriticalRuntimeTests),
            SpotType = typeof(CoverageCriticalRuntimeTests),
            Invoker = null!,
        };
        var tick = new ZLinkTimerTick(
            "tick",
            DeliveryIndex: 2,
            ScheduledIndex: 3,
            TimeSpan.FromSeconds(1),
            DateTimeOffset.UnixEpoch,
            DateTimeOffset.UnixEpoch.AddMilliseconds(10),
            TimeSpan.Zero,
            TimeSpan.FromMilliseconds(10),
            TimeSpan.FromMilliseconds(10),
            SkippedTicks: 1);

        var continuing = ZLinkSpotTimerFailureEventFactory.Create(
            "spot.events",
            RoutingId.From("spot"),
            isEntrySpot: true,
            descriptor,
            tick,
            new InvalidOperationException("boom"),
            stopped: false);
        var stopped = ZLinkSpotTimerFailureEventFactory.Create(
            "spot.events",
            RoutingId.From("spot"),
            isEntrySpot: false,
            descriptor,
            tick,
            new ApplicationException("stop"),
            stopped: true);

        Assert.Equal(ZLinkSpotEventKind.TimerHandlerFailed, continuing.Event);
        Assert.Equal(ZLinkSpotEventKind.TimerStoppedAfterUnhandledException, stopped.Event);
        Assert.Equal("tick", continuing.TimerDiagnostic!.Value.TimerName);
        Assert.Equal(2UL, continuing.TimerDiagnostic!.Value.DeliveryIndex);
        Assert.Contains("InvalidOperationException", continuing.TimerDiagnostic!.Value.ExceptionType);
        Assert.Equal("stop", stopped.TimerDiagnostic!.Value.ExceptionMessage);
    }
}
