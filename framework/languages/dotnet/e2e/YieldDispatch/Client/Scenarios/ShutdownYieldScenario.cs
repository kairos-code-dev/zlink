using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class ShutdownYieldScenario
{
    public static async Task RunWaitAsync(ClientOptions options)
    {
        await using var client = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(options.SessionAStreamEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(60),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024
        });
        await client.Connect.Async();

        try
        {
            var result = await client.Request(new YieldShutdownScenarioReq(options.RequestId, options.SpotRid, 30_000))
                .PacketName("YieldShutdownScenarioReq")
                .Timeout(TimeSpan.FromSeconds(90))
                .Async<YieldScenarioResult>();
            throw new InvalidOperationException(
                $"YD-E3 expected play-a shutdown while yield was pending, but request completed as {result.Operation}.");
        }
        catch (Exception ex) when (ex is ZlinkStreamException or OperationCanceledException)
        {
            Console.WriteLine("yield-dispatch shutdown wait result=passed");
        }
    }

    public static async Task RunRecoveryAsync(ClientOptions options)
    {
        await using var client = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(options.SessionAStreamEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(60),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024
        });
        await client.Connect.Async();

        var result = await client.Request(new YieldShutdownRecoveryReq(options.RequestId, options.SpotRid))
            .PacketName("YieldShutdownRecoveryReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldScenarioResult>();
        ScenarioAssert.That(result.Operation == "yield.e3-shutdown-recovery", "YD-E3 recovery operation mismatch.");
        ScenarioAssert.That(result.SpotRid == options.SpotRid, "YD-E3 recovery spot rid mismatch.");
        ScenarioAssert.That(
            result.Evidence.Any(line => line.Contains($"request={options.RequestId}", StringComparison.Ordinal)
                                        && line.Contains("marker=shutdown-recovery-probe", StringComparison.Ordinal)),
            "YD-E3 recovery probe marker missing.");

        Console.WriteLine("yield-dispatch shutdown recovery result=passed");
    }
}