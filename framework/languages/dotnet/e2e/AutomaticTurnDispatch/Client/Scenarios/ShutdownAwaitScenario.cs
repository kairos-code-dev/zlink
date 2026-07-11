using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class ShutdownAwaitScenario
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
            var result = await client.Request(new AwaitShutdownScenarioReq(options.RequestId, options.SpotRid, 30_000))
                .PacketName("AwaitShutdownScenarioReq")
                .Timeout(TimeSpan.FromSeconds(90))
                .Async<AwaitShutdownScenarioRes>();
            throw new InvalidOperationException(
                $"ATD-E3 expected a public shutdown failure while the request was pending, but it completed as {result.Operation}.");
        }
        catch (ZlinkStreamException ex) when (IsExpectedShutdownError(ex))
        {
            Console.WriteLine("automatic-turn-dispatch shutdown wait result=passed");
        }
        catch (OperationCanceledException)
        {
            Console.WriteLine("automatic-turn-dispatch shutdown wait result=passed");
        }
    }

    private static bool IsExpectedShutdownError(ZlinkStreamException exception)
    {
        if (exception.Error.Code == ZlinkStreamErrorCode.RequestTimeout)
            return false;

        // A session disconnect and a server-side remote error are both valid
        // public observations of the downstream runtime stopping. Only a
        // client request timeout would hide the shutdown result.
        return true;
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
        ScenarioAssert.That(result.Operation == "await.e3-shutdown-recovery", "ATD-E3 recovery operation mismatch.");
        ScenarioAssert.That(result.SpotRid == options.SpotRid, "ATD-E3 recovery spot rid mismatch.");
        ScenarioAssert.That(
            result.Evidence.Any(line => line.Contains($"request={options.RequestId}", StringComparison.Ordinal)
                                        && line.Contains("marker=shutdown-recovery-probe", StringComparison.Ordinal)),
            "ATD-E3 recovery probe marker missing.");

        Console.WriteLine("automatic-turn-dispatch shutdown recovery result=passed");
    }

    private static async Task<AwaitShutdownRecoveryRes> RequestRecoveryAfterRouteConvergesAsync(
        IZlinkStreamConnector client,
        ClientOptions options)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(90);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await client.Request(new AwaitShutdownRecoveryReq(options.RequestId, options.SpotRid))
                    .PacketName("AwaitShutdownRecoveryReq")
                    .Timeout(TimeSpan.FromSeconds(45))
                    .Async<AwaitShutdownRecoveryRes>();
            }
            catch (Exception ex) when (ex is ZlinkStreamException or OperationCanceledException)
            {
                last = ex;
                await Task.Delay(500);
            }
        }

        throw new TimeoutException("ATD-E3 recovery did not route to the restarted play node.", last);
    }
}
