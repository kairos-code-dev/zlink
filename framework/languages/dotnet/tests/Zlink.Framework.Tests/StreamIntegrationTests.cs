using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Protocol;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

[CollectionDefinition(nameof(StreamIntegrationTestsCollection), DisableParallelization = true)]
public sealed class StreamIntegrationTestsCollection
{
}

[Collection(nameof(StreamIntegrationTestsCollection))]
public sealed class StreamIntegrationTests
{
    private static readonly TimeSpan PollingInterval = TimeSpan.FromMilliseconds(150);

    [Fact]
    public void StreamSessionRuntime_Only_Exposes_Enqueue_Callback_Entrypoints()
    {
        var runtimeType = typeof(ZLinkStreamSessionRuntime);

        Assert.Null(runtimeType.GetMethod("MarkConnectedAsync", BindingFlags.Instance | BindingFlags.Public));
        Assert.Null(runtimeType.GetMethod("DispatchPacketAsync", BindingFlags.Instance | BindingFlags.Public));
        Assert.Null(runtimeType.GetMethod("MarkDisconnectedAsync", BindingFlags.Instance | BindingFlags.Public));
        Assert.True(runtimeType.GetMethod("MarkConnectedAsync", BindingFlags.Instance | BindingFlags.NonPublic)?.IsPrivate);
        Assert.True(runtimeType.GetMethod("DispatchPacketAsync", BindingFlags.Instance | BindingFlags.NonPublic)?.IsPrivate);
        Assert.True(runtimeType.GetMethod("MarkDisconnectedAsync", BindingFlags.Instance | BindingFlags.NonPublic)?.IsPrivate);
        Assert.NotNull(runtimeType.GetMethod("EnqueueConnected", BindingFlags.Instance | BindingFlags.Public));
        Assert.NotNull(runtimeType.GetMethod("EnqueuePacket", BindingFlags.Instance | BindingFlags.Public));
        Assert.NotNull(runtimeType.GetMethod("EnqueueDisconnected", BindingFlags.Instance | BindingFlags.Public));
    }

    [Fact]
    public async Task HeaderStreamSession_Receives_Replies_And_Tracks_Lifecycle()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new HeaderStreamRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("header.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.AddHeaderSession<HeaderStreamSession>();
                });
            });
        });
        try
        {
            using var client = ConnectRawClient(endpoint);
            var clientLocalPort = ((IPEndPoint)client.Client.LocalEndPoint!).Port;
            var network = client.GetStream();
            try
            {
                await RetryAsync(
                    async () =>
                    {
                        SendAll(network, BuildStreamPacketFrame(
                            new ZlinkStreamHeader(
                                ZlinkStreamMessageKind.Request,
                                ZlinkStreamCodec.Json,
                                ZlinkStreamHeaderFlags.HasRequestSeq,
                                new ZlinkStreamRequestSeq(1),
                                "ping",
                                ZlinkStreamMetadata.Empty),
                            "\"ping\""u8));
                        await Task.Yield();
                        callbackCapture.ThrowIfAny();
                        return recorder.ReceivedPayloads.Contains("ping");
                    },
                    received => received,
                    TimeSpan.FromSeconds(5));
            }
            catch (Exception ex) when (ex is TimeoutException or AggregateException)
            {
                throw new TimeoutException(
                    $"STREAM raw retry timed out. Connected={recorder.ConnectedCount}, Disconnected={recorder.DisconnectedCount}, Errors={recorder.ErrorCount}, Received={string.Join(',', recorder.ReceivedPayloads)}",
                    ex);
            }

            await RetryAsync(
                () => recorder.LastSessionId is not null
                    && recorder.LastRoutingId is not null
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            var reply = ReceiveFrame(network, new ZlinkStreamRequestSeq(1));
            Assert.Equal(ZlinkStreamMessageKind.Response, reply.Header.Kind);
            Assert.Equal(new ZlinkStreamRequestSeq(1), reply.Header.RequestSeq);
            Assert.Equal("\"pong\"", Encoding.UTF8.GetString(reply.Body));

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(2),
                    "ping",
                    ZlinkStreamMetadata.Empty),
                "\"ping-2\""u8));
            await RetryAsync(
                () => recorder.ReceivedPayloads.Contains("ping-2") && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            var secondReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(2));
            Assert.Equal(ZlinkStreamMessageKind.Response, secondReply.Header.Kind);
            Assert.Equal(new ZlinkStreamRequestSeq(2), secondReply.Header.RequestSeq);
            Assert.Equal("\"pong\"", Encoding.UTF8.GetString(secondReply.Body));

            Assert.NotNull(recorder.LastSessionId);
            Assert.NotNull(recorder.LastRoutingId);
            AssertStreamMetadata(endpoint, clientLocalPort, recorder.LastLocalAddr, recorder.LastRemoteAddr);
            Assert.Equal(1, recorder.ConnectedCount);

            client.Dispose();
            await RetryAsync(
                () => recorder.DisconnectedCount > 0 && recorder.ErrorCount > 0,
                TimeSpan.FromSeconds(5));
            Assert.Equal(ZLinkStreamSessionError.TransportError, recorder.LastError?.Error);
            Assert.NotNull(recorder.LastSessionId);
            Assert.Equal(1, recorder.MaxConcurrentCallbacksFor(recorder.LastSessionId));
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

    [Fact]
    public async Task HeaderStreamSession_Can_Close_Current_Client_Stream()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new HeaderStreamRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("header.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.AddHeaderSession<HeaderStreamSession>();
                });
            });
        });
        try
        {
            using var client = ConnectRawClient(endpoint);
            var network = client.GetStream();

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(1),
                    "close",
                    ZlinkStreamMetadata.Empty),
                "\"close\""u8));

            await RetryAsync(
                () => recorder.ReceivedPayloads.Contains("close")
                    && recorder.DisconnectedCount > 0
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            Assert.Equal(0, recorder.ErrorCount);
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

    private static async Task<IHost> CreateHostAsync(
        string endpoint,
        Action<IServiceCollection> configure)
    {
        _ = endpoint;
        var builder = Host.CreateApplicationBuilder();
        configure(builder.Services);
        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private static async Task RetryAsync(Func<bool> predicate, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
            {
                return;
            }

            await Task.Delay(PollingInterval);
        }

        throw new TimeoutException("STREAM integration retry timed out.");
    }

    private static async Task<T> RetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        Exception? lastError = null;

        while (DateTime.UtcNow < deadline)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(PollingInterval);
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("STREAM integration retry timed out.");
    }

    private static string GetFreeTcpEndpoint()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        return $"tcp://127.0.0.1:{endpoint.Port}";
    }

    private static TcpClient ConnectRawClient(string endpoint)
    {
        var uri = new Uri(endpoint);
        var client = new TcpClient();
        client.NoDelay = true;
        client.ReceiveTimeout = 5000;
        client.SendTimeout = 5000;
        client.Connect(IPAddress.Parse(uri.Host), uri.Port);
        return client;
    }

    private static void SendAll(NetworkStream stream, ReadOnlySpan<byte> payload)
    {
        stream.Write(payload);
        stream.Flush();
    }

    private static byte[] ReceiveExact(NetworkStream stream, int size)
    {
        var buffer = new byte[size];
        var read = 0;
        while (read < size)
        {
            var current = stream.Read(buffer, read, size - read);
            if (current <= 0)
            {
                throw new TimeoutException("STREAM receive timeout");
            }

            read += current;
        }

        return buffer;
    }

    private static byte[] BuildStreamPacketFrame(
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> body)
    {
        var headerBytes = new ZlinkStreamHeaderCodec().Encode(header).ToArray();
        var frame = new byte[6 + headerBytes.Length + body.Length];
        frame[0] = (byte)(headerBytes.Length >> 8);
        frame[1] = (byte)headerBytes.Length;
        frame[2] = (byte)(body.Length >> 24);
        frame[3] = (byte)(body.Length >> 16);
        frame[4] = (byte)(body.Length >> 8);
        frame[5] = (byte)body.Length;

        headerBytes.CopyTo(frame.AsSpan(6, headerBytes.Length));
        body.CopyTo(frame.AsSpan(6 + headerBytes.Length, body.Length));
        return frame;
    }

    private static (ZlinkStreamHeader Header, byte[] Body) ReceiveFrame(NetworkStream stream)
    {
        var lengths = ReceiveExact(stream, 6);
        var headerLength = (lengths[0] << 8) | lengths[1];
        var bodyLength = (lengths[2] << 24) | (lengths[3] << 16) | (lengths[4] << 8) | lengths[5];
        var headerBytes = ReceiveExact(stream, headerLength);
        var bodyBytes = ReceiveExact(stream, bodyLength);
        var header = new ZlinkStreamHeaderCodec().Decode(headerBytes);
        return (header, bodyBytes);
    }

    private static (ZlinkStreamHeader Header, byte[] Body) ReceiveFrame(
        NetworkStream stream,
        ZlinkStreamRequestSeq requestSeq)
    {
        while (true)
        {
            var frame = ReceiveFrame(stream);
            if (frame.Header.RequestSeq == requestSeq)
            {
                return frame;
            }
        }
    }

    private static void AssertStreamMetadata(
        string endpoint,
        int clientLocalPort,
        string? localAddr,
        string? remoteAddr)
    {
        Assert.False(string.IsNullOrWhiteSpace(localAddr));
        Assert.False(string.IsNullOrWhiteSpace(remoteAddr));

        var serverPort = new Uri(endpoint).Port;
        Assert.StartsWith("tcp://", localAddr, StringComparison.Ordinal);
        Assert.StartsWith("tcp://", remoteAddr, StringComparison.Ordinal);
        Assert.Contains($":{serverPort}", localAddr!, StringComparison.Ordinal);
        Assert.Contains($":{clientLocalPort}", remoteAddr!, StringComparison.Ordinal);
    }

    public sealed class HeaderStreamRecorder
    {
        public ConcurrentBag<string> ReceivedPayloads { get; } = [];

        public string? LastSessionId { get; set; }

        public global::Zlink.RoutingId? LastRoutingId { get; set; }

        public string? LastLocalAddr { get; set; }

        public string? LastRemoteAddr { get; set; }

        public int ConnectedCount { get; set; }

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

    public sealed class HeaderStreamSession(HeaderStreamRecorder recorder) : IZLinkSession
    {
        public IZLinkSessionContext Context { get; set; } = default!;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            using var lease = recorder.EnterCallback(Context.SessionId);
            _ = cancellationToken;
            recorder.LastSessionId = Context.SessionId;
            recorder.LastRoutingId = Context.RoutingId;
            recorder.LastLocalAddr = Context.LocalAddr;
            recorder.LastRemoteAddr = Context.RemoteAddr;
            recorder.ConnectedCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            using var lease = recorder.EnterCallback(Context.SessionId);
            _ = cancellationToken;
            recorder.DisconnectedCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            using var lease = recorder.EnterCallback(Context.SessionId);
            _ = cancellationToken;
            recorder.LastError = error;
            recorder.ErrorCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            global::Zlink.Message payload,
            CancellationToken cancellationToken)
        {
            using var lease = recorder.EnterCallback(Context.SessionId);
            _ = cancellationToken;
            _ = header;
            recorder.ReceivedPayloads.Add(Encoding.UTF8.GetString(payload.AsReadOnlySpan()).Trim('"'));
            if (recorder.ReceivedPayloads.Contains("close"))
            {
                return Context.CloseAsync(cancellationToken);
            }

            return Context.Reply("pong")
                .Async(cancellationToken);
        }
    }

    private sealed class CallbackExceptionCapture : IDisposable
    {
        private readonly ConcurrentQueue<Exception> _exceptions = new();
        private readonly EventInfo _eventInfo;
        private readonly Action<Exception> _handlerDelegate;

        private CallbackExceptionCapture()
        {
            _eventInfo = typeof(global::Zlink.Context).Assembly
                .GetType("Zlink.Runtime", throwOnError: true)!
                .GetEvent("UnhandledCallbackException", BindingFlags.Public | BindingFlags.Static)!
                ?? throw new InvalidOperationException("Could not locate Zlink.Runtime.UnhandledCallbackException.");
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
