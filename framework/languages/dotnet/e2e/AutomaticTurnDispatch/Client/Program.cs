using AutomaticTurnDispatch.Client.Scenarios;
using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

var options = ClientOptions.Parse(args);

switch (options.Scenario)
{
    case "full":
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

        var suite = new ExecutionTurnScenarioSuite(client, options.SessionBStreamEndpoint);
        var scenarios = new (string Id, Func<Task> Run)[]
        {
            ("TD-A1", () => TdA1TerminatorSurfaceScenario.RunAsync(suite)),
            ("TD-A2", () => TdA2AsyncCompletionOrderScenario.RunAsync(suite)),
            ("TD-A3", () => TdA3AsyncCounterSerializationScenario.RunAsync(suite)),
            ("TD-A4", () => TdA4DelayedAsyncCompletionScenario.RunAsync(suite)),
            ("TD-A5", () => TdA5AsyncTimerExclusionScenario.RunAsync(suite)),
            ("TD-B1", () => TdB1YieldProbeInterleaveScenario.RunAsync(suite)),
            ("TD-B2", () => TdB2YieldQueuedProbeOrderScenario.RunAsync(suite)),
            ("TD-B3", () => TdB3YieldLostUpdateScenario.RunAsync(suite)),
            ("TD-B4", () => TdB4YieldTimerInterleaveScenario.RunAsync(suite)),
            ("TD-C1", () => TdC1HttpYieldInterleaveScenario.RunAsync(suite)),
            ("TD-C2", () => TdC2HttpAsyncExclusionScenario.RunAsync(suite)),
            ("TD-C3", () => TdC3IoWorkerCapacityScenario.RunAsync(suite)),
            ("TD-C4", () => TdC4CpuWorkerTurnOrderScenario.RunAsync(suite)),
            ("TD-C5", () => TdC5CpuWorkerSourceGateScenario.RunAsync(suite)),
            ("TD-D1", () => TdD1CrossActorYieldInterleaveScenario.RunAsync(suite)),
            ("TD-D2", () => TdD2SameActorNoReentryScenario.RunAsync(suite)),
            ("TD-D3", () => TdD3TimerNoReentryScenario.RunAsync(suite)),
            ("TD-E1", () => TdE1EntryToUserSpotJoinScenario.RunAsync(suite)),
            ("TD-E2", () => TdE2UserToUserSpotJoinScenario.RunAsync(suite)),
            ("TD-E3", () => TdE3OppositeSpotJoinScenario.RunAsync(suite)),
            ("TD-F1", () => TdF1RemoteSpotContinuationScenario.RunAsync(suite)),
            ("TD-F2", () => TdF2RouteBridgeYieldScenario.RunAsync(suite)),
            ("TD-F3", () => TdF3SessionRelayYieldScenario.RunAsync(suite)),
            ("TD-F4", () => TdF4RequestTimeoutRecoveryScenario.RunAsync(suite)),
            ("TD-F5", () => TdF5CancellationShutdownRecoveryScenario.RunAsync(suite)),
            ("TD-F6", () => TdF6SelfRequestTimeoutRecoveryScenario.RunAsync(suite)),
            ("TD-G1", () => TdG1TerminatorConformanceScenario.RunAsync(suite))
        };
        foreach (var scenario in scenarios)
        {
            await scenario.Run();
            Console.WriteLine($"{scenario.Id} result=passed");
        }

        Console.WriteLine("automatic-turn-dispatch client result=passed");
        break;
    }
    case "shutdown-wait":
        await ShutdownAwaitProbe.RunWaitAsync(options);
        break;
    case "shutdown-recovery":
        await ShutdownAwaitProbe.RunRecoveryAsync(options);
        break;
    default:
        throw new ArgumentException($"Unknown scenario: {options.Scenario}");
}
