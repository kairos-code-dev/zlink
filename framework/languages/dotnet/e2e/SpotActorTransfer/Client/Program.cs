using SpotActorTransfer.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);
using var nodeA = ZLinkHttpClient.Create(options.NodeAUrl).Timeout(TimeSpan.FromSeconds(30)).Build();
using var nodeB = ZLinkHttpClient.Create(options.NodeBUrl).Timeout(TimeSpan.FromSeconds(30)).Build();

var scenarios = new Dictionary<string, Func<Task>>(StringComparer.OrdinalIgnoreCase)
{
    ["ST-A1"] = RunLocalAcceptAsync,
    ["ST-A2"] = RunLocalRejectAsync,
    ["ST-A3"] = RunLocalMovingDispatchBlockedAsync,
    ["ST-B1"] = RunRemoteStatefulTransferAsync,
    ["ST-B2"] = RunSourceCleanupFailureAfterSuccessAsync,
    ["ST-B3"] = RunRemoteMissingAdapterAsync,
    ["ST-B4"] = RunRemoteEmptyStateTransferAsync,
    ["ST-D1"] = RunLocationCommitTimingAsync,
    ["ST-D2"] = RunStaleSourceReleaseFencingAsync,
    ["ST-C3"] = RunCallbackFailureClassificationAsync,
    ["ST-C2"] = RunSourceDownAfterTargetCommitAsync,
    ["ST-C1"] = RunSourceDownBeforeCommitAsync,
    ["ST-E1"] = RunBoundSessionPushAfterRemoteTransferAsync,
    ["ST-E2"] = RunBoundSessionRebindIsolationAsync
};

foreach (var name in SelectedScenarioNames(options.Scenario, scenarios.Keys))
{
    if (!scenarios.TryGetValue(name, out var scenario))
        throw new ArgumentException($"Unknown scenario '{name}'.");

    await scenario();
    Console.WriteLine($"operation SpotActorTransfer.{name} passed");
}

Console.WriteLine("spot-actor-transfer e2e partial result=passed");

async Task RunLocalAcceptAsync()
{
    var actorId = $"actor-local-ok-{Guid.NewGuid():N}";
    var spotRid = $"spot-local-ok-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeA, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 11);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-A1", spotRid));
    Require(join.Accepted, "ST-A1 join was rejected.");

    var probe = await ProbeAsync(nodeA, actorId, new ProbeReq("ST-A1", "after-joined"));
    Require(probe.NodeRid == "actor-a", $"ST-A1 probe expected actor-a, got {probe.NodeRid}.");
    Require(probe.SpotRid == spotRid, "ST-A1 probe did not reach target spot.");

    var evidence = await WaitEvidenceAsync(nodeA, [
        $"ST-A1|{actorId}|admission|spot={spotRid}",
        $"transfer|{actorId}|leave|11",
        $"transfer|{actorId}|joined|{spotRid}:11",
        $"ST-A1|{actorId}|success_reply|{spotRid}",
        $"ST-A1|{actorId}|packet_handler|after-joined"
    ]);
    RequireContains(evidence, $"ST-A1|{actorId}|packet_handler|after-joined", "ST-A1 packet evidence missing.");
}

async Task RunLocalRejectAsync()
{
    var actorId = $"actor-local-reject-{Guid.NewGuid():N}";
    var spotRid = $"spot-local-reject-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeA, spotRid, "reject");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 12);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-A2", spotRid, "reject"));
    Require(!join.Accepted, "ST-A2 join should have been rejected.");

    var evidence = await WaitEvidenceAsync(nodeA, [
        $"ST-A2|{actorId}|admission|spot={spotRid}"
    ]);
    RequireNoContains(evidence, $"transfer|{actorId}|joined|{spotRid}", "ST-A2 joined side effect should not exist.");
}

async Task RunLocalMovingDispatchBlockedAsync()
{
    var actorId = $"actor-local-moving-{Guid.NewGuid():N}";
    var spotRid = $"spot-local-moving-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeA, spotRid, "delay-joined");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 13);

    var joinTask = JoinAsync(nodeA, actorId, new JoinTargetReq("ST-A3", spotRid));
    var waitingEvidence = await WaitEvidenceAsync(nodeA, [
        $"ST-A3|{actorId}|admission|spot={spotRid}",
        $"transfer|{actorId}|leave|13",
        $"ST-A3|{actorId}|joined_wait|{spotRid}"
    ]);
    RequireNoContains(
        waitingEvidence,
        $"ST-A3|{actorId}|packet_handler|during-joined-wait",
        "ST-A3 packet should not run before OnJoinedActorAsync is released.");

    var blockedProbe = ProbeAsync(nodeA, actorId, new ProbeReq("ST-A3", "during-joined-wait"));
    await Task.Delay(500);
    Require(!blockedProbe.IsCompleted, "ST-A3 actor packet completed while OnJoinedActorAsync was still blocked.");

    var release = await ReleaseJoinedGateAsync(nodeA, spotRid);
    Require(release.Released, "ST-A3 joined gate was already released before the scenario released it.");

    var join = await joinTask;
    Require(join.Accepted, "ST-A3 join was rejected.");
    var probe = await blockedProbe.WaitAsync(TimeSpan.FromSeconds(10));
    Require(probe.SpotRid == spotRid, "ST-A3 blocked packet did not resume on the target spot.");

    await WaitEvidenceAsync(nodeA, [
        $"ST-A3|{actorId}|joined_released|{spotRid}",
        $"transfer|{actorId}|joined|{spotRid}:13",
        $"ST-A3|{actorId}|packet_handler|during-joined-wait",
        $"ST-A3|{actorId}|success_reply|{spotRid}"
    ]);
}

async Task RunRemoteStatefulTransferAsync()
{
    var actorId = $"actor-remote-ok-{Guid.NewGuid():N}";
    var spotRid = $"spot-remote-ok-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 21);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-B1", spotRid));
    Require(join.Accepted, "ST-B1 join was rejected.");

    var probe = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-B1", "after-transfer"));
    Require(probe.NodeRid == "actor-b", $"ST-B1 probe expected actor-b, got {probe.NodeRid}.");
    Require(probe.StateVersion == 21, $"ST-B1 state version expected 21, got {probe.StateVersion}.");

    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out|21",
        $"transfer|{actorId}|leave|21",
        $"ST-B1|{actorId}|success_reply|{spotRid}"
    ]);
    await WaitEvidenceAsync(nodeB, [
        $"transfer|{actorId}|transfer_in|21",
        $"transfer|{actorId}|joined|{spotRid}:21",
        $"ST-B1|{actorId}|packet_handler|after-transfer"
    ]);
}

async Task RunSourceCleanupFailureAfterSuccessAsync()
{
    var actorId = $"actor-cleanup-after-success-{Guid.NewGuid():N}";
    var spotRid = $"spot-cleanup-after-success-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 22);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-B2", spotRid));
    Require(join.Accepted, "ST-B2 join was rejected.");
    var beforeShutdown = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-B2", "before-source-cleanup-loss"));
    Require(beforeShutdown.NodeRid == "actor-b", $"ST-B2 probe expected actor-b, got {beforeShutdown.NodeRid}.");

    await ShutdownAsync(nodeA);
    await Task.Delay(TimeSpan.FromSeconds(2));

    var afterShutdown = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-B2", "after-source-cleanup-loss"));
    Require(afterShutdown.NodeRid == "actor-b", $"ST-B2 target ownership was lost after source shutdown: {afterShutdown.NodeRid}.");
    Require(afterShutdown.StateVersion == 22, $"ST-B2 state changed after source cleanup loss: {afterShutdown.StateVersion}.");
    await WaitEvidenceAsync(nodeB, [
        $"transfer|{actorId}|joined|{spotRid}:22",
        $"ST-B2|{actorId}|packet_handler|after-source-cleanup-loss"
    ]);
}

async Task RunRemoteMissingAdapterAsync()
{
    var actorId = $"actor-no-adapter-{Guid.NewGuid():N}";
    var spotRid = $"spot-no-adapter-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeNoAdapter, 31);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-B3", spotRid));
    Require(join.Accepted, "ST-B3 join was rejected.");

    var probe = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-B3", "after-default-empty-transfer"));
    Require(probe.NodeRid == "actor-b", $"ST-B3 probe expected actor-b, got {probe.NodeRid}.");
    Require(probe.StateVersion == 0, $"ST-B3 default empty target state expected 0, got {probe.StateVersion}.");
    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out_empty_default|no-adapter",
        $"transfer|{actorId}|leave|31"
    ]);
    await WaitEvidenceAsync(nodeB, [
        $"transfer|{actorId}|transfer_in_empty_default|actor-factory",
        $"transfer|{actorId}|joined|{spotRid}:0",
        $"ST-B3|{actorId}|packet_handler|after-default-empty-transfer"
    ]);
}

async Task RunRemoteEmptyStateTransferAsync()
{
    var actorId = $"actor-empty-state-{Guid.NewGuid():N}";
    var spotRid = $"spot-empty-state-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeEmptyState, 41);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-B4", spotRid));
    Require(join.Accepted, "ST-B4 join was rejected.");

    var probe = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-B4", "after-empty-state-transfer"));
    Require(probe.NodeRid == "actor-b", $"ST-B4 probe expected actor-b, got {probe.NodeRid}.");
    Require(probe.StateVersion == 41, $"ST-B4 loaded target state expected 41, got {probe.StateVersion}.");

    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out_empty|custom-adapter",
        $"transfer|{actorId}|leave|41"
    ]);
    await WaitEvidenceAsync(nodeB, [
        $"transfer|{actorId}|transfer_in_empty|custom-adapter",
        $"transfer|{actorId}|joined|{spotRid}:0",
        $"transfer|{actorId}|domain_state_loaded|{actorId}",
        $"ST-B4|{actorId}|packet_handler|after-empty-state-transfer"
    ]);
}

async Task RunLocationCommitTimingAsync()
{
    await RunLocalLocationCommitTimingAsync();
    await RunRemoteLocationCommitTimingAsync();
}

async Task RunSourceDownAfterTargetCommitAsync()
{
    var actorId = $"actor-source-down-after-commit-{Guid.NewGuid():N}";
    var spotRid = $"spot-source-down-after-commit-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 61);
    var sourceRef = await GetActorRefAsync(nodeA, actorId);
    await using var bound = await ConnectAndBindAsync(options.NodeBStreamEndpoint, "ST-C2", sourceRef);
    var beforeTransferPush = WaitBoundPushAsync(bound, "bound-before-transfer");
    var beforeTransferReply = await bound.Request(new BoundPushReq("ST-C2", "bound-before-transfer"))
        .PacketName(nameof(BoundPushReq))
        .Async<BoundPushRes>();
    Require(beforeTransferReply.NodeRid == "actor-a", $"ST-C2 pre-transfer bound push expected actor-a, got {beforeTransferReply.NodeRid}.");
    await beforeTransferPush;

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-C2", spotRid));
    Require(join.Accepted, "ST-C2 join was rejected.");
    await WaitEvidenceAsync(nodeB, [
        $"transfer|{actorId}|transfer_in|61",
        $"transfer|{actorId}|joined|{spotRid}:61"
    ]);
    var beforeShutdown = await GetActorRefAsync(nodeB, actorId);
    Require(beforeShutdown.NodeRid == "actor-b", $"ST-C2 target ref expected actor-b, got {beforeShutdown.NodeRid}.");

    await ShutdownAsync(nodeA);
    await Task.Delay(TimeSpan.FromSeconds(2));

    var afterShutdown = await GetActorRefAsync(nodeB, actorId);
    Require(afterShutdown.NodeRid == "actor-b", $"ST-C2 target ref changed after source shutdown: {afterShutdown.NodeRid}.");
    Require(
        afterShutdown.Generation == beforeShutdown.Generation,
        $"ST-C2 target generation changed after source shutdown. before={beforeShutdown.Generation}, after={afterShutdown.Generation}");

    var probe = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-C2", "after-source-down"));
    Require(probe.NodeRid == "actor-b", $"ST-C2 probe expected actor-b, got {probe.NodeRid}.");
    Require(probe.SpotRid == spotRid, "ST-C2 probe did not reach target spot after source shutdown.");
    var pushed = WaitBoundPushAsync(bound, "bound-after-source-down");
    var pushReply = await BoundPushAsync(nodeB, actorId, new BoundPushReq("ST-C2", "bound-after-source-down"));
    var notify = await pushed;
    Require(pushReply.NodeRid == "actor-b", $"ST-C2 bound push reply expected actor-b, got {pushReply.NodeRid}.");
    Require(notify.Payload.NodeRid == "actor-b", $"ST-C2 bound push notify expected actor-b, got {notify.Payload.NodeRid}.");
    Require(notify.Payload.Marker == "bound-after-source-down", "ST-C2 bound push notify marker mismatch.");
    await WaitEvidenceAsync(nodeB, [
        $"ST-C2|{actorId}|packet_handler|after-source-down",
        $"ST-C2|{actorId}|bound_push|bound-after-source-down"
    ]);
}

async Task RunSourceDownBeforeCommitAsync()
{
    var actorId = $"actor-source-down-before-commit-{Guid.NewGuid():N}";
    var spotRid = $"spot-source-down-before-commit-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 62);

    var joinTask = JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-C1", spotRid));
    await WaitEvidenceAsync(nodeB, [
        $"ST-C1|{actorId}|admission|spot={spotRid}"
    ]);
    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out|62",
        $"ST-C1|{actorId}|before_commit_gate|62"
    ]);

    await ShutdownAsync(nodeA);
    try
    {
        var response = await joinTask.WaitAsync(TimeSpan.FromSeconds(3));
        Require(!response.Accepted, "ST-C1 join should not be accepted after source shutdown before commit.");
    }
    catch (Exception ex) when (ex is TimeoutException or InvalidOperationException or HttpRequestException)
    {
        // Source shutdown may abort the HTTP request before the app endpoint can return a failure body.
    }

    await Task.Delay(TimeSpan.FromSeconds(5));
    var targetEvidence = await GetEvidenceAsync(nodeB);
    RequireNoContains(targetEvidence, $"transfer|{actorId}|transfer_in|62", "ST-C1 target should not transfer in without commit.");
    RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C1 target should not join without commit.");
    RequireNoContains(targetEvidence, $"ST-C1|{actorId}|packet_handler|", "ST-C1 target should not dispatch actor packets without commit.");
}

async Task RunCallbackFailureClassificationAsync()
{
    await RunTransferOutFailureAsync();
    await RunSourceLeaveFailureAsync();
    await RunTransferInFailureAsync();
    await RunJoinedFailureAsync();
}

async Task RunStaleSourceReleaseFencingAsync()
{
    var actorId = $"actor-stale-release-{Guid.NewGuid():N}";
    var spotRid = $"spot-stale-release-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 81);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-D2", spotRid));
    Require(join.Accepted, "ST-D2 join was rejected.");
    var before = await GetActorRefAsync(nodeB, actorId);
    Require(before.NodeRid == "actor-b", $"ST-D2 target ref expected actor-b, got {before.NodeRid}.");

    await Task.Delay(TimeSpan.FromSeconds(2));
    var after = await GetActorRefAsync(nodeB, actorId);
    Require(after.NodeRid == "actor-b", $"ST-D2 target ref changed after delayed cleanup: {after.NodeRid}.");
    Require(after.Generation == before.Generation,
        $"ST-D2 generation changed after delayed cleanup. before={before.Generation}, after={after.Generation}");
    var probe = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-D2", "after-stale-cleanup-window"));
    Require(probe.NodeRid == "actor-b", $"ST-D2 probe expected actor-b, got {probe.NodeRid}.");
}

async Task RunBoundSessionPushAfterRemoteTransferAsync()
{
    var actorId = $"actor-bound-session-{Guid.NewGuid():N}";
    var spotRid = $"spot-bound-session-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 91);
    var sourceRef = await GetActorRefAsync(nodeA, actorId);
    await using var bound = await ConnectAndBindAsync(options.NodeAStreamEndpoint, "ST-E1", sourceRef);
    var beforeTransferPush = WaitBoundPushAsync(bound, "before-transfer");
    await BoundPushAsync(nodeA, actorId, new BoundPushReq("ST-E1", "before-transfer"));
    await beforeTransferPush;

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-E1", spotRid));
    Require(join.Accepted, "ST-E1 join was rejected.");
    var pushed = WaitBoundPushAsync(bound, "after-remote-transfer");
    var pushReply = await BoundPushAsync(nodeB, actorId, new BoundPushReq("ST-E1", "after-remote-transfer"));
    var notify = await pushed;
    Require(pushReply.NodeRid == "actor-b", $"ST-E1 bound push reply expected actor-b, got {pushReply.NodeRid}.");
    Require(notify.Payload.NodeRid == "actor-b", $"ST-E1 bound push notify expected actor-b, got {notify.Payload.NodeRid}.");
    Require(notify.Payload.StateVersion == 91, $"ST-E1 bound push state expected 91, got {notify.Payload.StateVersion}.");
}

async Task RunBoundSessionRebindIsolationAsync()
{
    var actorId = $"actor-bound-session-rebind-{Guid.NewGuid():N}";
    var spotRid = $"spot-bound-session-rebind-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 92);
    var sourceRef = await GetActorRefAsync(nodeA, actorId);
    await using var oldSession = await ConnectAndBindAsync(options.NodeAStreamEndpoint, "ST-E2", sourceRef);
    var beforeTransferPush = WaitBoundPushAsync(oldSession, "before-rebind-transfer");
    await BoundPushAsync(nodeA, actorId, new BoundPushReq("ST-E2", "before-rebind-transfer"));
    await beforeTransferPush;

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-E2", spotRid));
    Require(join.Accepted, "ST-E2 join was rejected.");
    var targetRef = await GetActorRefAsync(nodeB, actorId);
    await using var newSession = await ConnectAndBindAsync(options.NodeBStreamEndpoint, "ST-E2", targetRef);

    var oldPush = WaitBoundPushAsync(oldSession, "after-rebind");
    var newPush = WaitBoundPushAsync(newSession, "after-rebind");
    var pushReply = await BoundPushAsync(nodeB, actorId, new BoundPushReq("ST-E2", "after-rebind"));
    var notify = await newPush;
    Require(pushReply.NodeRid == "actor-b", $"ST-E2 bound push reply expected actor-b, got {pushReply.NodeRid}.");
    Require(notify.Payload.Marker == "after-rebind", "ST-E2 new bound session notify marker mismatch.");
    await Task.Delay(500);
    Require(!oldPush.IsCompleted, "ST-E2 old bound session received push after rebind.");
}

async Task RunTransferOutFailureAsync()
{
    var actorId = $"actor-fail-transfer-out-{Guid.NewGuid():N}";
    var spotRid = $"spot-fail-transfer-out-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeFailTransferOut, 71);

    var response = await JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
    Require(!response.Accepted, "ST-C3 transfer-out failure should not return accepted.");
    var sourceEvidence = await WaitEvidenceAsync(nodeA, [
        $"ST-C3|{actorId}|transfer_out_failed|71",
        $"ST-C3|{actorId}|join_failed|"
    ]);
    RequireNoContains(sourceEvidence, $"transfer|{actorId}|leave|71", "ST-C3 transfer-out failure should not leave source.");
    var targetEvidence = await GetEvidenceAsync(nodeB);
    RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C3 transfer-out failure should not join target.");
}

async Task RunSourceLeaveFailureAsync()
{
    var actorId = $"actor-fail-leave-{Guid.NewGuid():N}";
    var spotRid = $"spot-fail-leave-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeFailLeave, 72);

    var response = await JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
    Require(!response.Accepted, "ST-C3 source leave failure should not return accepted.");
    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out|72",
        $"ST-C3|{actorId}|leave_failed|72",
        $"ST-C3|{actorId}|join_failed|"
    ]);
    var targetEvidence = await GetEvidenceAsync(nodeB);
    RequireNoContains(targetEvidence, $"transfer|{actorId}|transfer_in|72", "ST-C3 source leave failure should not transfer in target.");
    RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C3 source leave failure should not join target.");
}

async Task RunTransferInFailureAsync()
{
    var actorId = $"actor-fail-transfer-in-{Guid.NewGuid():N}";
    var spotRid = $"spot-fail-transfer-in-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeFailTransferIn, 73);

    var response = await JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
    Require(!response.Accepted, "ST-C3 transfer-in failure should not return accepted.");
    await WaitEvidenceAsync(nodeB, [
        $"ST-C3|{actorId}|transfer_in_failed|73"
    ]);
    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out|73",
        $"transfer|{actorId}|leave|73",
        $"ST-C3|{actorId}|join_failed|"
    ]);
    var targetEvidence = await GetEvidenceAsync(nodeB);
    RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C3 transfer-in failure should not join target.");
}

async Task RunJoinedFailureAsync()
{
    var actorId = $"actor-fail-joined-{Guid.NewGuid():N}";
    var spotRid = $"spot-fail-joined-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid, "fail-joined");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 74);

    var response = await JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
    Require(!response.Accepted, "ST-C3 joined failure should not return accepted.");
    await WaitEvidenceAsync(nodeB, [
        $"ST-C3|{actorId}|joined_failed|{spotRid}"
    ]);
    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out|74",
        $"transfer|{actorId}|leave|74",
        $"ST-C3|{actorId}|join_failed|"
    ]);
    var targetEvidence = await GetEvidenceAsync(nodeB);
    RequireNoContains(targetEvidence, $"ST-C3|{actorId}|packet_handler|after-joined-failure", "ST-C3 joined failure should not dispatch as joined.");
}

async Task RunLocalLocationCommitTimingAsync()
{
    var actorId = $"actor-location-local-{Guid.NewGuid():N}";
    var spotRid = $"spot-location-local-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeA, spotRid, "delay-joined");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 51);
    var before = await GetActorRefAsync(nodeA, actorId);

    var joinTask = JoinAsync(nodeA, actorId, new JoinTargetReq("ST-D1", spotRid));
    var waitingEvidence = await WaitEvidenceAsync(nodeA, [
        $"ST-D1|{actorId}|admission|spot={spotRid}",
        $"ST-D1|{actorId}|joined_wait|{spotRid}"
    ]);
    RequireNoContains(
        waitingEvidence,
        $"ST-D1|{actorId}|success_reply|{spotRid}",
        "ST-D1 local join returned success before OnJoinedActorAsync completed.");
    var during = await GetActorRefAsync(nodeA, actorId);
    Require(
        during.Generation == before.Generation,
        $"ST-D1 local actor generation changed before joined completed. before={before.Generation}, during={during.Generation}");

    await ReleaseJoinedGateAsync(nodeA, spotRid);
    var join = await joinTask;
    Require(join.Accepted, "ST-D1 local join was rejected.");
    var after = await GetActorRefAsync(nodeA, actorId);
    Require(after.Generation >= before.Generation, "ST-D1 local actor generation regressed after commit.");

    await WaitEvidenceAsync(nodeA, [
        $"ST-D1|{actorId}|joined_released|{spotRid}",
        $"transfer|{actorId}|joined|{spotRid}:51",
        $"ST-D1|{actorId}|success_reply|{spotRid}"
    ]);
}

async Task RunRemoteLocationCommitTimingAsync()
{
    var actorId = $"actor-location-remote-{Guid.NewGuid():N}";
    var spotRid = $"spot-location-remote-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid, "delay-joined");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 52);

    var joinTask = JoinAsync(nodeA, actorId, new JoinTargetReq("ST-D1", spotRid));
    await WaitEvidenceAsync(nodeB, [
        $"ST-D1|{actorId}|admission|spot={spotRid}",
        $"ST-D1|{actorId}|joined_wait|{spotRid}"
    ]);
    var sourceDuring = await GetActorRefAsync(nodeA, actorId);
    Require(
        sourceDuring.NodeRid == "actor-a",
        $"ST-D1 remote source ref moved before target joined completed. got={sourceDuring.NodeRid}");

    await ReleaseJoinedGateAsync(nodeB, spotRid);
    var join = await joinTask;
    Require(join.Accepted, "ST-D1 remote join was rejected.");
    var targetAfter = await GetActorRefAsync(nodeB, actorId);
    Require(
        targetAfter.NodeRid == "actor-b",
        $"ST-D1 remote target ref was not committed after joined completed. got={targetAfter.NodeRid}");

    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|leave|52",
        $"ST-D1|{actorId}|success_reply|{spotRid}"
    ]);
    await WaitEvidenceAsync(nodeB, [
        $"ST-D1|{actorId}|joined_released|{spotRid}",
        $"transfer|{actorId}|joined|{spotRid}:52"
    ]);
}

async Task<CreateSpotRes> CreateSpotAsync(ZLinkHttpClient client, string spotRid, string mode = "accept")
{
    return (await client.Post("/spots").Body(new CreateSpotReq(spotRid, mode)).SubmitAsync<CreateSpotRes>()).Body
           ?? throw new InvalidOperationException("Create spot response was null.");
}

async Task<ActorCreateRes> CreateActorAsync(ZLinkHttpClient client, string actorId, string actorType, int stateVersion)
{
    return (await client.Post("/actors").Body(new ActorCreateReq(actorId, actorType, stateVersion))
               .SubmitAsync<ActorCreateRes>()).Body
           ?? throw new InvalidOperationException("Create actor response was null.");
}

async Task<GateReleaseRes> ReleaseJoinedGateAsync(ZLinkHttpClient client, string spotRid)
{
    return (await client.Post($"/joined-gates/{spotRid}/release").SubmitAsync<GateReleaseRes>()).Body
           ?? throw new InvalidOperationException("Gate release response was null.");
}

async Task<ActorRefSnapshotRes> GetActorRefAsync(ZLinkHttpClient client, string actorId)
{
    return (await client.Get($"/actors/{actorId}/ref").SubmitAsync<ActorRefSnapshotRes>()).Body
           ?? throw new InvalidOperationException("Actor ref response was null.");
}

async Task<IReadOnlyList<ActorEvidence>> GetEvidenceAsync(ZLinkHttpClient client)
{
    return (await client.Get("/evidence").SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
           ?? throw new InvalidOperationException("Evidence response was null.");
}

async Task ShutdownAsync(ZLinkHttpClient client)
{
    await client.Post("/shutdown").SubmitRawAsync();
}

async Task<JoinTargetRes> JoinAsync(ZLinkHttpClient client, string actorId, JoinTargetReq request)
{
    var response = await JoinRawAsync(client, actorId, request);
    return response.ToJoinTargetRes();
}

async Task<JoinResponse> JoinRawAsync(ZLinkHttpClient client, string actorId, JoinTargetReq request)
{
    return (await client.Post($"/actors/{actorId}/join").Body(request).SubmitAsync<JoinResponse>()).Body
           ?? throw new InvalidOperationException("Join response was null.");
}

async Task<ProbeRes> ProbeAsync(ZLinkHttpClient client, string actorId, ProbeReq request)
{
    return (await client.Post($"/actors/{actorId}/probe").Body(request).SubmitAsync<ProbeRes>()).Body
           ?? throw new InvalidOperationException("Probe response was null.");
}

async Task<BoundPushRes> BoundPushAsync(ZLinkHttpClient client, string actorId, BoundPushReq request)
{
    return (await client.Post($"/actors/{actorId}/bound-push").Body(request).SubmitAsync<BoundPushRes>()).Body
           ?? throw new InvalidOperationException("Bound push response was null.");
}

Task<ZlinkStreamMessage<BoundPushNotify>> WaitBoundPushAsync(
    IZlinkStreamConnector stream,
    string marker)
{
    return stream.WaitFor<BoundPushNotify>()
        .Where(message => message.Payload.Marker == marker)
        .Timeout(TimeSpan.FromSeconds(10))
        .Async()
        .AsTask();
}

async Task<IZlinkStreamConnector> ConnectAndBindAsync(
    string endpoint,
    string scenario,
    ActorRefSnapshotRes actor)
{
    var stream = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
    {
        Endpoint = new Uri(endpoint),
        ConnectTimeout = TimeSpan.FromSeconds(5),
        RequestTimeout = TimeSpan.FromSeconds(10),
        Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
        DispatchMode = ZlinkStreamDispatchMode.Immediate,
        MaxReceivedMessages = 1024
    });
    await stream.Connect.Async();
    var bound = await stream.Request(new BindActorSessionReq(
            scenario,
            actor.ActorId,
            actor.NodeRid,
            actor.Generation))
        .PacketName(nameof(BindActorSessionReq))
        .Async<BindActorSessionRes>();
    Require(bound.ActorId == actor.ActorId, $"{scenario} session bind actor mismatch.");
    return stream;
}

async Task<IReadOnlyList<ActorEvidence>> WaitEvidenceAsync(ZLinkHttpClient client, string[] containsAll)
{
    var evidence = (await client.Post("/evidence/wait").Body(new EvidenceWaitReq(containsAll))
            .SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
        ?? throw new InvalidOperationException("Evidence response was null.");

    foreach (var expected in containsAll)
        RequireContains(evidence, expected, $"Expected evidence marker was not observed: {expected}");

    return evidence;
}

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static void RequireContains(IEnumerable<ActorEvidence> evidence, string expected, string message)
{
    Require(evidence.Any(item => EvidenceText(item).Contains(expected, StringComparison.Ordinal)), message);
}

static void RequireNoContains(IEnumerable<ActorEvidence> evidence, string expected, string message)
{
    Require(!evidence.Any(item => EvidenceText(item).Contains(expected, StringComparison.Ordinal)), message);
}

static string EvidenceText(ActorEvidence evidence) =>
    $"{evidence.Scenario}|{evidence.ActorId}|{evidence.Kind}|{evidence.Value}|{evidence.NodeRid}";

static IEnumerable<string> SelectedScenarioNames(string selector, IEnumerable<string> allNames)
{
    if (string.Equals(selector, "all", StringComparison.OrdinalIgnoreCase))
        return allNames;

    return selector
        .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
}

internal sealed record ClientOptions(
    string NodeAUrl,
    string NodeBUrl,
    string NodeAStreamEndpoint,
    string NodeBStreamEndpoint,
    string Scenario)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            var key = args[i].TrimStart('-');
            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{args[i]}'.");
            values[key] = args[i + 1];
        }

        return new ClientOptions(
            values["node-a-url"],
            values["node-b-url"],
            values["node-a-stream-endpoint"],
            values["node-b-stream-endpoint"],
            values.TryGetValue("scenario", out var scenario) ? scenario : "all");
    }
}

internal sealed record JoinResponse(
    string Scenario,
    string ActorId,
    bool Accepted,
    string? SourceNodeRid,
    string? TargetSpotRid,
    int? StateVersion,
    string? ErrorKind)
{
    public JoinTargetRes ToJoinTargetRes()
    {
        return new JoinTargetRes(
            Scenario,
            ActorId,
            Accepted,
            SourceNodeRid ?? "",
            TargetSpotRid ?? "",
            StateVersion ?? 0);
    }
}
