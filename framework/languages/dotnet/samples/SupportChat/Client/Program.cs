using SupportChat.Client.Configuration;
using Microsoft.Extensions.Logging;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Samples.Logging;

namespace SupportChat.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var streamEndpoint = ReadOption(args, "--stream-endpoint")
                             ?? throw new ArgumentException("Missing --stream-endpoint.");
        using var loggerFactory = SampleLogging.CreateFactory(
            SampleLogging.DirectoryFromEnvironment("SUPPORTCHAT_LOG_DIR"),
            "client");
        var logger = loggerFactory.CreateLogger("SupportChat.Client");

        await using var customer = CreateClient(streamEndpoint);
        await using var agent = CreateClient(streamEndpoint);
        await using var reconnectingCustomer = CreateClient(streamEndpoint);
        await using var waitingCustomer = CreateClient(streamEndpoint);
        await using var closingCustomer = CreateClient(streamEndpoint);
        await using var closingAgent = CreateClient(streamEndpoint);

        await new SupportChatClientScenario().RunAsync(
            customer,
            agent,
            reconnectingCustomer,
            waitingCustomer,
            closingCustomer,
            closingAgent);

        logger.LogInformation("supportchat=completed");
    }

    private static IZlinkStreamConnector CreateClient(string streamEndpoint)
    {
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = SampleNames.ConnectTimeout,
            RequestTimeout = SampleNames.RequestTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate
        });
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
