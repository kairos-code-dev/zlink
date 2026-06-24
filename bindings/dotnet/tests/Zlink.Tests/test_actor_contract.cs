using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_actor_contract
{
    [Fact]
    public async Task local_actor_join_carries_request_and_reply_messages()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();
        using var actor = node.CreateActor($"actor-{Guid.NewGuid():N}");
        using Message joinMessage = Message.From("join:hello");

        Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> joinTask =
            actor.Join(spot).Message(joinMessage)
                .Timeout(TimeSpan.FromSeconds(2)).Async();

        ActorJoinRequest? request = null;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            request = spot.RecvActorJoin(RecvFlags.DontWait);
            return request != null;
        }, 2000));

        Assert.NotNull(request);
        using (request!.Message)
        {
            Assert.Equal("join:hello", request.Message.GetString());
        }
        Assert.Equal(actor.Ref.ActorId, request.Info.TargetActor.ActorId);

        using Message reply = Message.From("join:accepted");
        spot.ReplyActorJoin(request, joinResultCode: 0).Message(reply).Submit();

        IReadOnlyList<Message> replies =
            (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts;
        Assert.Single(replies);
        using (replies[0])
        {
            Assert.Equal("join:accepted", replies[0].GetString());
        }

        Assert.Contains(spot.Actors(),
            entry => entry.ActorId == actor.Ref.ActorId);
        Zlink.MultipartClose(await actor.Leave(spot)
            .Async().WaitAsync(TimeSpan.FromSeconds(5)));
    }

    [Fact]
    public async Task rejected_actor_join_preserves_application_result_code_and_reply()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();
        using var actor = node.CreateActor($"actor-{Guid.NewGuid():N}");
        using Message joinMessage = Message.From("join:reject");

        Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> joinTask =
            actor.Join(spot).Message(joinMessage)
                .Timeout(TimeSpan.FromSeconds(2)).Async();

        ActorJoinRequest? request = null;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            request = spot.RecvActorJoin(RecvFlags.DontWait);
            return request != null;
        }, 2000));

        request!.Message.Dispose();
        using Message reply = Message.From("join:room-full");
        spot.ReplyActorJoin(request, joinResultCode: 42).Message(reply).Submit();

        var rejected = await joinTask.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(RequestResult.Ok, rejected.Result.Result);
        Assert.Equal(42, rejected.Result.JoinResultCode);
        Assert.Equal(actor.Ref.ActorId, rejected.Result.Actor.ActorId);
        Assert.DoesNotContain(spot.Actors(),
            entry => entry.ActorId == actor.Ref.ActorId);

        Assert.Single(rejected.Parts);
        using (rejected.Parts[0])
        {
            Assert.Equal("join:room-full", rejected.Parts[0].GetString());
        }
    }

    [Fact]
    public async Task spot_actor_lifecycle_events_observe_join_and_leave()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();
        using var actor = node.CreateActor($"actor-{Guid.NewGuid():N}");
        using Message joinMessage = Message.From("join:lifecycle");
        var joined = new TaskCompletionSource<SpotActorLifecycleInfo>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var left = new TaskCompletionSource<SpotActorLifecycleInfo>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        spot.SetDispatchHandler(info =>
        {
            if (info.Event != SpotDispatchEvent.ActorLifecycleReadable)
                return;

            while (true)
            {
                SpotActorLifecycleEvent? lifecycle =
                    spot.RecvActorLifecycle(RecvFlags.DontWait);
                if (lifecycle == null)
                    return;

                if (lifecycle.Kind == SpotActorLifecycleEventKind.Joined)
                    joined.TrySetResult(lifecycle.Info);
                else if (lifecycle.Kind == SpotActorLifecycleEventKind.Left)
                    left.TrySetResult(lifecycle.Info);
            }
        });

        Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> joinTask =
            actor.Join(spot).Message(joinMessage)
                .Timeout(TimeSpan.FromSeconds(2)).Async();

        ActorJoinRequest? request = null;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            request = spot.RecvActorJoin(RecvFlags.DontWait);
            return request != null;
        }, 2000));

        request!.Message.Dispose();
        using Message reply = Message.From("join:accepted");
        spot.ReplyActorJoin(request, joinResultCode: 0).Message(reply).Submit();
        IReadOnlyList<Message> replies =
            (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts;
        foreach (Message message in replies)
            message.Dispose();

        SpotActorLifecycleInfo joinInfo =
            await joined.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(actor.Ref, joinInfo.CurrentActor);
        Assert.Equal(spot.RoutingId, joinInfo.CurrentSpotRid);
        Assert.True(joinInfo.JoinEpoch > 0);

        Zlink.MultipartClose(await actor.Leave(spot)
            .Async().WaitAsync(TimeSpan.FromSeconds(5)));

        SpotActorLifecycleInfo leaveInfo =
            await left.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(actor.Ref, leaveInfo.PreviousActor);
        Assert.Equal(spot.RoutingId, leaveInfo.PreviousSpotRid);
        Assert.True(leaveInfo.JoinEpoch > 0);
    }

    [Fact]
    public void actor_create_request_payload_is_exposed_on_entry_spot_lifecycle()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var entrySpot = node.EntrySpot();
        SpotActorLifecycleEvent? lifecycle = null;
        entrySpot.SetDispatchHandler(info =>
        {
            if (info.Event != SpotDispatchEvent.ActorLifecycleReadable)
                return;
            lifecycle ??= entrySpot.RecvActorLifecycle(RecvFlags.DontWait);
        });
        using Message profile = Message.From("profile");
        using Message displayName = Message.From("display-name");

        using var actor = node.CreateActor(
            $"actor-{Guid.NewGuid():N}",
            new[] { profile, displayName });

        Assert.Throws<ObjectDisposedException>(() => _ = profile.Size);
        Assert.Throws<ObjectDisposedException>(() => _ = displayName.Size);

        Assert.True(CoreTestSupport.WaitUntil(() => lifecycle != null, 2000));

        SpotActorLifecycleEvent created = lifecycle!;
        using (created)
        {
            Assert.Equal(SpotActorLifecycleEventKind.Joined, created.Kind);
            Assert.Equal(actor.Ref.ActorId,
                created.Info.CurrentActor.ActorId);
            Assert.Equal(2, created.RequestParts.Count);
            Assert.Equal("profile", created.RequestParts[0].GetString());
            Assert.Equal("display-name",
                created.RequestParts[1].GetString());
        }
    }

    [Fact]
    public async Task spot_node_join_actor_entry_spot_returns_final_actor_ref()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();
        using var entry = node.EntrySpot();
        using var actor = node.CreateActor($"actor-{Guid.NewGuid():N}");
        using Message joinMessage = Message.From("join:user");

        Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> joinTask =
            actor.Join(spot).Message(joinMessage)
                .Timeout(TimeSpan.FromSeconds(2)).Async();

        ActorJoinRequest? request = null;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            request = spot.RecvActorJoin(RecvFlags.DontWait);
            return request != null;
        }, 2000));

        request!.Message.Dispose();
        using Message reply = Message.From("ok");
        spot.ReplyActorJoin(request, joinResultCode: 0).Message(reply).Submit();
        foreach (Message message in
                 (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts)
            message.Dispose();

        using Message entryJoinMessage = Message.From("join:entry");
        Task<(ActorJoinEntrySpotResult Result, IReadOnlyList<Message> Parts)>
            entryJoinTask = node.JoinActorEntrySpot(actor.Ref, node.RoutingId,
                    entryJoinMessage)
                .Timeout(TimeSpan.FromSeconds(2))
                .Async();

        ActorJoinRequest? entryRequest = null;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            entryRequest = entry.RecvActorJoin(RecvFlags.DontWait);
            return entryRequest != null;
        }, 2000));

        Assert.Equal("join:entry",
            Encoding.UTF8.GetString(entryRequest!.Message.ToArray()));
        entryRequest.Message.Dispose();
        using Message entryReply = Message.From("entry-ok");
        entry.ReplyActorJoin(entryRequest, joinResultCode: 0)
            .Message(entryReply)
            .Submit();

        (ActorJoinEntrySpotResult result, IReadOnlyList<Message> entryParts) =
            await entryJoinTask.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(RequestResult.Ok, result.Result);
        Assert.Equal(0, result.JoinResultCode);
        Assert.Equal(actor.Ref.ActorId, result.Actor.ActorId);
        Assert.Equal(node.RoutingId, result.Actor.NodeRid);
        Assert.Equal(node.RoutingId, result.TargetNodeRid);
        Assert.Equal(entry.RoutingId, result.JoinedSpotRid);
        Assert.True(result.JoinEpoch > 0);
        Assert.Single(entryParts);
        Assert.Equal("entry-ok",
            Encoding.UTF8.GetString(entryParts[0].ToArray()));
        foreach (Message message in entryParts)
            message.Dispose();
        Assert.Contains(entry.Actors(),
            row => row.ActorId == actor.Ref.ActorId);
        Assert.Null(entry.RecvActorJoin(RecvFlags.DontWait));
    }

    [Fact]
    public async Task connect_peer_rid_routes_entry_spot_join_to_target_node_rid()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var sourceContext = Zlink.CreateContext();
        using var targetContext = Zlink.CreateContext();
        using var sourceNode = sourceContext.CreateSpotNode(SpotNodeMode.Routed);
        using var targetNode = targetContext.CreateSpotNode(SpotNodeMode.Routed);
        using var entry = targetNode.EntrySpot();

        RoutingId sourceRid = CoreTestSupport.RoutingIdUtf8("source-rid-peer");
        RoutingId targetRid = CoreTestSupport.RoutingIdUtf8("target-rid-peer");
        string sourceEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "connect-peer-rid-source");
        string targetEndpoint = CoreTestSupport.NewEndpoint("tcp",
            "connect-peer-rid-target");

        sourceNode.SetRoutingId(sourceRid);
        targetNode.SetRoutingId(targetRid);
        sourceNode.SetRouterBind(sourceEndpoint);
        targetNode.SetRouterBind(targetEndpoint);
        sourceNode.ConnectPeerRid(targetRid, targetEndpoint);
        Thread.Sleep(300);

        IActor actor = sourceNode.CreateActor($"actor-{Guid.NewGuid():N}");
        using Message request = Message.From("join:remote-entry");
        Task<(ActorJoinEntrySpotResult Result, IReadOnlyList<Message> Parts)>
            joinTask = sourceNode.JoinActorEntrySpot(actor.Ref, targetRid, request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async();

        ActorJoinRequest? joinRequest = null;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            joinRequest = entry.RecvActorJoin(RecvFlags.DontWait);
            return joinRequest != null;
        }, 5000));

        Assert.Equal(actor.Ref.ActorId, joinRequest!.Info.TargetActor.ActorId);
        joinRequest.Message.Dispose();
        using Message reply = Message.From("remote-entry-ok");
        entry.ReplyActorJoin(joinRequest, joinResultCode: 0)
            .Message(reply)
            .Submit();

        (ActorJoinEntrySpotResult result, IReadOnlyList<Message> parts) =
            await joinTask.WaitAsync(TimeSpan.FromSeconds(10));

        Assert.Equal(RequestResult.Ok, result.Result);
        Assert.Equal(targetRid, result.TargetNodeRid);
        Assert.Single(parts);
        Assert.Equal("remote-entry-ok", parts[0].GetString());
        foreach (Message part in parts)
            part.Dispose();
        await targetNode.DestroyActor(result.Actor)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();
    }

    [Fact]
    public void remote_actor_ref_generation_zero_is_not_invalid()
    {
        RoutingId nodeRid = CoreTestSupport.RoutingIdUtf8("remote-node");
        ActorRef actor = new(nodeRid, "remote-actor", generation: 0);

        Assert.True(actor.IsUnchecked);
        Assert.Equal(0UL, actor.Generation);
        Assert.Equal("remote-actor", actor.ActorId);
        Assert.Equal(nodeRid, actor.NodeRid);
    }
}
