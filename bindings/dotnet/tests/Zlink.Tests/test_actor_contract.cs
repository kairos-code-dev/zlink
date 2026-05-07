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
        using var actor = node.Actor($"actor-{Guid.NewGuid():N}");
        using var stream = new StreamSocket(ctx);
        RoutingId sessionRid = CoreTestSupport.RoutingIdUtf8("actor-session");
        stream.BindActor(node, sessionRid, actor.Ref, TimeSpan.FromSeconds(2));
        using Message joinMessage = Message.FromString("join:hello");

        Task<IReadOnlyList<Message>> joinTask = actor.JoinAsync(spot,
            joinMessage, TimeSpan.FromSeconds(2));

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
        spot.ReplyActorJoin(request.Info, accepted: true, reply);

        IReadOnlyList<Message> replies =
            await joinTask.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Single(replies);
        using (replies[0])
        {
            Assert.Equal("join:accepted", replies[0].GetString());
        }

        Assert.Contains(spot.ActorsSnapshot(),
            entry => entry.ActorId == actor.Ref.ActorId);
        actor.Leave(spot);
    }

    [Fact]
    public void remote_actor_ref_generation_zero_is_not_invalid()
    {
        RoutingId nodeRid = CoreTestSupport.RoutingIdUtf8("remote-node");
        ActorRef actor = ActorRef.Remote(nodeRid, "remote-actor");

        Assert.True(actor.IsUnchecked);
        Assert.Equal(0UL, actor.Generation);
        Assert.Equal("remote-actor", actor.ActorId);
        Assert.Equal(nodeRid, actor.NodeRid);
    }
}
