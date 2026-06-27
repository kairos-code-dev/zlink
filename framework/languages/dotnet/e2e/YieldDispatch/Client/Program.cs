using YieldDispatch.Client.Scenarios;
using YieldDispatch.Client.Support;

var options = ClientOptions.Parse(args);

switch (options.Scenario)
{
    case "full":
        await using (var context = await YieldDispatchScenarioContext.ConnectAsync(options))
        {
            var (trackASpotRid, _) = await YdA1BasicTerminatorScenario.RunAsync(context);
            await YdA2YieldTerminatorScenario.RunAsync(context, trackASpotRid);
            await YdA3ContinuationContextScenario.RunAsync(context, trackASpotRid);
            await YdA4WorkerYieldScenario.RunAsync(context, trackASpotRid);
            await YdE1TimeoutScenario.RunAsync(context);
            var (timerSpotRid, _) = await YdC1TimerIsolationScenario.RunAsync(context);
            await YdC2TimerReentryScenario.RunAsync(context, timerSpotRid);
            await YdD2RemoteSpotYieldScenario.RunAsync(context);
            await YdD3RouteBridgeYieldScenario.RunAsync(context);
            await YdE2CancellationScenario.RunAsync(context);
            var actors = await YieldActorScenarioContext.CreateAsync(context);
            await YdB1OtherActorProgressScenario.RunAsync(context, actors);
            await YdB2SameActorReentryScenario.RunAsync(context, actors);
            await YdB3ActorJoinYieldScenario.RunAsync(context, actors);
            await YdC3ActorTimerIsolationScenario.RunAsync(context, actors);
            await YdD4SessionRelayActorYieldScenario.RunAsync(context, actors);
        }

        Console.WriteLine("yield-dispatch client result=passed");
        break;
    case "shutdown-wait":
        await ShutdownYieldScenario.RunWaitAsync(options);
        break;
    case "shutdown-recovery":
        await ShutdownYieldScenario.RunRecoveryAsync(options);
        break;
    default:
        throw new ArgumentException($"Unknown scenario: {options.Scenario}");
}
