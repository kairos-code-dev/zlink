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
    public sealed class GatewayActorHandler(ActorDispatchRecorder recorder)
        : IZLinkActorRequestHandler<GatewayActor, GatewayPing, GatewayPong>
    {
        public ValueTask<GatewayPong> HandleAsync(
            GatewayActor actor,
            GatewayPing request,
            CancellationToken cancellationToken)
        {
            _ = actor;
            cancellationToken.ThrowIfCancellationRequested();
            recorder.LastPacketName = "relay.echo";
            return ValueTask.FromResult(new GatewayPong($"play:{request.Value}", 101));
        }
    }

    public sealed class GatewaySessionDisconnectHandler(ActorDispatchRecorder recorder)
        : IZLinkActorPacketHandler<GatewayActor, GatewayPing>
    {
        public async ValueTask HandleAsync(
            GatewayActor actor,
            GatewayPing message,
            CancellationToken cancellationToken)
        {
            _ = message;
            recorder.RecordProxyDisconnect();
            await actor.Context.BoundSession.DisconnectAsync(cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class GatewaySessionDisconnectRequestHandler(ActorDispatchRecorder recorder)
        : IZLinkActorRequestHandler<GatewayActor, GatewayPing, GatewayPong>
    {
        public async ValueTask<GatewayPong> HandleAsync(
            GatewayActor actor,
            GatewayPing request,
            CancellationToken cancellationToken)
        {
            recorder.RecordProxyDisconnect();
            await actor.Context.BoundSession.DisconnectAsync(cancellationToken)
                .ConfigureAwait(false);
            return new GatewayPong($"disconnect:{request.Value}", 202);
        }
    }

    public sealed class GatewaySessionRecorder
    {
        private int _disconnectedCount;
        private int _postRelayPayloadLength = -1;

        public GatewaySessionRecorder(
            string actorId = "player-1",
            ActorRef? actor = null)
        {
            ActorId = actorId;
            Actor = actor;
        }

        public string ActorId { get; }

        public ActorRef? Actor { get; }

        public int DisconnectedCount => Volatile.Read(ref _disconnectedCount);

        public int PostRelayPayloadLength => Volatile.Read(ref _postRelayPayloadLength);

        public void RecordDisconnected()
        {
            Interlocked.Increment(ref _disconnectedCount);
        }

        public void RecordPostRelayPayloadLength(int length)
        {
            Volatile.Write(ref _postRelayPayloadLength, length);
        }
    }

    protected sealed class CallbackExceptionCapture : IDisposable
    {
        private readonly ConcurrentQueue<Exception> _exceptions = new();
        private readonly EventInfo _eventInfo;
        private readonly Action<Exception> _handlerDelegate;

        private CallbackExceptionCapture()
        {
            _eventInfo = typeof(global::Systems.Zlink.Context).Assembly
                .GetType("Systems.Zlink.Runtime", throwOnError: true)!
                .GetEvent("UnhandledCallbackException", BindingFlags.Public | BindingFlags.Static)!
                ?? throw new InvalidOperationException("Could not locate Systems.Zlink.Runtime.UnhandledCallbackException.");
            _handlerDelegate = OnUnhandledCallbackException;
            _eventInfo.AddEventHandler(null, _handlerDelegate);
        }

        public bool IsEmpty => _exceptions.IsEmpty;

        public static CallbackExceptionCapture Start()
        {
            return new CallbackExceptionCapture();
        }

        public void Dispose()
        {
            _eventInfo.RemoveEventHandler(null, _handlerDelegate);
        }

        public void ThrowIfAny()
        {
            if (_exceptions.IsEmpty)
            {
                return;
            }

            throw new AggregateException(_exceptions);
        }

        private void OnUnhandledCallbackException(Exception exception)
        {
            _exceptions.Enqueue(exception);
        }
    }
}
