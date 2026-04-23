using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

public sealed class StreamIntegrationTests
{
    [Fact]
    public async Task RawStreamSession_Receives_Writes_And_Tracks_Lifecycle()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new RawStreamRecorder();

        using var host = await CreateHostAsync(endpoint, services =>
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

        using var client = ConnectRawClient(endpoint);
        var network = client.GetStream();
        SendAll(network, "ping"u8);

        await RetryAsync(() => recorder.ReceivedPayloads.Contains("ping"), TimeSpan.FromSeconds(5));
        Assert.Equal("pong", Encoding.UTF8.GetString(ReceiveExact(network, 4)));

        client.Dispose();
        await RetryAsync(() => recorder.DisconnectedCount > 0, TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task PacketStreamSession_Receives_Header_And_Body()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new PacketStreamRecorder();

        using var host = await CreateHostAsync(endpoint, services =>
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

        using var client = ConnectRawClient(endpoint);
        var network = client.GetStream();
        SendAll(network, BuildLen32BeFrame("hdr"u8));
        SendAll(network, BuildLen32BeFrame("body"u8));

        await RetryAsync(
            () => recorder.LastHeader == "hdr" && recorder.LastBody == "body",
            TimeSpan.FromSeconds(5));
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

            await Task.Delay(50);
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

    private static byte[] BuildLen32BeFrame(ReadOnlySpan<byte> payload)
    {
        var frame = new byte[4 + payload.Length];
        BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(0, 4), (uint)payload.Length);
        payload.CopyTo(frame.AsSpan(4));
        return frame;
    }

    public sealed class RawStreamRecorder
    {
        public List<string> ReceivedPayloads { get; } = [];

        public string? LastLocalAddr { get; set; }

        public string? LastRemoteAddr { get; set; }

        public int DisconnectedCount { get; set; }
    }

    public sealed class RawStreamSession(RawStreamRecorder recorder) : IZLinkRawStreamSession
    {
        public ValueTask OnConnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.LastLocalAddr = stream.LocalAddr;
            recorder.LastRemoteAddr = stream.RemoteAddr;
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
            _ = error;
            _ = cancellationToken;
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

        public string? LastLocalAddr { get; set; }

        public string? LastRemoteAddr { get; set; }
    }

    public sealed class PacketStreamSession(PacketStreamRecorder recorder) : IZLinkPacketStreamSession
    {
        public ValueTask OnConnectedAsync(IZLinkStream stream, CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.LastLocalAddr = stream.LocalAddr;
            recorder.LastRemoteAddr = stream.RemoteAddr;
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
}
