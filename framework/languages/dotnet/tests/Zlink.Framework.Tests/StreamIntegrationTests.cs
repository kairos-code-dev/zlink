using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
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
    public async Task RawStreamSession_Receives_Writes_And_Tracks_Lifecycle()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new RawStreamRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("raw.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.AddRawSession<RawStreamSession>();
                });
            });
        });
        try
        {
            using var client = ConnectRawClient(endpoint);
            var network = client.GetStream();
            try
            {
                await RetryAsync(
                    async () =>
                    {
                        SendAll(network, "ping"u8);
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
            Assert.Equal("pong", Encoding.UTF8.GetString(ReceiveExact(network, 4)));
            Assert.NotNull(recorder.LastSessionId);
            Assert.NotNull(recorder.LastRoutingId);
            Assert.Equal(1, recorder.ConnectedCount);

            client.Dispose();
            await RetryAsync(
                () => recorder.DisconnectedCount > 0 && recorder.ErrorCount > 0,
                TimeSpan.FromSeconds(5));
            Assert.Equal(ZLinkStreamSessionError.TransportError, recorder.LastError?.Error);
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

    [Fact]
    public async Task PacketStreamSession_Receives_Header_And_Body()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new PacketStreamRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("packet.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.AddPacketSession<PacketStreamSession>();
                });
            });
        });
        try
        {
            using var client = ConnectRawClient(endpoint);
            var network = client.GetStream();
            try
            {
                await RetryAsync(
                    async () =>
                    {
                        SendAll(network, BuildStreamPacketFrame("hdr"u8, "body"u8));
                        await Task.Yield();
                        callbackCapture.ThrowIfAny();
                        return recorder.LastHeader == "hdr" && recorder.LastBody == "body";
                    },
                    received => received,
                    TimeSpan.FromSeconds(5));
            }
            catch (Exception ex) when (ex is TimeoutException or AggregateException)
            {
                throw new TimeoutException(
                    $"STREAM packet retry timed out. Connected={recorder.ConnectedCount}, Header={recorder.LastHeader ?? "<null>"}, Body={recorder.LastBody ?? "<null>"}",
                    ex);
            }

            await RetryAsync(
                () => recorder.LastSessionId is not null
                    && recorder.LastRoutingId is not null
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            Assert.NotNull(recorder.LastSessionId);
            Assert.NotNull(recorder.LastRoutingId);
            Assert.Equal(1, recorder.ConnectedCount);
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
        ReadOnlySpan<byte> header,
        ReadOnlySpan<byte> body)
    {
        var frame = new byte[6 + header.Length + body.Length];
        frame[0] = (byte)(header.Length >> 8);
        frame[1] = (byte)header.Length;
        frame[2] = (byte)(body.Length >> 24);
        frame[3] = (byte)(body.Length >> 16);
        frame[4] = (byte)(body.Length >> 8);
        frame[5] = (byte)body.Length;

        header.CopyTo(frame.AsSpan(6, header.Length));
        body.CopyTo(frame.AsSpan(6 + header.Length, body.Length));
        return frame;
    }

    public sealed class RawStreamRecorder
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
    }

    public sealed class RawStreamSession(RawStreamRecorder recorder) : IZLinkRawStreamSession
    {
        public ValueTask OnConnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.LastSessionId = stream.SessionId;
            recorder.LastRoutingId = stream.RoutingId;
            recorder.LastLocalAddr = stream.LocalAddr;
            recorder.LastRemoteAddr = stream.RemoteAddr;
            recorder.ConnectedCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
        {
            _ = stream;
            _ = cancellationToken;
            recorder.DisconnectedCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            IZLinkStream stream,
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            _ = stream;
            _ = cancellationToken;
            recorder.LastError = error;
            recorder.ErrorCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnRawAsync(
            IZLinkStream stream,
            global::Zlink.Message payload,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.ReceivedPayloads.Add(Encoding.UTF8.GetString(payload.AsReadOnlySpan()));
            using var reply = global::Zlink.Message.FromString("pong");
            Assert.True(stream.Write(reply));
            return ValueTask.CompletedTask;
        }
    }

    public sealed class PacketStreamRecorder
    {
        public string? LastHeader { get; set; }

        public string? LastBody { get; set; }

        public string? LastSessionId { get; set; }

        public global::Zlink.RoutingId? LastRoutingId { get; set; }

        public string? LastLocalAddr { get; set; }

        public string? LastRemoteAddr { get; set; }

        public int ConnectedCount { get; set; }
    }

    public sealed class PacketStreamSession(PacketStreamRecorder recorder) : IZLinkPacketStreamSession
    {
        public ValueTask OnConnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.LastSessionId = stream.SessionId;
            recorder.LastRoutingId = stream.RoutingId;
            recorder.LastLocalAddr = stream.LocalAddr;
            recorder.LastRemoteAddr = stream.RemoteAddr;
            recorder.ConnectedCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
        {
            _ = stream;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            IZLinkStream stream,
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            _ = stream;
            _ = error;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnPacketAsync(
            IZLinkStream stream,
            global::Zlink.Message header,
            global::Zlink.Message body,
            CancellationToken cancellationToken)
        {
            _ = stream;
            _ = cancellationToken;
            recorder.LastHeader = Encoding.UTF8.GetString(header.AsReadOnlySpan());
            recorder.LastBody = Encoding.UTF8.GetString(body.AsReadOnlySpan());
            return ValueTask.CompletedTask;
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
