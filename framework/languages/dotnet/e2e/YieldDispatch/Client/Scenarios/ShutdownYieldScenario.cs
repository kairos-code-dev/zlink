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
                .Async<YieldShutdownScenarioRes>();
            throw new InvalidOperationException(
                $"YD-E3 expected play-a shutdown while yield was pending, but request completed as {result.Operation}.");
        }
        catch (ZlinkStreamException ex) when (IsExpectedShutdownError(ex))
        {
            Console.WriteLine("yield-dispatch shutdown wait result=passed");
        }
        catch (OperationCanceledException)
        {
            Console.WriteLine("yield-dispatch shutdown wait result=passed");
        }
    }

    private static bool IsExpectedShutdownError(ZlinkStreamException exception)
    {
        if (exception.Error.Code == ZlinkStreamErrorCode.RequestTimeout)
            return false;

        if (exception.Error.Code == ZlinkStreamErrorCode.Disconnected)
            return true;

        if (exception.InnerException is OperationCanceledException)
            return true;

        return exception.Message.Contains("cancel", StringComparison.OrdinalIgnoreCase)
               || exception.Message.Contains("closed", StringComparison.OrdinalIgnoreCase)
               || exception.Message.Contains("disconnect", StringComparison.OrdinalIgnoreCase)
               || exception.Message.Contains("terminated", StringComparison.OrdinalIgnoreCase);
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

        var result = await RequestRecoveryAfterRouteConvergesAsync(client, options);
        ScenarioAssert.That(result.Operation == "yield.e3-shutdown-recovery", "YD-E3 recovery operation mismatch.");
        ScenarioAssert.That(result.SpotRid == options.SpotRid, "YD-E3 recovery spot rid mismatch.");
        ScenarioAssert.That(
            result.Evidence.Any(line => line.Contains($"request={options.RequestId}", StringComparison.Ordinal)
                                        && line.Contains("marker=shutdown-recovery-probe", StringComparison.Ordinal)),
            "YD-E3 recovery probe marker missing.");

        Console.WriteLine("yield-dispatch shutdown recovery result=passed");
    }

    private static async Task<YieldShutdownRecoveryRes> RequestRecoveryAfterRouteConvergesAsync(
        IZlinkStreamConnector client,
        ClientOptions options)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(90);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await client.Request(new YieldShutdownRecoveryReq(options.RequestId, options.SpotRid))
                    .PacketName("YieldShutdownRecoveryReq")
                    .Timeout(TimeSpan.FromSeconds(45))
                    .Async<YieldShutdownRecoveryRes>();
            }
            catch (Exception ex) when (ex is ZlinkStreamException or OperationCanceledException)
            {
                last = ex;
                await Task.Delay(500);
            }
        }

        throw new TimeoutException("YD-E3 recovery did not route to the restarted play node.", last);
    }
}
