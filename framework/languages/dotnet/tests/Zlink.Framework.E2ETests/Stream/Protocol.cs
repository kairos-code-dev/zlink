using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

[Collection(nameof(StreamTestsCollection))]
public sealed class ProtocolTests : StreamTestSupport
{
    [Fact]
    public void StreamSessionRuntime_Only_Exposes_Enqueue_Callback_Entrypoints()
    {
        var runtimeType = typeof(ZLinkStreamSessionRuntime);

        Assert.Null(runtimeType.GetMethod("MarkConnectedAsync"));
        Assert.Null(runtimeType.GetMethod("DispatchPacketAsync"));
        Assert.Null(runtimeType.GetMethod("MarkDisconnectedAsync"));
        Assert.NotNull(runtimeType.GetMethod("EnqueueConnected"));
        Assert.NotNull(runtimeType.GetMethod("EnqueuePacket"));
        Assert.NotNull(runtimeType.GetMethod("EnqueueDisconnected"));
    }

    [Fact]
    public void SpotActorRegistry_DoesNot_Resolve_Request_To_Send_Handler()
    {
        var entryRegistry = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.EntrySpot,
            typeof(RegistryOnlyEntrySpot));
        var userRegistry = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.UserSpot,
            typeof(RegistryOnlySpot));
        var requestHeader = CreateStreamHeader(ZlinkStreamMessageKind.Request, "packet");
        var sendHeader = CreateStreamHeader(ZlinkStreamMessageKind.Send, "packet");
        var responseHeader = CreateStreamHeader(ZlinkStreamMessageKind.Response, "packet");
        var errorHeader = CreateStreamHeader(ZlinkStreamMessageKind.Error, "packet");
        var controlHeader = CreateStreamHeader(ZlinkStreamMessageKind.Control, "packet");

        entryRegistry.AddPacket(typeof(RegistryOnlyEntrySpotActorSendHandler), typeof(RegistryOnlyActor), "packet");
        userRegistry.AddPacket(typeof(RegistryOnlyUserSpotActorSendHandler), typeof(RegistryOnlyActor), "packet");

        Assert.True(entryRegistry.TryResolve(typeof(RegistryOnlyActor), sendHeader, out var entrySendDescriptor));
        Assert.NotNull(entrySendDescriptor);
        Assert.False(entryRegistry.TryResolve(typeof(RegistryOnlyActor), requestHeader, out _));
        Assert.False(entryRegistry.TryResolve(typeof(RegistryOnlyActor), responseHeader, out _));
        Assert.False(entryRegistry.TryResolve(typeof(RegistryOnlyActor), errorHeader, out _));
        Assert.False(entryRegistry.TryResolve(typeof(RegistryOnlyActor), controlHeader, out _));
        Assert.True(userRegistry.TryResolve(typeof(RegistryOnlyActor), sendHeader, out var userSendDescriptor));
        Assert.NotNull(userSendDescriptor);
        Assert.False(userRegistry.TryResolve(typeof(RegistryOnlyActor), requestHeader, out _));
        Assert.False(userRegistry.TryResolve(typeof(RegistryOnlyActor), responseHeader, out _));
        Assert.False(userRegistry.TryResolve(typeof(RegistryOnlyActor), errorHeader, out _));
        Assert.False(userRegistry.TryResolve(typeof(RegistryOnlyActor), controlHeader, out _));
    }

    [Fact]
    public void SpotActorRegistry_Resolves_Attributed_Handlers()
    {
        var entryRegistry = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.EntrySpot,
            typeof(RegistryOnlyEntrySpot));
        var userRegistry = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.UserSpot,
            typeof(RegistryOnlySpot));
        var joins = new ZLinkSpotActorJoinRegistry();
        var requestHeader = CreateStreamHeader(ZlinkStreamMessageKind.Request, "entry.request");
        var sendHeader = CreateStreamHeader(ZlinkStreamMessageKind.Send, "spot.send");

        entryRegistry.AddHandler(typeof(RegistryOnlyAttributedEntrySpotActorRequestHandler), null);
        entryRegistry.AddHandler(typeof(RegistryOnlyAttributedEntrySpotJoinedHandler), null);
        userRegistry.AddHandler(typeof(RegistryOnlyAttributedUserSpotActorSendHandler), null);
        userRegistry.AddHandler(typeof(RegistryOnlyAttributedUserSpotLeftHandler), null);
        joins.Add(typeof(RegistryOnlyAttributedSpotActorJoinHandler));
        joins.Bind(new RegistryOnlySpot(null!));

        Assert.True(entryRegistry.TryResolve(typeof(RegistryOnlyActor), requestHeader, out var entryRequest));
        Assert.NotNull(entryRequest);
        Assert.Equal(typeof(GatewayPong), entryRequest.ReplyType);
        Assert.True(entryRegistry.TryResolveJoined(typeof(RegistryOnlyActor), out var entryJoined));
        Assert.NotNull(entryJoined);
        Assert.True(userRegistry.TryResolve(typeof(RegistryOnlyActor), sendHeader, out var userSend));
        Assert.NotNull(userSend);
        Assert.True(userRegistry.TryResolveLeft(typeof(RegistryOnlyActor), out var userLeft));
        Assert.NotNull(userLeft);
        Assert.True(joins.TryResolve(typeof(GatewayPing), out var joinDescriptor));
        Assert.NotNull(joinDescriptor);
        Assert.Equal(typeof(GatewayPong), joinDescriptor.ReplyType);
    }

}
