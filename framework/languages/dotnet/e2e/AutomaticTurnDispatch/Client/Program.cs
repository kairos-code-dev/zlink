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

        var (trackASpotRid, _) = await AtdA1SingleTerminatorScenario.RunAsync(client);
        await AtdA2AwaitTerminatorScenario.RunAsync(client, trackASpotRid);
        await AtdA3ContinuationContextScenario.RunAsync(client, trackASpotRid);
        await AtdA4WorkerAwaitScenario.RunAsync(client, trackASpotRid);
        await AtdE1TimeoutScenario.RunAsync(client);
        var (timerSpotRid, _) = await AtdC1TimerIsolationScenario.RunAsync(client);
        await AtdC2TimerReentryScenario.RunAsync(client, timerSpotRid);
        await AtdD2RemoteSpotAwaitScenario.RunAsync(client);
        await AtdD3RouteBridgeAwaitScenario.RunAsync(client);
        await AtdE2CancellationScenario.RunAsync(client);

        var spotRid = $"await-track-b-{Guid.NewGuid():N}";
        var actorA = $"actor-a-{Guid.NewGuid():N}";
        var actorB = $"actor-b-{Guid.NewGuid():N}";
        var bound = await client.Request(new BindAwaitActorsReq(spotRid, [actorA, actorB]))
            .PacketName("BindAwaitActorsReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<BindAwaitActorsRes>();
        ScenarioAssert.That(bound.Actors.Length == 2, "ATD-B bind actor count mismatch.");

        var actors = new AwaitActorScenarioContext(spotRid, actorA, actorB);
        await AtdB1OtherActorProgressScenario.RunAsync(client, actors);
        await AtdB2SameActorReentryScenario.RunAsync(client, actors);
        await AtdB3ActorJoinAwaitScenario.RunAsync(client, actors);
        await AtdC3ActorTimerIsolationScenario.RunAsync(client, actors);
        await AtdD4SessionRelayActorAwaitScenario.RunAsync(client, options.SessionBStreamEndpoint, actors);

        Console.WriteLine("automatic-turn-dispatch client result=passed");
        break;
    }
    case "shutdown-wait":
        await ShutdownAwaitScenario.RunWaitAsync(options);
        break;
    case "shutdown-recovery":
        await ShutdownAwaitScenario.RunRecoveryAsync(options);
        break;
    default:
        throw new ArgumentException($"Unknown scenario: {options.Scenario}");
}
