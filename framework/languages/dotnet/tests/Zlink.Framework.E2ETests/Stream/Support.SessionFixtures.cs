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
        IZLinkSessionContext context) : IZLinkSession
    {
        private IZLinkActorRef? _actor;

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
                _actor = recorder.RemoteAddress is { } remoteAddress
                    ? await Context.BindActorHandleAsync(
                            recorder.ActorId,
                            "player",
                            remoteAddress,
                            cancellationToken)
                        .ConfigureAwait(false)
                    : await Context.BindActorHandleAsync(
                            recorder.ActorId,
                            "player",
                            cancellationToken)
                        .ConfigureAwait(false);
            }

            await Context.RelayToActorAsync(
                    _actor ?? throw new InvalidOperationException("Actor was not created."),
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class MissingRemoteActorRelaySession(
        GatewaySessionRecorder recorder,
        IZLinkSessionContext context) : IZLinkSession
    {
        private IZLinkActorRef? _actor;

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
            _actor ??= await Context.BindActorHandleAsync(
                recorder.ActorId,
                "player",
                cancellationToken);

            await Context.RelayToActorAsync(
                    _actor,
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class LocalNotifyDisconnectSession(
        GatewaySessionRecorder recorder,
        IZLinkActorManager actors,
        IZLinkSessionContext context) : IZLinkSession
    {
        private IZLinkActorRef? _actor;

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
            using (payload)
            {
                if (_actor is null)
                {
                    await actors.GetOrCreateAsync(
                            "local-player-1",
                            "player",
                            cancellationToken)
                        .ConfigureAwait(false);

                    _actor = await Context.BindActorHandleAsync(
                            "local-player-1",
                            "player",
                            cancellationToken)
                        .ConfigureAwait(false);
                }

                if (!string.Equals(header.Name, "open", StringComparison.Ordinal))
                {
                    using var dispatchPayload = payload.Move();
                    await Context.RelayToActorAsync(
                            _actor,
                            header,
                            dispatchPayload,
                            cancellationToken)
                        .ConfigureAwait(false);
                }
            }
        }
    }

}
