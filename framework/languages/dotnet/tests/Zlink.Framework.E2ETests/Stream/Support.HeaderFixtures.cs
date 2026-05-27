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
    public sealed class HeaderStreamRecorder
    {
        public ConcurrentBag<string> ReceivedPayloads { get; } = [];

        public string? LastSessionId { get; set; }

        public string? ConstructorContextSessionId { get; set; }

        public global::Systems.Zlink.RoutingId? LastRoutingId { get; set; }

        public string? LastLocalAddr { get; set; }

        public string? LastRemoteAddr { get; set; }

        public int ConnectedCount { get; set; }

        public ConcurrentBag<string> ConnectedSessionIds { get; } = [];

        public int DisconnectedCount { get; set; }

        public int ErrorCount { get; set; }

        public ZLinkStreamError? LastError { get; set; }

        private readonly ConcurrentDictionary<string, CallbackConcurrency> _callbackConcurrencyBySession = new();

        public IDisposable EnterCallback(string sessionId)
        {
            var concurrency = _callbackConcurrencyBySession.GetOrAdd(sessionId, _ => new CallbackConcurrency());
            concurrency.Enter();
            return new CallbackLease(concurrency);
        }

        public int MaxConcurrentCallbacksFor(string sessionId)
        {
            return _callbackConcurrencyBySession.TryGetValue(sessionId, out var concurrency)
                ? concurrency.MaxActive
                : 0;
        }

        private sealed class CallbackConcurrency
        {
            private int _active;
            private int _maxActive;

            public int MaxActive => Volatile.Read(ref _maxActive);

            public void Enter()
            {
                var active = Interlocked.Increment(ref _active);
                while (true)
                {
                    var current = Volatile.Read(ref _maxActive);
                    if (active <= current
                        || Interlocked.CompareExchange(ref _maxActive, active, current) == current)
                    {
                        break;
                    }
                }
            }

            public void Leave()
            {
                Interlocked.Decrement(ref _active);
            }
        }

        private sealed class CallbackLease(CallbackConcurrency concurrency) : IDisposable
        {
            public void Dispose()
            {
                concurrency.Leave();
            }
        }
    }

    public sealed class HeaderStreamSession : IZLinkSession
    {
        private readonly HeaderStreamRecorder _recorder;
        private readonly IZLinkSessionContext _context;

        public HeaderStreamSession(
            HeaderStreamRecorder recorder,
            IZLinkSessionContext context)
        {
            _recorder = recorder;
            _context = context;
            recorder.ConstructorContextSessionId = context.SessionId;
        }

        public IZLinkSessionContext Context => _context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            using var lease = _recorder.EnterCallback(_context.SessionId);
            _ = cancellationToken;
            _recorder.LastSessionId = _context.SessionId;
            _recorder.LastRoutingId = _context.RoutingId;
            _recorder.LastLocalAddr = _context.LocalAddr;
            _recorder.LastRemoteAddr = _context.RemoteAddr;
            _recorder.ConnectedCount++;
            _recorder.ConnectedSessionIds.Add(_context.SessionId);
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            using var lease = _recorder.EnterCallback(_context.SessionId);
            _ = cancellationToken;
            _recorder.DisconnectedCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            using var lease = _recorder.EnterCallback(_context.SessionId);
            _ = cancellationToken;
            _recorder.LastError = error;
            _recorder.ErrorCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message payload,
            CancellationToken cancellationToken)
        {
            using var lease = _recorder.EnterCallback(_context.SessionId);
            _ = cancellationToken;
            _ = header;
            _recorder.ReceivedPayloads.Add(Encoding.UTF8.GetString(payload.AsReadOnlySpan()).Trim('"'));
            if (_recorder.ReceivedPayloads.Contains("close"))
            {
                return _context.CloseAsync(cancellationToken);
            }

            return _context.Client.Reply("pong")
                .Submit(cancellationToken);
        }
    }

    public sealed record GatewayPing(string Value);

    public sealed record GatewayPong(string Value, ulong RequestSeq);

}
