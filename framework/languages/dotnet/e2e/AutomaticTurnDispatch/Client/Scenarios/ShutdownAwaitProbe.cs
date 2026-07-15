// Verifies shutdown cancels an outstanding await without hanging the host.
using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class ShutdownAwaitProbe
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
                $"TD-F5 expected a public shutdown failure while the request was pending, but it completed as {result.Operation}.");
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

        var result = await client.Request(new AwaitShutdownRecoveryReq(options.RequestId, options.SpotRid))
            .PacketName("AwaitShutdownRecoveryReq")
            .Timeout(TimeSpan.FromSeconds(45))
            .Async<AwaitShutdownRecoveryRes>();
        ZlinkStreamAssert.Ensure(result.Operation == "await.e3-shutdown-recovery", "TD-F5 recovery operation mismatch.");
        ZlinkStreamAssert.Ensure(result.SpotRid == options.SpotRid, "TD-F5 recovery spot rid mismatch.");
        ZlinkStreamAssert.Ensure(
            result.Evidence.Any(line => line.Contains($"request={options.RequestId}", StringComparison.Ordinal)
                                        && line.Contains("marker=shutdown-recovery-probe", StringComparison.Ordinal)),
            "TD-F5 recovery probe marker missing.");

        Console.WriteLine("automatic-turn-dispatch shutdown recovery result=passed");
    }

}
