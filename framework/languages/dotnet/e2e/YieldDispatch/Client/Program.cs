using YieldDispatch.Client.Scenarios;
using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

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

        var (trackASpotRid, _) = await YdA1BasicTerminatorScenario.RunAsync(client);
        await YdA2YieldTerminatorScenario.RunAsync(client, trackASpotRid);
        await YdA3ContinuationContextScenario.RunAsync(client, trackASpotRid);
        await YdA4WorkerYieldScenario.RunAsync(client, trackASpotRid);
        await YdE1TimeoutScenario.RunAsync(client);
        var (timerSpotRid, _) = await YdC1TimerIsolationScenario.RunAsync(client);
        await YdC2TimerReentryScenario.RunAsync(client, timerSpotRid);
        await YdD2RemoteSpotYieldScenario.RunAsync(client);
        await YdD3RouteBridgeYieldScenario.RunAsync(client);
        await YdE2CancellationScenario.RunAsync(client);

        var spotRid = $"yield-track-b-{Guid.NewGuid():N}";
        var actorA = $"actor-a-{Guid.NewGuid():N}";
        var actorB = $"actor-b-{Guid.NewGuid():N}";
        var bound = await client.Request(new BindYieldActorsReq(spotRid, [actorA, actorB]))
            .PacketName("BindYieldActorsReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<BindYieldActorsReply>();
        ScenarioAssert.That(bound.Actors.Length == 2, "YD-B bind actor count mismatch.");

        var actors = new YieldActorScenarioContext(spotRid, actorA, actorB);
        await YdB1OtherActorProgressScenario.RunAsync(client, actors);
        await YdB2SameActorReentryScenario.RunAsync(client, actors);
        await YdB3ActorJoinYieldScenario.RunAsync(client, actors);
        await YdC3ActorTimerIsolationScenario.RunAsync(client, actors);
        await YdD4SessionRelayActorYieldScenario.RunAsync(client, options.SessionBStreamEndpoint, actors);

        Console.WriteLine("yield-dispatch client result=passed");
        break;
    }
    case "shutdown-wait":
        await ShutdownYieldScenario.RunWaitAsync(options);
        break;
    case "shutdown-recovery":
        await ShutdownYieldScenario.RunRecoveryAsync(options);
        break;
    default:
        throw new ArgumentException($"Unknown scenario: {options.Scenario}");
}