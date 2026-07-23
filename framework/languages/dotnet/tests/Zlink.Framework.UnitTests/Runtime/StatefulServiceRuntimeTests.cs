using System.Diagnostics;
using Systems.Zlink.Framework.Runtime.Protocol;

namespace Zlink.Framework.UnitTests;

public sealed class StatefulServiceRuntimeTests
{
    [Fact]
    public void StatefulWireRoundTripsExactSpotAndActorFences()
    {
        var nodeRid = RoutingId.From("wire-node");
        var spotRid = RoutingId.From("wire-spot");
        var sourceSpotRid = RoutingId.From("wire-source");
        var spot = ZLinkServiceWireCodec.EncodeSpot(
            ServiceWireConstants.Command.SpotRequest,
            9,
            sourceSpotRid,
            spotRid,
            10,
            nodeRid,
            11,
            12,
            hasMetadata: true);
        Assert.True(ZLinkServiceWireCodec.TryDecodeStateful(
            spot,
            out var spotRecord,
            out var spotError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, spotError);
        Assert.Equal(9UL, spotRecord.Correlation);
        Assert.Equal(sourceSpotRid, spotRecord.SourceSpotRid);
        Assert.Equal(spotRid, spotRecord.TargetSpotRid);
        Assert.Equal(10UL, spotRecord.TargetSpotGeneration);

        var actor = new ActorRef(nodeRid, "wire-actor", 13);
        var actorBytes = ZLinkServiceWireCodec.EncodeActor(
            ServiceWireConstants.Command.ActorSend,
            0,
            actor,
            nodeRid,
            14,
            15,
            hasMetadata: false);
        Assert.True(ZLinkServiceWireCodec.TryDecodeStateful(
            actorBytes,
            out var actorRecord,
            out var actorError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, actorError);
        Assert.Equal(actor, actorRecord.TargetActor);
        Assert.Equal(14UL, actorRecord.TargetNodeGeneration);
        Assert.Equal(15UL, actorRecord.AuthorityOwnerGeneration);
    }

    [Fact]
    public async Task ActorPayloadUsesActorMailboxAndLifecycleUsesSpotMailbox()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var node = NewNode(context, "stateful-node");
        var actor = node.CreateActor("player-1");

        using var payload = Message.From(new byte[] { 1 });
        Assert.Equal(SubmitResult.Ok, node.SendToActor(actor, [payload]));

        using var ready = new MeshReadyBatch();
        node.DrainReady(MeshReadyDomains.All, ready, RecvFlags.DontWait);
        Assert.Equal(2, ready.Count);
        Assert.Contains(
            Enumerable.Range(0, ready.Count),
            index => ready[index].OwnerKind == MeshOwnerKind.Actor
                     && ready[index].Domain == MeshReadyDomains.Application);
        Assert.Contains(
            Enumerable.Range(0, ready.Count),
            index => ready[index].OwnerKind == MeshOwnerKind.Spot
                     && ready[index].Domain == MeshReadyDomains.Infrastructure);

        var actorIndex = Enumerable.Range(0, ready.Count)
            .Single(index => ready[index].OwnerKind == MeshOwnerKind.Actor);
        using var actorClaim = ready.TakeClaim(actorIndex);
        using var received = new MeshReceiveBatch();
        Assert.True(actorClaim.Receive(received, RecvFlags.DontWait));
        Assert.Equal(MeshRecordKind.ActorSend, received[0].Kind);
        Assert.Equal(actor, ready[actorIndex].Actor);
    }

    [Fact]
    public async Task LogicalMulticastSnapshotsMatchingSpotMailboxes()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var node = NewNode(context, "multicast-node");
        var publisher = node.CreateSpot();
        var first = node.CreateSpot();
        var second = node.CreateSpot();
        first.SetSubscription("events", "room.updated");
        second.SetSubscription("events", "room.updated");

        using var payload = Message.From(new byte[] { 2 });
        var result = publisher.Publish(
            "events",
            "room.updated",
            [payload]);
        Assert.Equal(SubmitResult.Ok, result.Result);
        Assert.Equal(2UL, result.Detail.SnapshotLocalSpots);
        Assert.Equal(2UL, result.Detail.AdmittedLocalSpots);

        using var ready = new MeshReadyBatch();
        node.DrainReady(MeshReadyDomains.Application, ready, RecvFlags.DontWait);
        Assert.Equal(2, ready.Count);
        foreach (var index in Enumerable.Range(0, ready.Count))
        {
            using var claim = ready.TakeClaim(index);
            using var received = new MeshReceiveBatch();
            Assert.True(claim.Receive(received, RecvFlags.DontWait));
            Assert.Equal(MeshRecordKind.SpotMulticast, received[0].Kind);
            Assert.Equal("events", received[0].ChannelName);
            Assert.Equal("room.updated", received[0].Topic);
        }
    }

    [Fact]
    public async Task JoinCommitsMembershipOnlyAfterAcceptedReply()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var node = NewNode(context, "join-node");
        var actor = node.CreateActor("player-2");
        DrainAndDispose(node);
        var targetRid = RoutingId.From("room-7");
        var target = node.GetOrCreateSpot(targetRid, out var created);
        Assert.True(created);

        var operation = node.JoinSpot(
            actor,
            node.RoutingId,
            targetRid,
            target.LifecycleGeneration);
        Assert.True(node.ActorLookup(actor.ActorId, out var before));
        Assert.NotEqual(targetRid, before.SpotRid);

        using var ready = new MeshReadyBatch();
        node.DrainReady(MeshReadyDomains.All, ready, RecvFlags.DontWait);
        var joinIndex = Enumerable.Range(0, ready.Count)
            .Single(index => ready[index].SpotRid == targetRid);
        using var claim = ready.TakeClaim(joinIndex);
        using var received = new MeshReceiveBatch();
        Assert.True(claim.Receive(received, RecvFlags.DontWait));
        Assert.Equal(MeshRecordKind.SpotControl, received[0].Kind);
        Assert.Equal(MeshOperationKind.ActorJoin, received[0].OperationKind);
        Assert.Equal(
            SubmitResult.Ok,
            received[0].ReplyJoin(
                ActorJoinResult.Accepted,
                Array.Empty<Message>()));

        Assert.True(node.ActorLookup(actor.ActorId, out var after));
        Assert.Equal(targetRid, after.SpotRid);
        Assert.Equal(before.MembershipEpoch + 1, after.MembershipEpoch);

        var completions = DrainRecords(node);
        var completion = Assert.Single(
            completions.Where(record =>
                record.Kind == MeshRecordKind.Completion
                && record.OperationId == operation));
        Assert.Equal((int)RequestResult.Ok, completion.TerminalResult);
        Assert.Equal(
            ActorJoinResult.Accepted,
            Assert.IsType<ActorJoinCompletion>(completion.JoinCompletion).JoinResult);
    }

    [Fact]
    public async Task ActorRequestHasOneTerminalWinnerAcrossDuplicateReplies()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var node = NewNode(context, "request-node");
        var actor = node.CreateActor("player-3");
        DrainAndDispose(node);

        using var request = Message.From(new byte[] { 3 });
        Assert.Equal(
            SubmitResult.Ok,
            node.RequestToActor(
                actor,
                [request],
                out var operation,
                TimeSpan.FromSeconds(1)));

        using var ready = new MeshReadyBatch();
        node.DrainReady(MeshReadyDomains.Application, ready, RecvFlags.DontWait);
        using var claim = ready.TakeClaim(0);
        using var received = new MeshReceiveBatch();
        Assert.True(claim.Receive(received, RecvFlags.DontWait));
        var inbound = received[0];
        Parallel.For(
            0,
            32,
            _ => Assert.Equal(
                SubmitResult.Ok,
                inbound.Reply(Array.Empty<Message>())));

        var completions = DrainRecords(node);
        Assert.Single(
            completions.Where(record =>
                record.Kind == MeshRecordKind.Completion
                && record.OperationId == operation));
    }

    [Fact]
    public async Task RequestOperationCapacityBackpressuresBeforeAllocatingMoreWork()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var node = new ZLinkManagedMeshNode(
            context,
            "mesh",
            maxPendingOperations: 1);
        node.SetRoutingId(RoutingId.From("capacity-node"));
        var actor = node.CreateActor("capacity-actor");
        DrainAndDispose(node);

        using var request = Message.From(new byte[] { 4 });
        Assert.Equal(
            SubmitResult.Ok,
            node.RequestToActor(
                actor,
                [request],
                out var first,
                TimeSpan.FromSeconds(30)));
        Assert.NotEqual(default, first);

        Assert.Equal(
            SubmitResult.Backpressured,
            node.RequestToActor(
                actor,
                [request],
                out var rejected,
                TimeSpan.FromSeconds(30)));
        Assert.Equal(default, rejected);
    }

    [Fact]
    public async Task TransferFenceBlocksAdmissionUntilAbort()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var node = NewNode(context, "transfer-node");
        var actor = node.CreateActor("player-4");
        Assert.True(node.ActorLookup(actor.ActorId, out var location));
        DrainAndDispose(node);
        var prepare = new ActorTransferPrepare(
            ActorTransferRole.Source,
            new ActorTransferId(1, 2),
            actor,
            location.MembershipEpoch,
            RoutingId.From("peer"),
            0,
            16,
            4096);
        var token = node.PrepareActorTransfer(
            prepare,
            out var result,
            TimeSpan.FromSeconds(1));
        Assert.Equal(prepare.TransferId, result.TransferId);

        using var payload = Message.From(new byte[] { 5 });
        Assert.Equal(
            SubmitResult.Backpressured,
            node.SendToActor(actor, [payload]));
        node.AbortActorTransfer(token);
        Assert.Equal(SubmitResult.Ok, node.SendToActor(actor, [payload]));
    }

    [Fact]
    public async Task SessionBindingUsesExactActorGenerationAndRelaysToActorTurn()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var node = NewNode(context, "session-node");
        var actor = node.CreateActor("player-5");
        DrainAndDispose(node);
        await using var stream = context.CreateStreamSocket();
        await using var sessions = node.CreateStreamSessionService(stream);
        sessions.Start();
        var sessionRid = RoutingId.From("session-1");

        Assert.Equal(
            SubmitResult.Ok,
            sessions.BindActor(
                sessionRid,
                actor,
                out var bindOperation,
                TimeSpan.FromSeconds(1)));
        Assert.NotEqual(default, bindOperation);
        var binding = Assert.Single(sessions.Bindings(sessionRid));
        Assert.Equal(actor, binding.Actor);
        DrainAndDispose(node);

        using var payload = Message.From(new byte[] { 6 });
        Assert.Equal(
            SubmitResult.Ok,
            sessions.SendToActor(sessionRid, actor, [payload]));
        using var ready = new MeshReadyBatch();
        node.DrainReady(MeshReadyDomains.Application, ready, RecvFlags.DontWait);
        using var claim = ready.TakeClaim(0);
        using var received = new MeshReceiveBatch();
        Assert.True(claim.Receive(received, RecvFlags.DontWait));
        Assert.Equal(MeshRecordKind.ActorSend, received[0].Kind);

        var stale = new ActorRef(actor.NodeRid, actor.ActorId, actor.Generation + 1);
        Assert.Equal(
            SubmitResult.NotFound,
            sessions.SendToActor(sessionRid, stale, [payload]));
    }

    [Fact]
    public async Task InstanceStyleSpotKeepsOneGenerationAndRejectsStaleFence()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var node = NewNode(context, "instance-node");
        var spotRid = RoutingId.From("instance-cart-1");
        var first = node.GetOrCreateSpot(spotRid, out var firstCreated);
        var second = node.GetOrCreateSpot(spotRid, out var secondCreated);
        Assert.True(firstCreated);
        Assert.False(secondCreated);
        Assert.Equal(first.LifecycleGeneration, second.LifecycleGeneration);

        using var payload = Message.From(new byte[] { 7 });
        Assert.Equal(
            SubmitResult.InvalidState,
            first.SendToSpot(
                node.RoutingId,
                spotRid,
                first.LifecycleGeneration + 1,
                [payload]));
        Assert.Equal(
            SubmitResult.Ok,
            first.SendToSpot(
                node.RoutingId,
                spotRid,
                first.LifecycleGeneration,
                [payload]));
        DrainAndDispose(node);
        await first.DisposeAsync();
        var reactivated = node.GetOrCreateSpot(spotRid, out var reactivatedCreated);
        Assert.True(reactivatedCreated);
        Assert.NotEqual(
            first.LifecycleGeneration,
            reactivated.LifecycleGeneration);
    }

    [Fact]
    public async Task RemoteSpotAndActorDispatchPreserveExactOwnerMailbox()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "stateful-source");
        await using var target = NewNode(context, "stateful-target");
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://stateful-source-{suffix}";
        var targetEndpoint = $"inproc://stateful-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        source.Start();
        target.Start();
        await WaitUntilAsync(
            () => source.Status().AdmittedPeerCount == 1
                  && target.Status().AdmittedPeerCount == 1);

        var actor = target.CreateActor("remote-player");
        var spotRid = RoutingId.From("remote-room");
        var spot = target.GetOrCreateSpot(spotRid, out _);
        DrainAndDispose(target);

        using var payload = Message.From(new byte[] { 8 });
        Assert.Equal(SubmitResult.Ok, source.SendToActor(actor, [payload]));
        Assert.Equal(
            SubmitResult.Ok,
            source.EntrySpot().SendToSpot(
                target.RoutingId,
                spotRid,
                spot.LifecycleGeneration,
                [payload]));

        await WaitUntilAsync(() =>
        {
            using var ready = new MeshReadyBatch();
            target.DrainReady(
                MeshReadyDomains.Application,
                ready,
                RecvFlags.DontWait);
            return ready.Count == 2;
        });
        using var finalReady = new MeshReadyBatch();
        target.DrainReady(
            MeshReadyDomains.Application,
            finalReady,
            RecvFlags.DontWait);
        Assert.Contains(
            Enumerable.Range(0, finalReady.Count),
            index => finalReady[index].OwnerKind == MeshOwnerKind.Actor);
        Assert.Contains(
            Enumerable.Range(0, finalReady.Count),
            index => finalReady[index].SpotRid == spotRid);
        finalReady.Reset();
        DrainAndDispose(target);

        Assert.Equal(
            SubmitResult.Ok,
            source.RequestToActor(
                actor,
                [payload],
                out var operation,
                TimeSpan.FromSeconds(1)));
        await WaitUntilAsync(() =>
        {
            using var ready = new MeshReadyBatch();
            target.DrainReady(
                MeshReadyDomains.Application,
                ready,
                RecvFlags.DontWait);
            return ready.Count == 1;
        });
        using (var requestReady = new MeshReadyBatch())
        {
            target.DrainReady(
                MeshReadyDomains.Application,
                requestReady,
                RecvFlags.DontWait);
            using var requestClaim = requestReady.TakeClaim(0);
            using var requestBatch = new MeshReceiveBatch();
            Assert.True(requestClaim.Receive(requestBatch, RecvFlags.DontWait));
            Assert.Equal(MeshRecordKind.ActorRequest, requestBatch[0].Kind);
            Assert.Equal(
                SubmitResult.Ok,
                requestBatch[0].Reply(Array.Empty<Message>()));
        }
        await WaitUntilAsync(() =>
            source.Status().PendingInfrastructureMessages > 0);
        var completions = DrainRecords(source);
        Assert.Single(
            completions.Where(record =>
                record.Kind == MeshRecordKind.Completion
                && record.OperationId == operation));
    }

    private static ZLinkManagedMeshNode NewNode(IContext context, string rid)
    {
        var node = new ZLinkManagedMeshNode(context, "mesh");
        node.SetRoutingId(RoutingId.From(rid));
        return node;
    }

    private static List<MeshReceiveRecord> DrainRecords(ZLinkManagedMeshNode node)
    {
        var records = new List<MeshReceiveRecord>();
        using var ready = new MeshReadyBatch();
        node.DrainReady(MeshReadyDomains.All, ready, RecvFlags.DontWait);
        for (var index = 0; index < ready.Count; index++)
        {
            using var claim = ready.TakeClaim(index);
            using var received = new MeshReceiveBatch();
            while (claim.Receive(received, RecvFlags.DontWait))
            {
                for (var record = 0; record < received.Count; record++)
                    records.Add(received[record]);
                received.Reset();
            }
        }
        return records;
    }

    private static void DrainAndDispose(ZLinkManagedMeshNode node) =>
        _ = DrainRecords(node);

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        var deadline = Stopwatch.GetTimestamp() + 5 * Stopwatch.Frequency;
        while (!condition())
        {
            if (Stopwatch.GetTimestamp() >= deadline)
                throw new TimeoutException("The stateful runtime condition was not reached.");
            await Task.Delay(10);
        }
    }
}
