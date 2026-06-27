using System.Net.Sockets;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using RuntimeMonitoring.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;

namespace RuntimeMonitoring.Trigger;

internal static class TriggerClientRequests
{
    public static async Task<ProfileReply> RequestWithTransientHostAsync(
        TriggerOptions options,
        string channelEndpoint,
        string traceLabel,
        ProfileRequest request)
    {
        using var host = CreateClientHost(options, channelEndpoint, traceLabel);
        await host.StartAsync();
        try
        {
            var channel = host.Services.GetRequiredService<IZLinkChannelClient>();
            return await channel.RequestToChannel(RuntimeMonitoringNames.Channel, request)
                .PacketName("ProfileRequest")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<ProfileReply>();
        }
        finally
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            try
            {
                await host.StopAsync(cts.Token);
            }
            catch (OperationCanceledException)
            {
            }
        }
    }

    public static async Task SendInvalidHandshakeAsync(string endpoint)
    {
        var uri = new Uri(endpoint);
        using var client = new TcpClient();
        await client.ConnectAsync(uri.Host, uri.Port);
        await client.GetStream().WriteAsync("not-a-zlink-handshake"u8.ToArray());
    }

    static IHost CreateClientHost(TriggerOptions options, string channelEndpoint, string traceLabel)
    {
        return Host.CreateDefaultBuilder()
            .ConfigureServices(services =>
            {
                services.AddZLinkFramework(framework =>
                {
                    framework.ConfigureDispatch()
                        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                        .TraceLogFile(Path.Combine(options.LogDir, $"{traceLabel}-flow.log"))
                        .TraceLabel(traceLabel);
                    framework.AddClientServerChannel(RuntimeMonitoringNames.Channel)
                        .EnableClient(channelEndpoint);
                });
            })
            .Build();
    }
}
