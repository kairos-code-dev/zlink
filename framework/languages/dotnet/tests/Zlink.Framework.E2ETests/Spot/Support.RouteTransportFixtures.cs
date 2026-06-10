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
    public sealed record StageBootCommand(string ScopeId);

    private protected enum SpotRouteTransportKind
    {
        ClientServer,
        RouteMesh
    }

    public sealed record SpotRouteTargetCommand(string Value);

    public sealed record SpotRouteTargetRequest(string Value);

    public sealed record SpotRouteTargetReply(string Value);

    public sealed record SpotRouteSendCallerCommand(string Value);

    public sealed record SpotRouteRequestCallerCommand(string Value);

    public sealed record RoutedSpotApiRequest(byte[] SpotRid, string Value);

    public sealed record RoutedSpotApiReply(string Value);

    public sealed class SpotRouteTargetSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<SpotRouteTargetCommandHandler>();
            Context.Handlers.AddPacket<SpotRouteTargetRequestHandler>();
        }
    }

    public sealed class SpotRouteCallerEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<SpotRouteSendCallerHandler>();
            Context.Handlers.AddPacket<SpotRouteRequestCallerHandler>();
        }
    }

    public sealed class SpotRouteTargetCommandHandler(SpotRouteTransportRecorder recorder)
        : IZLinkSpotPacketHandler<SpotRouteTargetSpot, SpotRouteTargetCommand>
    {
        public ValueTask HandleAsync(
            SpotRouteTargetSpot spot,
            SpotRouteTargetCommand message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.Commands.Enqueue(message.Value);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class SpotRouteTargetRequestHandler(SpotRouteTransportRecorder recorder)
        : IZLinkSpotRequestHandler<SpotRouteTargetSpot, SpotRouteTargetRequest, SpotRouteTargetReply>
    {
        public ValueTask<SpotRouteTargetReply> HandleAsync(
            SpotRouteTargetSpot spot,
            SpotRouteTargetRequest request,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.Requests.Enqueue(request.Value);
            return ValueTask.FromResult(new SpotRouteTargetReply($"reply:{request.Value}"));
        }
    }

    public sealed class SpotRouteSendCallerHandler
        : IZLinkSpotPacketHandler<SpotRouteCallerEntrySpot, SpotRouteSendCallerCommand>
    {
        public async ValueTask HandleAsync(
            SpotRouteCallerEntrySpot spot,
            SpotRouteSendCallerCommand message,
            CancellationToken cancellationToken)
        {
            await spot.Context.Outbound.SendToSpot(RoutingId.From("route-target"), new SpotRouteTargetCommand(message.Value))
                .SubmitAsync(cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class SpotRouteRequestCallerHandler(
        SpotRouteTransportRecorder recorder)
        : IZLinkSpotPacketHandler<SpotRouteCallerEntrySpot, SpotRouteRequestCallerCommand>
    {
        public async ValueTask HandleAsync(
            SpotRouteCallerEntrySpot spot,
            SpotRouteRequestCallerCommand message,
            CancellationToken cancellationToken)
        {
            var reply = await spot.Context.Outbound
                .RequestToSpot(RoutingId.From("route-target"), new SpotRouteTargetRequest(message.Value))
                .Timeout(TimeSpan.FromMilliseconds(500))
                .SubmitAsync<SpotRouteTargetReply>(cancellationToken)
                .ConfigureAwait(false);
            recorder.Replies.Enqueue(reply.Value);
        }
    }

    public sealed class FixedSpotRemoteAddressResolver : IZLinkSpotRemoteAddressResolver
    {
        private ZLinkSpotRemoteAddress? _route;

        public void Configure(
            string routerChannelId,
            RoutingId targetNodeRid,
            RoutingId spotRid)
        {
            _route = new ZLinkSpotRemoteAddress(
                routerChannelId,
                targetNodeRid,
                spotRid,
                ZLinkSpotKind.User);
        }

        public ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
            RoutingId spotRid,
            CancellationToken cancellationToken)
        {
            _ = spotRid;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(RequireRoute());
        }

        private ZLinkSpotRemoteAddress RequireRoute()
        {
            return _route ?? throw new InvalidOperationException("SPOT route is not configured.");
        }
    }

    public sealed class SpotRouteTransportRecorder
    {
        public ConcurrentQueue<string> Commands { get; } = new();

        public ConcurrentQueue<string> Requests { get; } = new();

        public ConcurrentQueue<string> Replies { get; } = new();
    }
}
