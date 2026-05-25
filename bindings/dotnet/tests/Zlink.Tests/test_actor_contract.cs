using System;
using System.Collections.Generic;
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

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = node.CreateSpot();
        using var actor = node.CreateActor($"actor-{Guid.NewGuid():N}");
        using Message joinMessage = Message.FromString("join:hello");

        Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> joinTask =
            actor.Join(spot).Message(joinMessage)
                .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync();

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

        using Message reply = Message.FromString("join:accepted");
        spot.ReplyActorJoin(request, accepted: true).Message(reply).Submit();

        IReadOnlyList<Message> replies =
            (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts;
        Assert.Single(replies);
        using (replies[0])
        {
            Assert.Equal("join:accepted", replies[0].GetString());
        }

        Assert.Contains(spot.ActorsSnapshot(),
            entry => entry.ActorId == actor.Ref.ActorId);
        Zlink.MultipartClose(await actor.Leave(spot)
            .SubmitAsync().WaitAsync(TimeSpan.FromSeconds(5)));
    }

    [Fact]
    public async Task spot_actor_lifecycle_callbacks_observe_join_and_leave()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = node.CreateSpot();
        using var actor = node.CreateActor($"actor-{Guid.NewGuid():N}");
        using Message joinMessage = Message.FromString("join:lifecycle");
        var joined = new TaskCompletionSource<SpotActorLifecycleInfo>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var left = new TaskCompletionSource<SpotActorLifecycleInfo>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        spot.OnActorLifecycle(
            info => joined.TrySetResult(info),
            info => left.TrySetResult(info));

        Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> joinTask =
            actor.Join(spot).Message(joinMessage)
                .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync();

        ActorJoinRequest? request = null;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            request = spot.RecvActorJoin(RecvFlags.DontWait);
            return request != null;
        }, 2000));

        request!.Message.Dispose();
        using Message reply = Message.FromString("join:accepted");
        spot.ReplyActorJoin(request, accepted: true).Message(reply).Submit();
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
            .SubmitAsync().WaitAsync(TimeSpan.FromSeconds(5)));

        SpotActorLifecycleInfo leaveInfo =
            await left.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(actor.Ref, leaveInfo.PreviousActor);
        Assert.Equal(spot.RoutingId, leaveInfo.PreviousSpotRid);
        Assert.True(leaveInfo.JoinEpoch > 0);
    }

    [Fact]
    public async Task spot_node_join_actor_entry_spot_returns_final_actor_ref()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = node.CreateSpot();
        using var entry = node.EntrySpot();
        using var actor = node.CreateActor($"actor-{Guid.NewGuid():N}");
        using Message joinMessage = Message.FromString("join:user");

        Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> joinTask =
            actor.Join(spot).Message(joinMessage)
                .Timeout(TimeSpan.FromSeconds(2)).SubmitAsync();

        ActorJoinRequest? request = null;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            request = spot.RecvActorJoin(RecvFlags.DontWait);
            return request != null;
        }, 2000));

        request!.Message.Dispose();
        using Message reply = Message.FromString("ok");
        spot.ReplyActorJoin(request, accepted: true).Message(reply).Submit();
        foreach (Message message in
                 (await joinTask.WaitAsync(TimeSpan.FromSeconds(5))).Parts)
            message.Dispose();

        ActorJoinEntrySpotResult result =
            await node.JoinActorEntrySpot(actor.Ref, node.RoutingId)
                .Timeout(TimeSpan.FromSeconds(2))
                .SubmitAsync()
                .WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(RequestResult.Ok, result.Result);
        Assert.Equal(actor.Ref.ActorId, result.Actor.ActorId);
        Assert.Equal(node.RoutingId, result.Actor.NodeRid);
        Assert.Equal(node.RoutingId, result.TargetNodeRid);
        Assert.True(result.JoinEpoch > 0);
        Assert.Contains(entry.ActorsSnapshot(),
            row => row.ActorId == actor.Ref.ActorId);
        Assert.Null(entry.RecvActorJoin(RecvFlags.DontWait));
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
