using System.Buffers.Binary;
using System.Text;
using K4os.Compression.LZ4;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore.Monitoring;
using Zlink.Framework.Runtime.Codecs;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class CoverageCriticalRuntimeTests
{
    [Fact]
    public void StreamProtocolLz4DecompressRejectsDecodedPayloadAboveDefaultLimit()
    {
        var compressed = LZ4Pickler.Pickle(new byte[(64 * 1024) + 1]);

        Assert.Throws<InvalidOperationException>(() =>
            ZLinkStreamProtocolDefaults.Lz4Decompress(compressed));
    }

    [Fact]
    public void StreamSendBuilderUsesConfiguredCompressionCodec()
    {
        var compression = new PrefixCompressionCodec();
        var builder = new ZLinkStreamSendBuilder<CompressionProbe>(
            new CompressionProbe("hello"),
            new ZLinkCodecRegistryBuilder(),
            compression);
        ZlinkStreamHeader? capturedHeader = null;
        byte[]? capturedFrame = null;

        builder.EnableCompression();
        builder.Write(
            (codec, flags, name, metadata) =>
            {
                capturedHeader = new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    codec,
                    flags,
                    null,
                    name,
                    metadata);
                return capturedHeader;
            },
            message =>
            {
                capturedFrame = message.ToArray();
                return true;
            },
            "send failed");

        Assert.NotNull(capturedHeader);
        Assert.True(capturedHeader.Flags.HasFlag(ZlinkStreamHeaderFlags.PayloadCompressed));
        Assert.NotNull(capturedFrame);
        var headerLength = BinaryPrimitives.ReadUInt16BigEndian(capturedFrame.AsSpan(0, 2));
        Assert.Equal(PrefixCompressionCodec.Marker, capturedFrame[6 + headerLength]);
    }

    [Fact]
    public void StreamPayloadDecodeUsesConfiguredCompressionCodecAndRuntimeLimit()
    {
        var compression = new PrefixCompressionCodec();
        var payload = compression.Compress(
            ZLinkStreamPacketPayloadCodec.EncodeJson(new CompressionProbe("hello"), typeof(CompressionProbe)));
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.PayloadCompressed,
            null,
            nameof(CompressionProbe),
            ZlinkStreamMetadata.Empty);

        using var message = Message.From(payload.Span);
        var decoded = ZLinkStreamPacketPayloadCodec.DecodeMessage(
            header,
            message,
            new ZLinkCodecRegistryBuilder(),
            compression);

        Assert.Equal(new CompressionProbe("hello"), decoded.Decode<CompressionProbe>());

        using var oversized = Message.From([0x01]);
        Assert.Throws<InvalidOperationException>(() =>
            ZLinkStreamPacketPayloadCodec.DecodeMessage(
                header,
                oversized,
                new ZLinkCodecRegistryBuilder(),
                new OversizedCompressionCodec()));
    }

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

    private sealed record CompressionProbe(string Text);

    private sealed class PrefixCompressionCodec : IZlinkStreamCompressionCodec
    {
        public const byte Marker = 0x5A;

        public ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> payload)
        {
            var compressed = new byte[payload.Length + 1];
            compressed[0] = Marker;
            payload.CopyTo(compressed.AsMemory(1));
            return compressed;
        }

        public ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> payload, int maxDecompressedPayloadSize)
        {
            if (payload.Length == 0 || payload.Span[0] != Marker)
            {
                throw new InvalidOperationException("Unexpected custom compression marker.");
            }

            return payload[1..].ToArray();
        }
    }

    private sealed class OversizedCompressionCodec : IZlinkStreamCompressionCodec
    {
        public ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> payload)
            => payload;

        public ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> payload, int maxDecompressedPayloadSize)
            => new byte[maxDecompressedPayloadSize + 1];
    }
}
