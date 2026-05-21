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

[CollectionDefinition(nameof(StreamTestsCollection), DisableParallelization = true)]
public sealed class StreamTestsCollection
{
}

public abstract partial class StreamTestSupport
{
    private protected static readonly TimeSpan PollingInterval = TimeSpan.FromMilliseconds(150);
    private protected static async Task<IHost> CreateHostAsync(
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

    private protected static async Task RetryAsync(Func<bool> predicate, TimeSpan timeout)
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

    private protected static async Task<T> RetryAsync<T>(
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

    private protected static string GetFreeTcpEndpoint()
    {
        return ChannelMessagingTestSupport.GetTcpEndpoint();
    }

    private protected static ZlinkStreamHeader CreateStreamHeader(
        ZlinkStreamMessageKind kind,
        string packetName)
    {
        var requestSeq = kind is ZlinkStreamMessageKind.Request or ZlinkStreamMessageKind.Response
            ? new ZlinkStreamRequestSeq(1)
            : (ZlinkStreamRequestSeq?)null;
        return new ZlinkStreamHeader(
            kind,
            kind == ZlinkStreamMessageKind.Control
                ? ZlinkStreamCodec.Raw
                : ZlinkStreamCodec.Json,
            requestSeq is not null
                ? ZlinkStreamHeaderFlags.HasRequestSeq
                : ZlinkStreamHeaderFlags.None,
            requestSeq,
            packetName,
            ZlinkStreamMetadata.Empty);
    }

    private protected static TcpClient ConnectRawClient(string endpoint)
    {
        var uri = new Uri(endpoint);
        var client = new TcpClient();
        client.NoDelay = true;
        client.ReceiveTimeout = 5000;
        client.SendTimeout = 5000;
        client.Connect(IPAddress.Parse(uri.Host), uri.Port);
        return client;
    }

    private protected static void SendAll(NetworkStream stream, ReadOnlySpan<byte> payload)
    {
        stream.Write(payload);
        stream.Flush();
    }

    private protected static byte[] ReceiveExact(NetworkStream stream, int size)
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

    private protected static byte[] BuildStreamPacketFrame(
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> payload)
    {
        var headerBytes = ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray();
        var frame = new byte[6 + headerBytes.Length + payload.Length];
        frame[0] = (byte)(headerBytes.Length >> 8);
        frame[1] = (byte)headerBytes.Length;
        frame[2] = (byte)(payload.Length >> 24);
        frame[3] = (byte)(payload.Length >> 16);
        frame[4] = (byte)(payload.Length >> 8);
        frame[5] = (byte)payload.Length;

        headerBytes.CopyTo(frame.AsSpan(6, headerBytes.Length));
        payload.CopyTo(frame.AsSpan(6 + headerBytes.Length, payload.Length));
        return frame;
    }

    private protected static (ZlinkStreamHeader Header, byte[] Payload) ReceiveFrame(NetworkStream stream)
    {
        var lengths = ReceiveExact(stream, 6);
        var headerLength = (lengths[0] << 8) | lengths[1];
        var payloadLength = (lengths[2] << 24) | (lengths[3] << 16) | (lengths[4] << 8) | lengths[5];
        var headerBytes = ReceiveExact(stream, headerLength);
        var payloadBytes = ReceiveExact(stream, payloadLength);
        var header = ZLinkStreamProtocolDefaults.DecodeHeader(headerBytes);
        return (header, payloadBytes);
    }

    private protected static (ZlinkStreamHeader Header, byte[] Payload) ReceiveFrame(
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

    private protected static void AssertStreamMetadata(
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

    private protected static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
}
