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
    public sealed class GatewayRelaySession(
        GatewaySessionRecorder recorder,
        IZLinkSessionContext context,
        IServiceProvider services) : IZLinkSession
    {
        private IZLinkSessionActor? _actor;

        public IZLinkSessionContext Context { get; } = context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.RecordDisconnected();
            var actor = _actor;
            _actor = null;
            _ = actor;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            _ = error;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public async ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            Message payload,
            CancellationToken cancellationToken)
        {
            if (_actor is null)
            {
                _actor = recorder.Actor is { } actorRef
                    ? await Context.Actors.BindAsync(
                            actorRef,
                            cancellationToken)
                        .ConfigureAwait(false)
                    : await BindLocalActorAsync(cancellationToken).ConfigureAwait(false);
            }

            await (_actor ?? throw new InvalidOperationException("Actor was not created.")).RelayAsync(
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
            recorder.RecordPostRelayPayloadLength(payload.AsReadOnlySpan().Length);
        }

        private async ValueTask<IZLinkSessionActor> BindLocalActorAsync(CancellationToken cancellationToken)
        {
            var actors = services.GetRequiredService<IZLinkActorManager>();
            var actor = await actors.FindAsync(recorder.ActorId, cancellationToken)
                .ConfigureAwait(false);
            return await Context.Actors.BindAsync(
                    actor ?? new SessionBindActor(recorder.ActorId),
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class MissingRemoteActorRelaySession(
        GatewaySessionRecorder recorder,
        IZLinkSessionContext context) : IZLinkSession
    {
        private IZLinkSessionActor? _actor;

        public IZLinkSessionContext Context { get; } = context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _actor = null;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            _ = error;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public async ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            Message payload,
            CancellationToken cancellationToken)
        {
            _actor ??= await Context.Actors.BindAsync(
                new SessionBindActor(recorder.ActorId),
                cancellationToken);

            await _actor.RelayAsync(
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private sealed class SessionBindActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context
            => throw new InvalidOperationException("Session bind test actor does not expose a runtime context.");
    }

    public sealed class LocalNotifyDisconnectSession(
        GatewaySessionRecorder recorder,
        IZLinkActorManager actors,
        IZLinkSessionContext context) : IZLinkSession
    {
        private IZLinkSessionActor? _actor;

        public IZLinkSessionContext Context { get; } = context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.RecordDisconnected();
            var actor = _actor;
            _actor = null;
            _ = actor;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            _ = error;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public async ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            Message payload,
            CancellationToken cancellationToken)
        {
            _ = header;
            if (_actor is null)
            {
                var actor = await actors.GetOrCreateAsync(
                        "local-player-1",
                        "player",
                        cancellationToken)
                    .ConfigureAwait(false);
                var actorRef = await actor.Context.JoinEntrySpot(RoutingId.From("local-notify-actor-node"))
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async(cancellationToken)
                    .ConfigureAwait(false);
                recorder.SetActor(actorRef);

                _actor = await Context.Actors.BindAsync(
                        actorRef,
                        cancellationToken)
                    .ConfigureAwait(false);
            }

            if (!string.Equals(header.Name, "open", StringComparison.Ordinal))
            {
                await _actor.RelayAsync(
                        header,
                        payload,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
        }
    }

}
