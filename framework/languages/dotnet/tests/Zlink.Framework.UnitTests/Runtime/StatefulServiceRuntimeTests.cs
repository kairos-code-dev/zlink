using System.Diagnostics;
using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Framework.Runtime.Protocol;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class StatefulServiceRuntimeTests
{
    [Fact]
    public void RelocationApplicationPayloadEnvelopePreservesContentTypeAndPayload()
    {
        var encoded = ZLinkApplicationPayloadEnvelopeCodec.Encode(
            "create",
            "application/x-zlink-test",
            [1, 2, 3, 4]);

        Assert.True(ZLinkApplicationPayloadEnvelopeCodec.TryDecode(
            encoded,
            out var decoded));
        Assert.Equal("create", decoded.PacketName);
        Assert.Equal("application/x-zlink-test", decoded.ContentType);
        Assert.Equal([1, 2, 3, 4], decoded.Payload.ToArray());

        var reference = ZLinkInlineCreationIntentCodec.Encode(encoded);
        Assert.StartsWith("inline-v1:", reference, StringComparison.Ordinal);
        Assert.True(ZLinkInlineCreationIntentCodec.TryDecode(
            reference,
            out var restored));
        Assert.Equal(encoded, restored);
        Assert.False(ZLinkInlineCreationIntentCodec.TryDecode(
            reference[..^1] + (reference[^1] == 'A' ? "B" : "A"),
            out _));
    }

    [Fact]
    public void InstanceSpotColdActivationWirePreservesDescriptorPlacementAndDeadline()
    {
        var operation = new InstanceSpotActivationOperation(
            new InstanceSpotActivationTarget(
                "target-mesh",
                RoutingId.From("target-node"),
                17,
                RoutingId.From("instance-spot"),
                "Sample.InstanceSpot",
                "descriptor-23",
                "latency",
                "tenant-7"),
            RoutingId.From("source-node"),
            29,
            RoutingId.From("source-spot"),
            new MeshOperationId(31, 37),
            IsRequest: true,
            ReplyRouteId: 41,
            DeadlineUnixMs: 4_102_444_800_000);

        var encoded = ZLinkServiceWireCodec.EncodeInstanceSpotActivation(
            operation,
            hasMetadata: true);

        Assert.True(ZLinkServiceWireCodec.TryDecodeInstanceSpotActivation(
            encoded,
            out var decoded,
            out var error));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, error);
        Assert.True(decoded.HasMetadata);
        Assert.Equal(operation, decoded.Operation);

        var invalidRouteKind = encoded.ToArray();
        invalidRouteKind[5] = 3;
        Assert.False(ZLinkServiceWireCodec.TryDecodeInstanceSpotActivation(
            invalidRouteKind,
            out _,
            out var routeKindError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.InvalidField, routeKindError);

        Assert.False(ZLinkServiceWireCodec.TryDecodeInstanceSpotActivation(
            [.. encoded, 0],
            out _,
            out var trailingError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.TrailingByte, trailingError);
    }

    [Fact]
    public void UserSpotOperationWireRoundTripsExactReservationCloseAndReplyTails()
    {
        var sourceRid = RoutingId.From("create-source");
        var targetRid = RoutingId.From("create-target");
        var spotRid = RoutingId.From("created-spot");
        var reservation = new UserSpotReservationFence(
            "reservation-1",
            "store-17",
            19,
            23,
            targetRid,
            29,
            "owner-b",
            31,
            1);
        var create = new UserSpotCreateOperation(
            37,
            new MeshOperationId(41, 43),
            sourceRid,
            47,
            spotRid,
            "Sample.UserSpot",
            reservation,
            4_102_444_800_000);

        var encodedCreate = ZLinkServiceWireCodec.EncodeUserSpotCreate(create);
        Assert.True(ZLinkServiceWireCodec.TryDecodeUserSpotOperation(
            encodedCreate,
            out var decodedCreate,
            out var createError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, createError);
        Assert.Equal(ServiceWireConstants.Command.UserSpotCreate, decodedCreate.Command);
        Assert.Equal(create, decodedCreate.Create);

        var close = new UserSpotCloseOperation(
            53,
            new MeshOperationId(59, 61),
            sourceRid,
            47,
            new UserSpotCloseFence(
                spotRid,
                19,
                targetRid,
                29,
                23,
                "store-18"),
            4_102_444_800_000);
        var encodedClose = ZLinkServiceWireCodec.EncodeUserSpotClose(close);
        Assert.True(ZLinkServiceWireCodec.TryDecodeUserSpotOperation(
            encodedClose,
            out var decodedClose,
            out var closeError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, closeError);
        Assert.Equal(ServiceWireConstants.Command.UserSpotClose, decodedClose.Command);
        Assert.Equal(close, decodedClose.Close);

        var createReply = ZLinkServiceWireCodec.EncodeUserSpotCreateReply(
            37,
            RequestResult.Ok,
            ServiceWireConstants.FrameworkErrorCode.None,
            new UserSpotCreateCompletion(
                UserSpotCreateResult.Created,
                spotRid,
                19));
        Assert.True(ZLinkServiceWireCodec.TryDecodeReply(
            createReply,
            out var decodedCreateReply,
            out var createReplyError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, createReplyError);
        Assert.True(ZLinkServiceWireCodec.TryDecodeUserSpotReply(
            decodedCreateReply,
            MeshOperationKind.UserSpotCreate,
            out var createCompletion,
            out var createTailError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, createTailError);
        Assert.Equal(
            new UserSpotCreateCompletion(UserSpotCreateResult.Created, spotRid, 19),
            createCompletion);

        var closeReply = ZLinkServiceWireCodec.EncodeUserSpotCloseReply(
            53,
            RequestResult.Ok,
            ServiceWireConstants.FrameworkErrorCode.None,
            new UserSpotCloseCompletion(true));
        Assert.True(ZLinkServiceWireCodec.TryDecodeReply(
            closeReply,
            out var decodedCloseReply,
            out var closeReplyError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, closeReplyError);
        Assert.True(ZLinkServiceWireCodec.TryDecodeUserSpotReply(
            decodedCloseReply,
            MeshOperationKind.UserSpotClose,
            out var closeCompletion,
            out var closeTailError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, closeTailError);
        Assert.Equal(new UserSpotCloseCompletion(true), closeCompletion);

        Assert.False(ZLinkServiceWireCodec.TryDecodeUserSpotOperation(
            [.. encodedCreate, 0],
            out _,
            out var trailingError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.TrailingByte, trailingError);
        Assert.False(ZLinkServiceWireCodec.TryDecodeUserSpotOperation(
            encodedClose.AsSpan(0, encodedClose.Length - 1),
            out _,
            out var truncatedError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.TruncatedField, truncatedError);
    }

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
        var spot = (ZLinkManagedSpot)target.GetOrCreateSpot(spotRid, out _);
        DrainAndDispose(target);

        using var payload = Message.From(new byte[] { 8 });
        Assert.True(target.TryGetActorAuthority(actor, out var actorAuthority));
        source.ObserveActorAuthority(actor, actorAuthority);
        source.ObserveSpotAuthority(
            target.RoutingId,
            spotRid,
            spot.LifecycleGeneration,
            spot.AuthorityOwnerGeneration);
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

    [Fact]
    public async Task FrameworkHostAutomaticallyExecutesRemoteUserSpotCreateAndCloseAgainstAuthorityStore()
    {
        ProductionUserSpot.Reset();
        var suffix = Guid.NewGuid().ToString("N");
        var targetRid = RoutingId.From($"production-target-{suffix}");
        var sourceRid = RoutingId.From($"production-source-{suffix}");
        var targetEndpoint = $"tcp://127.0.0.1:{FindFreeTcpPort()}";
        var sourceEndpoint = $"tcp://127.0.0.1:{FindFreeTcpPort()}";
        const string stableType = "Tests.ProductionUserSpot";
        var relocationStore = new TestRelocationStore();

        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRelocationStore(relocationStore);
            var node = options.AddRouteMesh("objects")
                .Listen(targetEndpoint)
                .SetRoutingId(targetRid);
            node.ChannelName("objects");
            node.Objects().Server().AddSpotFactory<ProductionUserSpot>(
                stableType,
                null,
                ZLinkRelocationPolicy<ProductionUserSpot>.Disabled);
        });

        await using var provider = services.BuildServiceProvider();
        var runtime = provider.GetRequiredService<ZLinkFrameworkRuntime>();
        await runtime.StartAsync(CancellationToken.None);
        try
        {
            var target = runtime.GetSpotNodeRuntime("objects");
            await using var sourceContext = Systems.Zlink.Zlink.CreateContext();
            await using var source = new ZLinkManagedMeshNode(sourceContext, "objects");
            source.SetRoutingId(sourceRid);
            source.SetBind(sourceEndpoint);
            source.ConnectPeer(targetEndpoint, targetRid);
            target.Node.ConnectPeer(sourceRid, sourceEndpoint);
            source.Start();
            await WaitUntilAsync(() =>
                source.Status().AdmittedPeerCount == 1
                && target.Node.MeshStatus().AdmittedPeerCount == 1);

            var store = Assert.IsAssignableFrom<IZLinkLocationStore>(
                runtime.Registration.Locations.ResolveStore());
            var owner = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
                await store.ClaimOwnerLeaseAsync(
                    $"production-owner-{suffix}",
                    TimeSpan.FromMinutes(1)));
            var targetGeneration = target.Node.MeshStatus().LifecycleGeneration;
            var descriptorKey = new ZLinkMeshNodeDescriptorKey("objects", targetRid);
            var descriptor = new ZLinkMeshNodeDescriptor(
                "objects",
                targetRid,
                targetGeneration,
                1,
                targetEndpoint,
                new Dictionary<string, int>(StringComparer.Ordinal)
                {
                    ["objects"] = 100
                },
                string.Empty,
                owner.Token.OwnerId,
                owner.Token.LeaseGeneration,
                DateTimeOffset.UtcNow)
            {
                ObjectRole = ZLinkMeshNodeObjectRole.Server,
                State = ZLinkFrameworkRuntimeState.Serving,
                ObjectCapabilities =
                [
                    new ZLinkObjectCapability(
                        ZLinkPlacementObjectKind.UserSpot,
                        stableType,
                        ZLinkObjectMaintenancePolicyKind.Disabled,
                        false,
                        new HashSet<string>(StringComparer.Ordinal),
                        null,
                        null)
                ]
            };
            Assert.Equal(
                ZLinkLocationWriteStatus.Stored,
                (await store.UpdateMeshNodeAsync(
                    descriptor,
                    ZLinkLocationWriteIntent.NewClaim)).Status);

            var spotRid = RoutingId.From($"production-spot-{suffix}");
            var authorityKey = ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(spotRid);
            var creationPayload = ZLinkApplicationPayloadEnvelopeCodec.Encode(
                string.Empty,
                ZLinkEnvelopeCodec.DefaultContentType,
                "{}"u8);
            var creationReference =
                ZLinkInlineCreationIntentCodec.Encode(creationPayload);
            var reservation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
                await store.ReserveAsync(
                    new ZLinkObjectReservationRequest(
                        ZLinkPlacementObjectKind.UserSpot,
                        authorityKey,
                        stableType,
                        null,
                        null,
                        creationReference,
                        System.Security.Cryptography.SHA256.HashData(creationPayload),
                        creationPayload.Length,
                        descriptorKey,
                        targetGeneration,
                        owner.Token,
                        ZLinkUserSpotAuthorityPayloadCodec.Encode(
                            new ZLinkUserSpotAuthorityPayload(
                                ZLinkUserSpotAuthorityState.Creating,
                                stableType,
                                spotRid,
                                owner.Token.OwnerId,
                                checked((ulong)owner.Token.LeaseGeneration),
                                "objects",
                                targetRid,
                                targetGeneration)),
                        1)));
            var fence = new UserSpotReservationFence(
                reservation.Reservation.ReservationVersion,
                reservation.Reservation.StoreVersion,
                reservation.Reservation.ObjectGeneration,
                reservation.Reservation.AuthorityOwnerGeneration,
                targetRid,
                targetGeneration,
                owner.Token.OwnerId,
                checked((ulong)owner.Token.LeaseGeneration),
                1);
            var deadline = checked(
                (ulong)DateTimeOffset.UtcNow.AddSeconds(5).ToUnixTimeMilliseconds());

            Assert.Equal(
                SubmitResult.Ok,
                source.CreateUserSpot(
                    targetRid,
                    spotRid,
                    stableType,
                    fence,
                    deadline,
                    out var createOperation,
                    TimeSpan.FromSeconds(3)));
            await WaitUntilAsync(() =>
                source.Status().PendingInfrastructureMessages > 0);
            var (createCompletion, replyParts) = DrainCompletion(
                source,
                createOperation);
            try
            {
                Assert.Equal((int)RequestResult.Ok, createCompletion.TerminalResult);
                Assert.Equal(
                    UserSpotCreateResult.Created,
                    createCompletion.UserSpotCreateCompletion?.Result);
                Assert.Equal(1, ProductionUserSpot.CreateCount);
                Assert.Equal(2, replyParts.Count);
                var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(replyParts);
                Assert.Equal(ZLinkMessageKind.Response, replyHeader.Kind);
                Assert.Equal(string.Empty, replyHeader.MessageName);
                Assert.Contains(
                    "production-created",
                    System.Text.Encoding.UTF8.GetString(
                        replyParts[1].AsReadOnlySpan()),
                    StringComparison.Ordinal);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(replyParts);
            }

            var active = Assert.IsType<ZLinkAuthorityReadResult.Found>(
                await store.ReadAuthorityAsync(authorityKey));
            var closeFence = new UserSpotCloseFence(
                spotRid,
                active.Snapshot.ObjectGeneration,
                targetRid,
                targetGeneration,
                active.Snapshot.AuthorityOwnerGeneration,
                active.Snapshot.StoreVersion);
            Assert.Equal(
                SubmitResult.Ok,
                source.CloseUserSpot(
                    targetRid,
                    closeFence,
                    deadline,
                    out var closeOperation,
                    TimeSpan.FromSeconds(3)));
            await WaitUntilAsync(() =>
                source.Status().PendingInfrastructureMessages > 0);
            var (closeCompletion, closeParts) = DrainCompletion(
                source,
                closeOperation);
            ZLinkMessageParts.DisposeAll(closeParts);
            Assert.Equal(
                new UserSpotCloseCompletion(true),
                closeCompletion.UserSpotCloseCompletion);
            Assert.Equal(1, ProductionUserSpot.CloseCount);
            Assert.IsType<ZLinkAuthorityReadResult.Missing>(
                await store.ReadAuthorityAsync(authorityKey));

            var orphanRid = RoutingId.From($"production-orphan-{suffix}");
            var orphanKey = ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(orphanRid);
            var orphanReservation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
                await store.ReserveAsync(
                    new ZLinkObjectReservationRequest(
                        ZLinkPlacementObjectKind.UserSpot,
                        orphanKey,
                        stableType,
                        null,
                        null,
                        creationReference,
                        System.Security.Cryptography.SHA256.HashData(creationPayload),
                        creationPayload.Length,
                        descriptorKey,
                        targetGeneration,
                        owner.Token,
                        ZLinkUserSpotAuthorityPayloadCodec.Encode(
                            new ZLinkUserSpotAuthorityPayload(
                                ZLinkUserSpotAuthorityState.Creating,
                                stableType,
                                orphanRid,
                                owner.Token.OwnerId,
                                checked((ulong)owner.Token.LeaseGeneration),
                                "objects",
                                targetRid,
                                targetGeneration)),
                        1)));
            var orphanActive = Assert.IsType<ZLinkObjectCommitResult.Committed>(
                await store.CommitAsync(
                    orphanReservation.Reservation,
                    ZLinkUserSpotAuthorityPayloadCodec.Encode(
                        new ZLinkUserSpotAuthorityPayload(
                            ZLinkUserSpotAuthorityState.Ready,
                            stableType,
                            orphanRid,
                            owner.Token.OwnerId,
                            checked((ulong)owner.Token.LeaseGeneration),
                            "objects",
                            targetRid,
                            targetGeneration))));
            var orphanFence = new UserSpotCloseFence(
                orphanRid,
                orphanActive.Snapshot.ObjectGeneration,
                targetRid,
                targetGeneration,
                orphanActive.Snapshot.AuthorityOwnerGeneration,
                orphanActive.Snapshot.StoreVersion);
            Assert.Equal(
                SubmitResult.Ok,
                source.CloseUserSpot(
                    targetRid,
                    orphanFence,
                    checked((ulong)DateTimeOffset.UtcNow.AddSeconds(5)
                        .ToUnixTimeMilliseconds()),
                    out var orphanCloseOperation,
                    TimeSpan.FromSeconds(3)));
            await WaitUntilAsync(() =>
                source.Status().PendingInfrastructureMessages > 0);
            var (orphanCompletion, orphanParts) = DrainCompletion(
                source,
                orphanCloseOperation);
            ZLinkMessageParts.DisposeAll(orphanParts);
            Assert.Equal(
                (int)RequestResult.Conflict,
                orphanCompletion.TerminalResult);
            Assert.Equal(
                (int)ServiceWireConstants.FrameworkErrorCode.SpotMoving,
                orphanCompletion.FailureErrno);
            Assert.Null(orphanCompletion.UserSpotCloseCompletion);
            var retainedOrphan = Assert.IsType<ZLinkAuthorityReadResult.Found>(
                await store.ReadAuthorityAsync(orphanKey));
            Assert.True(ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                retainedOrphan.Snapshot.Payload.Span,
                out var retainedPayload));
            Assert.Equal(
                ZLinkUserSpotAuthorityState.Ready,
                retainedPayload.State);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task PublicSpotManagerUsesReserveAndRemoteUserSpotCommands()
    {
        ProductionUserSpot.Reset();
        var suffix = Guid.NewGuid().ToString("N");
        var locationStore = new ZLinkInMemoryLocationStore();
        var sourceRid = RoutingId.From($"public-source-{suffix}");
        var targetRid = RoutingId.From($"public-target-{suffix}");
        var sourceEndpoint = $"tcp://127.0.0.1:{FindFreeTcpPort()}";
        var targetEndpoint = $"tcp://127.0.0.1:{FindFreeTcpPort()}";

        ServiceProvider Build(
            RoutingId rid,
            string endpoint,
            bool server,
            ZLinkMeshPeerConnection? peer = null)
        {
            var services = new ServiceCollection();
            services.AddZLinkFramework(options =>
            {
                options.AddLocationStore(locationStore);
                var node = options.AddRouteMesh("objects")
                    .Listen(endpoint)
                    .SetRoutingId(rid);
                node.ChannelName("objects");
                if (peer is { } connection)
                {
                    node.PeerConnections.Connect(
                        connection.ExpectedRoutingId!.Value,
                        connection.Endpoint);
                }
                var objects = node.Objects();
                if (server)
                {
                    objects.Server()
                        .AddSpotFactory<ProductionUserSpot>(
                            "Tests.ProductionUserSpot",
                            null,
                            ZLinkRelocationPolicy<ProductionUserSpot>.Disabled);
                }
                else
                {
                    objects.Client();
                }
            });
            return services.BuildServiceProvider();
        }

        await using var targetProvider = Build(targetRid, targetEndpoint, true);
        await using var sourceProvider = Build(
            sourceRid,
            sourceEndpoint,
            false,
            new ZLinkMeshPeerConnection(targetEndpoint, targetRid));
        var target = targetProvider.GetRequiredService<ZLinkFrameworkRuntime>();
        var source = sourceProvider.GetRequiredService<ZLinkFrameworkRuntime>();
        await target.StartAsync(CancellationToken.None);
        await source.StartAsync(CancellationToken.None);
        try
        {
            target.GetSpotNodeRuntime("objects").Node.ConnectPeer(
                sourceRid,
                sourceEndpoint);
            await WaitUntilAsync(() =>
                source.GetSpotNodeRuntime("objects").Node.MeshStatus()
                    .AdmittedPeerCount > 0);
            var owner = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
                await locationStore.ClaimOwnerLeaseAsync(
                    $"public-owner-{suffix}",
                    TimeSpan.FromMinutes(1)));
            var targetGeneration =
                target.GetSpotNodeRuntime("objects").Node.MeshStatus()
                    .LifecycleGeneration;
            Assert.Equal(
                ZLinkLocationWriteStatus.Stored,
                (await locationStore.UpdateMeshNodeAsync(
                    new ZLinkMeshNodeDescriptor(
                        "objects",
                        targetRid,
                        targetGeneration,
                        1,
                        targetEndpoint,
                        new Dictionary<string, int>(StringComparer.Ordinal)
                        {
                            ["objects"] = 100
                        },
                        string.Empty,
                        owner.Token.OwnerId,
                        owner.Token.LeaseGeneration,
                        DateTimeOffset.UtcNow)
                    {
                        ObjectRole = ZLinkMeshNodeObjectRole.Server,
                        State = ZLinkFrameworkRuntimeState.Serving,
                        ObjectCapabilities =
                        [
                            new ZLinkObjectCapability(
                                ZLinkPlacementObjectKind.UserSpot,
                                "Tests.ProductionUserSpot",
                                ZLinkObjectMaintenancePolicyKind.Disabled,
                                false,
                                new HashSet<string>(StringComparer.Ordinal),
                                null,
                                null)
                        ]
                    },
                    ZLinkLocationWriteIntent.NewClaim)).Status);
            var spotRid = RoutingId.From($"public-spot-{suffix}");
            using var operationTimeout = new CancellationTokenSource(
                TimeSpan.FromSeconds(10));
            using var customPayload = Message.From([9, 8, 7]);
            var allocated = await source
                .Create("Tests.ProductionUserSpot")
                .Request(ZLinkMessage.FromEnvelopePayload(
                    "application/x-zlink-test",
                    customPayload,
                    source.Registration.Codecs))
                .Async(operationTimeout.Token);
            Assert.Equal(ZLinkSpotCreateState.Created, allocated.State);
            Assert.Equal(
                "application/x-zlink-test",
                ProductionUserSpot.LastCreateContentType);
            Assert.Equal(allocated.Spot, await source.FindAsync(allocated.Spot.SpotRid));
            Assert.NotNull(await target.FindAsync(allocated.Spot.SpotRid));
            Assert.True(await source.CloseAsync(
                allocated.Spot,
                operationTimeout.Token));
            Assert.Null(await target.FindAsync(allocated.Spot.SpotRid));

            var created = await source
                .GetOrCreate(spotRid, "Tests.ProductionUserSpot")
                .Request(new { Name = "created" })
                .Async(operationTimeout.Token);
            Assert.Equal(ZLinkSpotCreateState.Created, created.State);
            Assert.Equal(created.Spot, await source.FindAsync(spotRid));
            Assert.NotNull(await target.FindAsync(spotRid));
            var existing = await source
                .GetOrCreate(spotRid, "Tests.ProductionUserSpot")
                .Async(operationTimeout.Token);
            Assert.Equal(ZLinkSpotCreateState.Existing, existing.State);
            Assert.Equal(created.Spot, existing.Spot);
            Assert.True(await source.CloseAsync(
                created.Spot,
                operationTimeout.Token));
            Assert.Null(await target.FindAsync(spotRid));

            var joiningRid = RoutingId.From($"joining-spot-{suffix}");
            var createGate = ProductionUserSpot.BlockNextCreate();
            var ownerCreate = source
                .GetOrCreate(joiningRid, "Tests.ProductionUserSpot")
                .Async(operationTimeout.Token)
                .AsTask();
            await createGate.Entered.Task.WaitAsync(operationTimeout.Token);
            var joiningPending = Assert.IsType<ZLinkAuthorityReadResult.Found>(
                await locationStore.ReadAuthorityAsync(
                    ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(joiningRid),
                    operationTimeout.Token));
            Assert.Equal(
                ZLinkPlacementAllocationState.Pending,
                joiningPending.Snapshot.Allocation.State);
            Assert.StartsWith(
                "inline-v1:",
                joiningPending.Snapshot.PendingCreation!
                    .RequestContentReference,
                StringComparison.Ordinal);
            var joinedCreate = source
                .GetOrCreate(joiningRid, "Tests.ProductionUserSpot")
                .Async(operationTimeout.Token)
                .AsTask();
            Assert.False(joinedCreate.IsCompleted);
            createGate.Release.TrySetResult();
            var ownerResult = await ownerCreate;
            var joinedResult = await joinedCreate;
            Assert.Equal(ZLinkSpotCreateState.Created, ownerResult.State);
            Assert.Equal(ZLinkSpotCreateState.Existing, joinedResult.State);
            Assert.Equal(ownerResult.Spot, joinedResult.Spot);
            Assert.True(await source.CloseAsync(
                ownerResult.Spot,
                operationTimeout.Token));
        }
        finally
        {
            await source.StopAsync(CancellationToken.None);
            await target.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task RemoteUserSpotCreateAndCloseUseGenerationFencedTerminalOperations()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "user-spot-source");
        await using var target = NewNode(context, "user-spot-target");
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://user-spot-source-{suffix}";
        var targetEndpoint = $"inproc://user-spot-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        var operationTarget = new RecordingUserSpotOperationTarget();
        target.SetUserSpotOperationTarget(operationTarget);
        source.Start();
        target.Start();
        await WaitUntilAsync(
            () => source.Status().AdmittedPeerCount == 1
                  && target.Status().AdmittedPeerCount == 1);

        var sourceGeneration = source.Status().LifecycleGeneration;
        var targetGeneration = target.Status().LifecycleGeneration;
        var spotRid = RoutingId.From("remote-created-spot");
        var reservation = new UserSpotReservationFence(
            "reservation-runtime",
            "store-runtime-1",
            71,
            73,
            target.RoutingId,
            targetGeneration,
            "target-owner",
            79,
            1);
        var deadline = checked(
            (ulong)DateTimeOffset.UtcNow.AddSeconds(5).ToUnixTimeMilliseconds());

        Assert.Equal(
            SubmitResult.Ok,
            source.CreateUserSpot(
                target.RoutingId,
                spotRid,
                "Sample.RemoteSpot",
                reservation,
                deadline,
                out var createOperation,
                TimeSpan.FromSeconds(3)));
        await WaitUntilAsync(() => source.Status().PendingInfrastructureMessages > 0);
        var createRecords = DrainRecords(source);
        var createCompletion = Assert.Single(createRecords.Where(record =>
            record.Kind == MeshRecordKind.Completion
            && record.OperationId == createOperation));
        Assert.Equal(MeshOperationKind.UserSpotCreate, createCompletion.OperationKind);
        Assert.Equal((int)RequestResult.Ok, createCompletion.TerminalResult);
        Assert.Equal(
            new UserSpotCreateCompletion(
                UserSpotCreateResult.Created,
                spotRid,
                reservation.ObjectGeneration),
            createCompletion.UserSpotCreateCompletion);
        Assert.Equal(1, createCompletion.PartCount);
        Assert.Equal(1, operationTarget.CreateCount);
        Assert.Equal(source.RoutingId, operationTarget.LastCreate.SourceNodeRid);
        Assert.Equal(sourceGeneration, operationTarget.LastCreate.SourceNodeGeneration);
        Assert.Equal(reservation, operationTarget.LastCreate.Reservation);

        var closeFence = new UserSpotCloseFence(
            spotRid,
            reservation.ObjectGeneration,
            target.RoutingId,
            targetGeneration,
            reservation.AuthorityOwnerGeneration,
            "store-runtime-2");
        Assert.Equal(
            SubmitResult.Ok,
            source.CloseUserSpot(
                target.RoutingId,
                closeFence,
                deadline,
                out var closeOperation,
                TimeSpan.FromSeconds(3)));
        await WaitUntilAsync(() => source.Status().PendingInfrastructureMessages > 0);
        var closeRecords = DrainRecords(source);
        var closeCompletion = Assert.Single(closeRecords.Where(record =>
            record.Kind == MeshRecordKind.Completion
            && record.OperationId == closeOperation));
        Assert.Equal(MeshOperationKind.UserSpotClose, closeCompletion.OperationKind);
        Assert.Equal((int)RequestResult.Ok, closeCompletion.TerminalResult);
        Assert.Equal(new UserSpotCloseCompletion(true), closeCompletion.UserSpotCloseCompletion);
        Assert.Equal(1, operationTarget.CloseCount);
        Assert.Equal(closeFence, operationTarget.LastClose.Target);

        operationTarget.CloseError = new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotMoving,
            "The User Spot is sealed for relocation.",
            true);
        Assert.Equal(
            SubmitResult.Ok,
            source.CloseUserSpot(
                target.RoutingId,
                closeFence,
                deadline,
                out var movingOperation,
                TimeSpan.FromSeconds(3)));
        await WaitUntilAsync(() => source.Status().PendingInfrastructureMessages > 0);
        var movingRecords = DrainRecords(source);
        var movingCompletion = Assert.Single(movingRecords.Where(record =>
            record.Kind == MeshRecordKind.Completion
            && record.OperationId == movingOperation));
        Assert.Equal((int)RequestResult.Conflict, movingCompletion.TerminalResult);
        Assert.Equal(
            (int)ServiceWireConstants.FrameworkErrorCode.SpotMoving,
            movingCompletion.FailureErrno);
        Assert.Null(movingCompletion.UserSpotCloseCompletion);
        Assert.Equal(2, operationTarget.CloseCount);

        Assert.Equal(
            SubmitResult.NotConnected,
            source.CloseUserSpot(
                target.RoutingId,
                closeFence with { TargetNodeGeneration = targetGeneration + 1 },
                deadline,
                out var staleOperation,
                TimeSpan.FromSeconds(1)));
        Assert.Equal(default, staleOperation);
        Assert.Equal(2, operationTarget.CloseCount);
    }

    [Fact]
    public async Task RemoteInstanceSpotColdActivationDispatchesFirstMessageThroughCommand39()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "instance-source");
        await using var target = NewNode(context, "instance-target");
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://instance-source-{suffix}";
        var targetEndpoint = $"inproc://instance-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        var activationTarget = new RecordingInstanceSpotActivationTarget();
        target.SetInstanceSpotActivationTarget(activationTarget);
        source.Start();
        target.Start();
        await WaitUntilAsync(
            () => source.Status().AdmittedPeerCount == 1
                  && target.Status().AdmittedPeerCount == 1);

        using var firstMessage = Message.From([1, 2, 3]);
        var activation = new InstanceSpotActivationTarget(
            "objects",
            target.RoutingId,
            target.Status().LifecycleGeneration,
            RoutingId.From("cold-instance"),
            "Sample.InstanceSpot",
            "descriptor-1",
            "latency",
            "tenant-7");
        var deadline = checked(
            (ulong)DateTimeOffset.UtcNow.AddSeconds(5).ToUnixTimeMilliseconds());

        Assert.Equal(
            SubmitResult.Ok,
            source.ActivateInstanceSpot(
                activation,
                RoutingId.From("caller-spot"),
                [firstMessage],
                request: true,
                out var operationId,
                deadline,
                TimeSpan.FromSeconds(3),
                metadata: new byte[] { 9, 8 }));

        await WaitUntilAsync(() =>
            source.Status().PendingInfrastructureMessages > 0);
        var completion = DrainCompletion(source, operationId);
        try
        {
            Assert.Equal(MeshOperationKind.InstanceSpotRequest, completion.Record.OperationKind);
            Assert.Equal((int)RequestResult.Ok, completion.Record.TerminalResult);
            Assert.Equal([7, 6], completion.Parts.Single().ToArray());
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(completion.Parts);
        }

        Assert.Equal(1, activationTarget.Count);
        Assert.Equal(activation, activationTarget.LastOperation.Target);
        Assert.Equal([9, 8], activationTarget.LastMetadata.ToArray());
        Assert.Equal([1, 2, 3], activationTarget.LastPayload.Single().ToArray());
    }

    [Fact]
    public async Task RemoteUserSpotTerminalReplaysAfterDeadlineAndExpiresWithoutReexecution()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "retention-source");
        await using var target = NewNode(
            context,
            "retention-target",
            TimeSpan.FromMilliseconds(500));
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://retention-source-{suffix}";
        var targetEndpoint = $"inproc://retention-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        var operationTarget = new RecordingUserSpotOperationTarget();
        target.SetUserSpotOperationTarget(operationTarget);
        source.Start();
        target.Start();
        await WaitUntilAsync(
            () => source.Status().AdmittedPeerCount == 1
                  && target.Status().AdmittedPeerCount == 1);

        var targetGeneration = target.Status().LifecycleGeneration;
        var reservation = new UserSpotReservationFence(
            "retention-reservation",
            "retention-store",
            101,
            103,
            target.RoutingId,
            targetGeneration,
            "retention-owner",
            107,
            1);
        var deadline = checked(
            (ulong)DateTimeOffset.UtcNow.AddMilliseconds(500)
                .ToUnixTimeMilliseconds());
        var spotRid = RoutingId.From("retention-spot");
        Assert.Equal(
            SubmitResult.Ok,
            source.CreateUserSpot(
                target.RoutingId,
                spotRid,
                "Sample.RetentionSpot",
                reservation,
                deadline,
                out var operationId,
                TimeSpan.FromSeconds(2)));
        await WaitUntilAsync(() =>
            source.Status().PendingInfrastructureMessages > 0);
        _ = DrainRecords(source);
        Assert.Equal(1, operationTarget.CreateCount);
        Assert.Equal(1, target.RetainedUserSpotOperationCount);

        var replay = new ZLinkServiceWireCodec.UserSpotOperationRecord(
            ServiceWireConstants.Command.UserSpotCreate,
            new UserSpotCreateOperation(
                999,
                operationId,
                source.RoutingId,
                source.Status().LifecycleGeneration,
                spotRid,
                "Sample.RetentionSpot",
                reservation,
                deadline),
            default);
        var afterDeadline = DateTimeOffset.FromUnixTimeMilliseconds(
            checked((long)deadline)).AddMilliseconds(25);
        var wait = afterDeadline - DateTimeOffset.UtcNow;
        if (wait > TimeSpan.Zero)
            await Task.Delay(wait);
        Assert.Equal(
            SubmitResult.Ok,
            source.ResubmitUserSpotOperation(target.RoutingId, replay));
        await Task.Delay(50);
        Assert.Equal(1, operationTarget.CreateCount);
        Assert.Equal(1, target.RetainedUserSpotOperationCount);

        await WaitUntilAsync(() => target.RetainedUserSpotOperationCount == 0);
        Assert.Equal(
            SubmitResult.Ok,
            source.ResubmitUserSpotOperation(target.RoutingId, replay));
        await Task.Delay(50);
        Assert.Equal(1, operationTarget.CreateCount);
        Assert.Equal(0, target.RetainedUserSpotOperationCount);
    }

    private static ZLinkManagedMeshNode NewNode(
        IContext context,
        string rid,
        TimeSpan? remoteUserSpotTerminalRetention = null)
    {
        var node = new ZLinkManagedMeshNode(
            context,
            "mesh",
            remoteUserSpotTerminalRetention:
                remoteUserSpotTerminalRetention);
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

    private static (MeshReceiveRecord Record, IReadOnlyList<Message> Parts)
        DrainCompletion(
            ZLinkManagedMeshNode node,
            MeshOperationId operationId)
    {
        using var ready = new MeshReadyBatch();
        node.DrainReady(MeshReadyDomains.All, ready, RecvFlags.DontWait);
        for (var index = 0; index < ready.Count; index++)
        {
            using var claim = ready.TakeClaim(index);
            using var received = new MeshReceiveBatch();
            while (claim.Receive(received, RecvFlags.DontWait))
            {
                for (var record = 0; record < received.Count; record++)
                {
                    var value = received[record];
                    if (value.Kind != MeshRecordKind.Completion
                        || value.OperationId != operationId)
                        continue;
                    return (value, received.RetainMessage(record));
                }
                received.Reset();
            }
        }
        throw new InvalidOperationException(
            $"Completion '{operationId}' was not queued.");
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

    private static int FindFreeTcpPort()
    {
        using var listener = new System.Net.Sockets.TcpListener(
            System.Net.IPAddress.Loopback,
            0);
        listener.Start();
        return ((System.Net.IPEndPoint)listener.LocalEndpoint).Port;
    }

    private sealed class RecordingUserSpotOperationTarget : IUserSpotOperationTarget
    {
        private int _createCount;
        private int _closeCount;

        internal int CreateCount => Volatile.Read(ref _createCount);
        internal int CloseCount => Volatile.Read(ref _closeCount);
        internal UserSpotCreateOperation LastCreate { get; private set; }
        internal UserSpotCloseOperation LastClose { get; private set; }
        internal Exception? CloseError { get; set; }

        public ValueTask<UserSpotOperationTerminal> CreateAsync(
            UserSpotCreateOperation operation,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            LastCreate = operation;
            Interlocked.Increment(ref _createCount);
            return ValueTask.FromResult(new UserSpotOperationTerminal(
                RequestResult.Ok,
                ServiceWireConstants.FrameworkErrorCode.None,
                new UserSpotCreateCompletion(
                    UserSpotCreateResult.Created,
                    operation.SpotRid,
                    operation.Reservation.ObjectGeneration),
                [new byte[] { 0x51 }]));
        }

        public ValueTask<UserSpotOperationTerminal> CloseAsync(
            UserSpotCloseOperation operation,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            LastClose = operation;
            Interlocked.Increment(ref _closeCount);
            if (CloseError is { } error)
                throw error;
            return ValueTask.FromResult(new UserSpotOperationTerminal(
                RequestResult.Ok,
                ServiceWireConstants.FrameworkErrorCode.None,
                new UserSpotCloseCompletion(true)));
        }
    }

    private sealed class RecordingInstanceSpotActivationTarget
        : IInstanceSpotActivationTarget
    {
        private int _count;

        internal int Count => Volatile.Read(ref _count);
        internal InstanceSpotActivationOperation LastOperation { get; private set; }
        internal ReadOnlyMemory<byte> LastMetadata { get; private set; }
        internal IReadOnlyList<ReadOnlyMemory<byte>> LastPayload { get; private set; } =
            Array.Empty<ReadOnlyMemory<byte>>();

        public ValueTask<InstanceSpotActivationTerminal> ActivateAsync(
            InstanceSpotActivationOperation operation,
            ReadOnlyMemory<byte>? metadata,
            IReadOnlyList<ReadOnlyMemory<byte>> payload,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            LastOperation = operation;
            LastMetadata = metadata ?? ReadOnlyMemory<byte>.Empty;
            LastPayload = payload;
            Interlocked.Increment(ref _count);
            return ValueTask.FromResult(new InstanceSpotActivationTerminal(
                RequestResult.Ok,
                ServiceWireConstants.FrameworkErrorCode.None,
                [new byte[] { 7, 6 }]));
        }
    }

    private sealed class ProductionUserSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        private static int _createCount;
        private static int _closeCount;
        private static string? _lastCreateContentType;
        private static CreateGate? _nextCreateGate;

        public IZLinkSpotContext Context { get; } = context;
        internal static int CreateCount => Volatile.Read(ref _createCount);
        internal static int CloseCount => Volatile.Read(ref _closeCount);
        internal static string? LastCreateContentType =>
            Volatile.Read(ref _lastCreateContentType);

        internal static void Reset()
        {
            Volatile.Write(ref _createCount, 0);
            Volatile.Write(ref _closeCount, 0);
            Volatile.Write(ref _lastCreateContentType, null);
            Volatile.Write(ref _nextCreateGate, null);
        }

        internal static CreateGate BlockNextCreate()
        {
            var gate = new CreateGate();
            if (Interlocked.CompareExchange(ref _nextCreateGate, gate, null)
                is not null)
                throw new InvalidOperationException("A create gate is already installed.");
            return gate;
        }

        public async ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Interlocked.Increment(ref _createCount);
            Volatile.Write(ref _lastCreateContentType, request.ContentType);
            if (Interlocked.Exchange(ref _nextCreateGate, null) is { } gate)
            {
                gate.Entered.TrySetResult();
                await gate.Release.Task.WaitAsync(cancellationToken);
            }
            return ZLinkSpotCreateResponse.Accept(
                new ProductionCreateReply("production-created"));
        }

        public ValueTask OnClosingAsync(CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Interlocked.Increment(ref _closeCount);
            return ValueTask.CompletedTask;
        }

        internal sealed class CreateGate
        {
            internal TaskCompletionSource Entered { get; } =
                new(TaskCreationOptions.RunContinuationsAsynchronously);
            internal TaskCompletionSource Release { get; } =
                new(TaskCreationOptions.RunContinuationsAsynchronously);
        }
    }

    private sealed record ProductionCreateReply(string Value);

    private sealed class TestRelocationStore : IZLinkRelocationStore
    {
        private readonly Dictionary<string, byte[]> _payloads =
            new(StringComparer.Ordinal);

        public ValueTask<ZLinkRelocationStored> PutRelocationAsync(
            ReadOnlyMemory<byte> payload,
            TimeSpan retention,
            CancellationToken cancellationToken = default)
        {
            var bytes = payload.ToArray();
            var reference = Convert.ToHexString(
                System.Security.Cryptography.SHA256.HashData(bytes));
            _payloads[reference] = bytes;
            var now = DateTimeOffset.UtcNow;
            return ValueTask.FromResult(new ZLinkRelocationStored(
                reference,
                Zlink.Framework.Runtime.Locations.ZLinkCrc32C.Compute(bytes),
                now + retention,
                now));
        }

        public ValueTask<ZLinkRelocationReadResult> GetRelocationAsync(
            string reference,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkRelocationReadResult>(
                _payloads.TryGetValue(reference, out var payload)
                    ? new ZLinkRelocationReadResult.Found(payload)
                    : new ZLinkRelocationReadResult.Missing());

        public ValueTask<ZLinkRelocationRenewResult> RenewRelocationAsync(
            string reference,
            TimeSpan retention,
            CancellationToken cancellationToken = default)
        {
            var now = DateTimeOffset.UtcNow;
            return ValueTask.FromResult<ZLinkRelocationRenewResult>(
                _payloads.ContainsKey(reference)
                    ? new ZLinkRelocationRenewResult.Renewed(now + retention, now)
                    : new ZLinkRelocationRenewResult.Missing());
        }

        public ValueTask<ZLinkRelocationDeleteResult> DeleteRelocationAsync(
            string reference,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(
                _payloads.Remove(reference)
                    ? ZLinkRelocationDeleteResult.Deleted
                    : ZLinkRelocationDeleteResult.Missing);
    }
}
