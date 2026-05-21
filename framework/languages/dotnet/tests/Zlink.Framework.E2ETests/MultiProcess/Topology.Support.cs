using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Streams;
using Zlink.Framework.Tests.Common;

namespace Zlink.Framework.E2ETests.MultiProcess;

public sealed partial class TopologyTests
{
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

    private static (ZlinkStreamHeader Header, byte[] Payload) ReceiveFrame(NetworkStream stream)
    {
        var lengths = ReceiveExact(stream, 6);
        var headerLength = (lengths[0] << 8) | lengths[1];
        var payloadLength = (lengths[2] << 24) | (lengths[3] << 16) | (lengths[4] << 8) | lengths[5];
        var headerBytes = ReceiveExact(stream, headerLength);
        var payloadBytes = ReceiveExact(stream, payloadLength);
        var header = ZLinkStreamProtocolDefaults.DecodeHeader(headerBytes);
        return (header, payloadBytes);
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

            await Task.Delay(TimeSpan.FromMilliseconds(150));
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("Multi-process retry timed out.");
    }

    private static async Task<string> WaitForFileLineAsync(
        string path,
        Func<string, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;

        while (DateTime.UtcNow < deadline)
        {
            if (File.Exists(path))
            {
                var lines = await File.ReadAllLinesAsync(path);
                var match = lines.FirstOrDefault(predicate);
                if (match is not null)
                {
                    return match;
                }
            }

            await Task.Delay(TimeSpan.FromMilliseconds(150));
        }

        throw new TimeoutException($"Timed out waiting for event file '{path}'.");
    }

    public sealed class RegistryChangeProbe : IZLinkRuntimeEventHandler<ZLinkRegistryEvent>
    {
        private readonly TaskCompletionSource<ZLinkRegistryEvent> _topology =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource<ZLinkRegistryEvent> _summary =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkRegistryEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;

            if (@event.Event == ZLinkRegistryEventKind.TopologyChanged
                && @event.Topology is { Count: > 0 })
            {
                _topology.TrySetResult(@event);
            }

            if (@event.Event == ZLinkRegistryEventKind.ServiceSummaryChanged
                && @event.ServiceSummary is { Count: > 0 })
            {
                _summary.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkRegistryEvent> WaitForTopologyAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _topology.Task.WaitAsync(timeoutSource.Token);
        }

        public async Task<ZLinkRegistryEvent> WaitForSummaryAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _summary.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class SpotChangeProbe : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
    {
        private readonly TaskCompletionSource<ZLinkSpotEvent> _peer =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource<ZLinkSpotEvent> _subject =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkSpotEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;

            if (@event.Event == ZLinkSpotEventKind.PeersChanged
                && @event.Peers is { Count: > 0 })
            {
                _peer.TrySetResult(@event);
            }

            if (@event.Event == ZLinkSpotEventKind.SubjectsChanged
                && @event.Subjects is { Count: > 0 })
            {
                _subject.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkSpotEvent> WaitForPeerAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _peer.Task.WaitAsync(timeoutSource.Token);
        }

        public async Task<ZLinkSpotEvent> WaitForSubjectAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _subject.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class LocalMonitoringStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddSubscribe<LocalMonitoringStageSubscriptionHandler>("stage.monitor");
        }
    }

    public sealed record LocalMonitoringStageEvent(string Value);

    public sealed class LocalMonitoringStageSubscriptionHandler
        : IZLinkSpotSubscriptionHandler<LocalMonitoringStageSpot, LocalMonitoringStageEvent>
    {
        public ValueTask HandleAsync(
            LocalMonitoringStageSpot spot,
            LocalMonitoringStageEvent message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = message;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }
}
