using System.Text;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Codecs;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class RouteCodecTests
{
    [Fact]
    public async Task RouteHandlerInvoker_Uses_Configured_Codec()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.AddSerializer("application/route-test", new RouteProbeSerializer());
        var services = new ServiceCollection()
            .AddSingleton<RouteProbeHandler>()
            .BuildServiceProvider();
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "play",
            "Probe",
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            null,
            null,
            null);
        var parts = ZLinkEnvelopeCodec.EncodeParts(header, new RouteProbe("hello"), typeof(RouteProbe), codecs);
        var descriptor = new ZLinkRouteHandlerDescriptor(
            ZLinkMessageKind.Request,
            "play",
            "Probe",
            typeof(RouteProbeHandler),
            typeof(RouteProbe),
            typeof(RouteProbeReply),
            ZLinkHandlerMethodInvokerFactory.Create(
                typeof(RouteProbeHandler).GetMethod(nameof(RouteProbeHandler.HandleAsync))!));

        var invoker = new ZLinkRouteHandlerInvoker(services, codecs);

        var reply = await invoker.InvokeRequestAsync(
            descriptor,
            "play",
            RoutingId.From("source-node"),
            ZLinkEnvelopeCodec.DecodeHeader(parts),
            parts,
            CancellationToken.None);

        Assert.Equal("hello", RouteProbeHandler.LastRequest?.Text);
        Assert.Equal("application/route-test", ZLinkEnvelopeCodec.DecodeHeader(parts).ContentType);
        Assert.Equal(new RouteProbeReply("HELLO"), reply.Message);
    }

    [Fact]
    public void RouteReplyWriter_Uses_Configured_Codec()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.AddSerializer("application/route-test", new RouteProbeSerializer());
        var router = new RecordingRouter();
        var requestHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "play",
            "Probe",
            ZLinkEnvelopeCodec.DefaultContentType,
            "corr-1",
            null,
            null,
            null,
            null);

        ZLinkChannelReplyWriter.ReplyEnvelope(
            router,
            RoutingId.From("source-node"),
            7,
            ZLinkChannelReplyWriter.CreateReplyHeader(
                ZLinkMessageKind.Response,
                "play",
                requestHeader),
            new RouteProbe("reply"),
            typeof(RouteProbe),
            codecs);

        Assert.Equal("application/route-test", router.ReplyContentType);
        Assert.Equal("ROUTE:reply", router.ReplyBody);
    }

    private sealed record RouteProbe(string Text);

    private sealed record RouteProbeReply(string Text);

    private sealed class RouteProbeHandler : IZLinkRouteRequestHandler<RouteProbe, RouteProbeReply>
    {
        public static RouteProbe? LastRequest { get; private set; }

        public ValueTask<RouteProbeReply> HandleAsync(
            RouteProbe request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            LastRequest = request;
            return ValueTask.FromResult(new RouteProbeReply(request.Text.ToUpperInvariant()));
        }
    }

    private sealed class RouteProbeSerializer : IZLinkMessageSerializer
    {
        public ZLinkEncodedPayload Serialize(object value, Type type)
        {
            var text = value switch
            {
                RouteProbe probe => probe.Text,
                RouteProbeReply reply => reply.Text,
                _ => throw new NotSupportedException(type.FullName)
            };
            return ZLinkEncodedPayload.From(Encoding.UTF8.GetBytes("ROUTE:" + text));
        }

        public object? Deserialize(ZLinkEncodedPayload payload, Type type)
        {
            var text = Encoding.UTF8.GetString(payload.Bytes.Span);
            var value = text.StartsWith("ROUTE:", StringComparison.Ordinal)
                ? text["ROUTE:".Length..]
                : text;
            if (type == typeof(RouteProbe)) return new RouteProbe(value);
            if (type == typeof(RouteProbeReply)) return new RouteProbeReply(value);
            throw new NotSupportedException(type.FullName);
        }
    }

    private sealed class RecordingRouter : IZLinkBackendRouterSocket
    {
        public string? ReplyContentType { get; private set; }

        public string? ReplyBody { get; private set; }

        public object NativeInstance => this;

        public ValueTask DisposeAsync()
        {
            return ValueTask.CompletedTask;
        }

        public void Bind(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetChannelName(string channelName)
        {
            throw new NotSupportedException();
        }

        public void Connect(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void Disconnect(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetPeerWeight(int weight)
        {
            throw new NotSupportedException();
        }

        public int GetPeerWeight()
        {
            throw new NotSupportedException();
        }

        public void AttachDiscovery(IZLinkBackendDiscovery discovery)
        {
            throw new NotSupportedException();
        }

        public void OnSendReady(Action handler)
        {
            throw new NotSupportedException();
        }

        public void SetSendHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetReceiveHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetRoutingId(RoutingId routingId)
        {
            throw new NotSupportedException();
        }

        public void SetMandatory(bool mandatory)
        {
            throw new NotSupportedException();
        }

        public Received? Recv(RecvFlags flags = RecvFlags.None)
        {
            throw new NotSupportedException();
        }

        public bool Send(RoutingId routingId, Message message, SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool Send(RoutingId routingId, IReadOnlyList<Message> parts, SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool Request(
            RoutingId routingId,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool Request(
            RoutingId routingId,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool SendToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool RequestToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, Message message)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, IReadOnlyList<Message> parts)
        {
            _ = routingId;
            _ = requestSeq;
            ReplyContentType = ZLinkEnvelopeCodec.DecodeHeader(parts).ContentType;
            ReplyBody = parts[1].GetString();
        }
    }
}