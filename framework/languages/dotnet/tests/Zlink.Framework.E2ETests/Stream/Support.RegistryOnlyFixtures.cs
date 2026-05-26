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

public abstract partial class StreamTestSupport
{
    public sealed class ActorDispatchRecorder
    {
        private int _createdCount;
        private int _disconnectedCount;
        private int _proxyDisconnectCount;

        public string? LastPacketName { get; set; }

        public string? LastTraceId { get; set; }

        public bool ForwardedTenantId { get; set; }

        public int CreatedCount => Volatile.Read(ref _createdCount);

        public int DisconnectedCount => Volatile.Read(ref _disconnectedCount);

        public int ProxyDisconnectCount => Volatile.Read(ref _proxyDisconnectCount);

        public void RecordCreated()
        {
            Interlocked.Increment(ref _createdCount);
        }

        public void RecordDisconnected()
        {
            Interlocked.Increment(ref _disconnectedCount);
        }

        public void RecordProxyDisconnect()
        {
            Interlocked.Increment(ref _proxyDisconnectCount);
        }
    }

    public sealed class RegistryOnlyActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context
            => throw new InvalidOperationException("Registry-only actor does not expose context.");

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryOnlyActorSendHandler
        : IZLinkActorPacketHandler<RegistryOnlyActor, GatewayPing>
    {
        public ValueTask HandleAsync(
            RegistryOnlyActor actor,
            GatewayPing message,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = message;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryOnlyEntrySpotActorSendHandler
        : IZLinkEntrySpotActorSendHandler<RegistryOnlyEntrySpot, RegistryOnlyActor, GatewayPing>
    {
        public ValueTask HandleAsync(
            RegistryOnlyEntrySpot entrySpot,
            RegistryOnlyActor actor,
            GatewayPing message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = actor;
            _ = message;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryOnlyEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;
    }

    public sealed class RegistryOnlySpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;
    }

    public sealed class RegistryOnlyUserSpotActorSendHandler
        : IZLinkSpotActorSendHandler<RegistryOnlySpot, RegistryOnlyActor, GatewayPing>
    {
        public ValueTask HandleAsync(
            RegistryOnlySpot spot,
            RegistryOnlyActor actor,
            GatewayPing message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = actor;
            _ = message;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryOnlyAttributedEntrySpotActorRequestHandler
    {
        [ZLinkSpotActorRequest(PacketName = "entry.request")]
        public ValueTask<GatewayPong> HandleAsync(
            RegistryOnlyEntrySpot entrySpot,
            RegistryOnlyActor actor,
            GatewayPing message,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = actor;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new GatewayPong(message.Value, 1));
        }
    }

    public sealed class RegistryOnlyAttributedEntrySpotJoinedHandler
    {
        [ZLinkSpotPostActorJoined]
        public ValueTask HandleAsync(
            RegistryOnlyEntrySpot entrySpot,
            RegistryOnlyActor actor,
            ZLinkSpotActorChangeResult info,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = actor;
            _ = info;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryOnlyAttributedUserSpotActorSendHandler
    {
        [ZLinkSpotActorSend(PacketName = "spot.send")]
        public ValueTask HandleAsync(
            RegistryOnlySpot spot,
            RegistryOnlyActor actor,
            GatewayPing message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = actor;
            _ = message;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryOnlyAttributedUserSpotLeftHandler
    {
        [ZLinkSpotActorLeft]
        public ValueTask HandleAsync(
            RegistryOnlySpot spot,
            RegistryOnlyActor actor,
            ZLinkSpotActorChangeResult info,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = actor;
            _ = info;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryOnlyAttributedSpotActorJoinHandler
    {
        [ZLinkSpotActorJoin]
        public ValueTask<GatewayPong> HandleAsync(
            RegistryOnlySpot spot,
            RegistryOnlyActor actor,
            GatewayPing request,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = actor;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new GatewayPong(request.Value, 1));
        }
    }
}
