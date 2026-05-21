using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;

public abstract partial class SpotTestSupport
{
    private protected static ZLinkSpotActivation GetSingleSpotActivation(
        ZLinkFrameworkRuntime runtime,
        string spotNodeName)
    {
        var nodeRuntime = GetSpotNodeRuntime(runtime, spotNodeName);
        return Assert.Single(nodeRuntime.Spots);
    }

    private protected static ZLinkSpotNodeRuntime GetSpotNodeRuntime(
        ZLinkFrameworkRuntime runtime,
        string spotNodeName)
    {
        return runtime.GetSpotNodeRuntime(spotNodeName);
    }

    private protected static string GetSubscriptionPumpState(ZLinkSpotActivation activation)
    {
        return activation.SubscriptionPumpState;
    }

    private protected static async Task SubmitEntrySpotStringAsync(
        ZLinkFrameworkRuntime actorRuntime,
        IZLinkActor actor,
        string packetName,
        string value)
    {
        using var body = global::Systems.Zlink.Message.FromString(value);
        await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    packetName,
                    ZlinkStreamMetadata.Empty),
                body)
            .ConfigureAwait(false);
    }

    private protected static ZLinkEnvelopeHeader CreateEntrySpotEnvelopeHeader<TMessage>(
        string channelName,
        TMessage message)
    {
        return new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            channelName,
            ZLinkMessageNameResolver.ResolveFromMessage(message)
                ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            null,
            null,
            null);
    }

    private protected static ZLinkBackendActorPart CreateEntryActorHeaderPart(
        IZLinkActor actor,
        string packetName)
    {
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            packetName,
            ZlinkStreamMetadata.Empty);

        return new ZLinkBackendActorPart(
            new ZLinkBackendActorRef(RoutingId.FromString("01"), actor.ActorId, 0),
            RoutingId.FromString("02"),
            RoutingId.FromString("03"),
            Message.FromBytes(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span),
            More: true);
    }

    private protected static ZLinkBackendActorPart CreateEntryActorBodyPart(
        IZLinkActor actor,
        string value)
    {
        return new ZLinkBackendActorPart(
            new ZLinkBackendActorRef(RoutingId.FromString("01"), actor.ActorId, 0),
            RoutingId.FromString("02"),
            RoutingId.FromString("03"),
            Message.FromString(value),
            More: false);
    }
}
