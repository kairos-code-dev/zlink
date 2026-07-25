namespace Zlink.Framework.UnitTests;

public sealed class ActorBoundSessionRelayTests
{
    [Fact]
    public void Bound_Session_Operation_Runs_Only_For_The_Expected_Binding()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        var originalRid = RoutingId.From("session-original");
        var replacementRid = RoutingId.From("session-replacement");
        var originalToken = ZLinkActorBoundSessionBindingToken.Native(originalRid);
        var replacementToken = ZLinkActorBoundSessionBindingToken.Native(replacementRid);
        var calls = 0;

        state.BindSession(
            null,
            originalRid,
            originalToken,
            objectGeneration: 1,
            authorityOwnerGeneration: 1,
            meshName: "actors",
            ownerLeaseGeneration: 1);
        Assert.True(state.TryUseBoundSession(originalToken, _ =>
        {
            calls++;
            return true;
        }));

        state.BindSession(
            null,
            replacementRid,
            replacementToken,
            objectGeneration: 1,
            authorityOwnerGeneration: 2,
            meshName: "actors",
            ownerLeaseGeneration: 1);
        Assert.True(state.TryUseBoundSession(originalToken, _ =>
        {
            calls++;
            return true;
        }));

        Assert.Equal(1, calls);
        Assert.True(state.TryGetBoundSession(out var current));
        Assert.Equal(replacementToken, current.BindingToken);
    }
}
