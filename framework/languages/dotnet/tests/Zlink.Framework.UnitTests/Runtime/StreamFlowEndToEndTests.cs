using System.Net;
using System.Net.Sockets;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Runtime;
using Systems.Zlink.Stream.Connector.Runtime.Protocol;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Messaging;

namespace Zlink.Framework.UnitTests;

public sealed class StreamFlowEndToEndTests
{
    [Fact]
    public async Task Connector_Request_Server_Log_Reply_And_Callback_Share_One_Flow()
    {
        var root = Path.Combine(Path.GetTempPath(), $"zlink-connector-server-flow-{Guid.NewGuid():N}");
        var logPath = Path.Combine(root, "flow.log");
        var port = FindFreeTcpPort();
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(logPath);
            options.AddStreamNode("flow.stream")
                .Bind($"tcp://127.0.0.1:{port}")
                .AddSession<FlowSession>();
        });
        using var host = builder.Build();

        try
        {
            await host.StartAsync();
            await using var connector = ZlinkStreamConnectorFactory.Create(
                new ZlinkStreamConnectorOptions
                {
                    Endpoint = new Uri($"tcp://127.0.0.1:{port}"),
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    Reconnect = new ZlinkStreamReconnectOptions { Enabled = false },
                    Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false }
                });
            await connector.Connect.Async();
            var completed = new TaskCompletionSource<(
                ZlinkStreamResult<FlowReply> Result,
                string FlowId,
                ZlinkStreamFlowOrigin Origin)>(TaskCreationOptions.RunContinuationsAsynchronously);

            connector.Request(new FlowRequest("request"))
                .PacketName(nameof(FlowRequest))
                .Timeout(TimeSpan.FromSeconds(5))
                .Submit<FlowReply>(result =>
                {
                    var flow = ZlinkStreamFlowContext.Current
                               ?? throw new InvalidOperationException(
                                   "Connector reply callback did not receive the echoed flow context.");
                    completed.TrySetResult((result, flow.FlowId, flow.Origin));
                });

            var callback = await completed.Task.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.True(callback.Result.IsSuccess);
            Assert.Equal("reply", callback.Result.Value?.Value);
            Assert.Equal(ZlinkStreamFlowOrigin.Application, callback.Origin);
            Assert.Null(ZlinkStreamFlowContext.Current);

            var lines = File.ReadAllLines(logPath)
                .Where(line => line.Contains($"flow={callback.FlowId}", StringComparison.Ordinal))
                .ToArray();
            Assert.Equal(2, lines.Length);
            var received = Assert.Single(lines.Where(line =>
                line.Contains("phase=received", StringComparison.Ordinal)));
            var replied = Assert.Single(lines.Where(line =>
                line.Contains("phase=replied", StringComparison.Ordinal)));
            Assert.Contains($"packet={nameof(FlowRequest)}", received, StringComparison.Ordinal);
            Assert.DoesNotContain("packet=", replied, StringComparison.Ordinal);
            Assert.Contains($"flow={callback.FlowId}", received, StringComparison.Ordinal);
            Assert.Contains($"flow={callback.FlowId}", replied, StringComparison.Ordinal);
            Assert.Contains("origin=application", received, StringComparison.Ordinal);
            Assert.Contains("origin=application", replied, StringComparison.Ordinal);
            var correlation = ReadToken(received, "corr");
            Assert.False(string.IsNullOrWhiteSpace(correlation));
            Assert.Equal(correlation, ReadToken(replied, "corr"));

            await connector.Close.Async();
            await host.StopAsync();
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
    }

    private static string? ReadToken(string line, string key)
    {
        var prefix = key + "=";
        return line.Split(' ', StringSplitOptions.RemoveEmptyEntries)
            .FirstOrDefault(token => token.StartsWith(prefix, StringComparison.Ordinal))?[prefix.Length..];
    }

    private static int FindFreeTcpPort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    private sealed record FlowRequest(string Value);

    private sealed record FlowReply(string Value);

    private sealed class FlowSession(IZLinkSessionContext context) : IZLinkSession
    {
        public IZLinkSessionContext Context { get; } = context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDispatchAsync(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload,
            CancellationToken cancellationToken)
        {
            Context.Client.Reply(new FlowReply("reply")).Submit(cancellationToken);
            return ValueTask.CompletedTask;
        }
    }
}
