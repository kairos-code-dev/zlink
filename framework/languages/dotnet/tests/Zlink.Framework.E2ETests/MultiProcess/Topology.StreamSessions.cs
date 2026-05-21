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
    [Fact]
    public async Task StreamRawSession_OnConnected_Emits_Metadata_Once_From_TestHostProcess()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var eventFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-stream-connected-{Guid.NewGuid():N}.log");

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var streamHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "stream-raw",
            "--stream-endpoint", streamEndpoint,
            "--event-file", eventFilePath);

        using var client = ConnectRawClient(streamEndpoint);
        var clientLocalPort = ((IPEndPoint)client.Client.LocalEndPoint!).Port;
        var network = client.GetStream();
        SendAll(network, BuildStreamPacketFrame(
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Request,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.HasRequestSeq,
                new ZlinkStreamRequestSeq(1),
                "ping",
                ZlinkStreamMetadata.Empty),
            "\"ping\""u8));
        var reply = ReceiveFrame(network);
        Assert.Equal(ZlinkStreamMessageKind.Response, reply.Header.Kind);
        Assert.Equal("ping", reply.Header.Name);
        Assert.Equal("\"pong\"", Encoding.UTF8.GetString(reply.Payload));

        var connectedLine = await WaitForFileLineAsync(
            eventFilePath,
            static value => value.StartsWith("connected|", StringComparison.Ordinal),
            TimeSpan.FromSeconds(15));

        var parts = connectedLine.Split('|');
        Assert.Equal(5, parts.Length);
        Assert.False(string.IsNullOrWhiteSpace(parts[1]));
        Assert.False(string.IsNullOrWhiteSpace(parts[2]));
        Assert.StartsWith("tcp://", parts[3], StringComparison.Ordinal);
        Assert.StartsWith("tcp://", parts[4], StringComparison.Ordinal);
        Assert.Contains($":{new Uri(streamEndpoint).Port}", parts[3], StringComparison.Ordinal);
        Assert.Contains($":{clientLocalPort}", parts[4], StringComparison.Ordinal);

        var connectedCount = (await File.ReadAllLinesAsync(eventFilePath))
            .Count(static line => line.StartsWith("connected|", StringComparison.Ordinal));
        Assert.Equal(1, connectedCount);
    }

    [Fact]
    public async Task StreamRawSession_OnError_Reports_TransportError_For_RemoteDisconnect()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var eventFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-stream-error-{Guid.NewGuid():N}.log");

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var streamHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "stream-raw",
            "--stream-endpoint", streamEndpoint,
            "--event-file", eventFilePath);

        var client = ConnectRawClient(streamEndpoint);
        var network = client.GetStream();
        SendAll(network, BuildStreamPacketFrame(
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Request,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.HasRequestSeq,
                new ZlinkStreamRequestSeq(1),
                "ping",
                ZlinkStreamMetadata.Empty),
            "\"ping\""u8));
        _ = ReceiveFrame(network);
        client.Dispose();

        var errorLine = await WaitForFileLineAsync(
            eventFilePath,
            static value => string.Equals(value, "error|TransportError", StringComparison.Ordinal),
            TimeSpan.FromSeconds(15));

        Assert.Equal("error|TransportError", errorLine);
    }
}
