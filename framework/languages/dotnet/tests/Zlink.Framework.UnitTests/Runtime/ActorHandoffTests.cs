using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class ActorHandoffTests
{
    [Fact]
    public void InFlightHandoffOrder_PreservesArrivalOrder()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        state.BeginHandoffCapture();

        Capture(state, "P1", "session-1");
        Capture(state, "P2", "session-1");
        Capture(state, "P3", "session-1");

        var frames = state.DrainHandoffFrames();
        Assert.Equal(["P1", "P2", "P3"], frames.Select(DecodeBody));
    }

    [Fact]
    public void DirectOvertakesPrevented_ImportedBacklogStaysAheadOfTargetArrivals()
    {
        var source = new ZLinkActorRuntimeState("actor-1");
        source.BeginHandoffCapture();
        Capture(source, "B1", "session-1");
        Capture(source, "B2", "session-1");

        var target = new ZLinkActorRuntimeState("actor-1");
        target.ImportHandoffFrames(source.DrainHandoffFrames());
        Capture(target, "D1", "session-2");

        var frames = target.CompleteHandoffCapture();
        Assert.Equal(["B1", "B2", "D1"], frames.Select(DecodeBody));
    }

    [Fact]
    public void BoundSessionCrossMoveOrder_PreservesSessionRouteAndSequence()
    {
        var source = new ZLinkActorRuntimeState("actor-1");
        source.BeginHandoffCapture();
        Capture(source, "S1", "bound-session");
        Capture(source, "S2", "bound-session");

        var target = new ZLinkActorRuntimeState("actor-1");
        target.ImportHandoffFrames(source.DrainHandoffFrames());
        Capture(target, "S3", "bound-session");
        Capture(target, "S4", "bound-session");

        var frames = target.CompleteHandoffCapture();
        Assert.Equal(["S1", "S2", "S3", "S4"], frames.Select(DecodeBody));
        Assert.All(frames, frame => Assert.Equal("bound-session", RoutingId.From(frame.SourceSessionRid).ToString()));
    }

    [Fact]
    public async Task StragglerForwardThenFailFast_UsesBoundedWindow()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        var source = ActorRef("node-a", 1);
        var target = ActorRef("node-b", 2);
        state.BindNativeActorRef(target);
        state.InstallForwardingMapping(source, target, TimeSpan.FromMilliseconds(50));

        Assert.Equal(ZLinkActorFrameRoute.Forward, state.ResolveFrameRoute(source, out var forwarded));
        Assert.Equal(target, forwarded);

        await Task.Delay(150);
        Assert.Equal(ZLinkActorFrameRoute.Stale, state.ResolveFrameRoute(source, out _));
    }

    [Fact]
    public void ForwardingMappingEviction_ReplacesTheNodeActorEntry()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        var firstSource = ActorRef("node-a", 1);
        var firstTarget = ActorRef("node-b", 2);
        var nextSource = ActorRef("node-a", 3);
        var nextTarget = ActorRef("node-c", 4);
        state.BindNativeActorRef(nextTarget);

        state.InstallForwardingMapping(firstSource, firstTarget, TimeSpan.FromSeconds(1));
        state.InstallForwardingMapping(nextSource, nextTarget, TimeSpan.FromSeconds(1));

        Assert.Equal(ZLinkActorFrameRoute.Stale, state.ResolveFrameRoute(firstSource, out _));
        Assert.Equal(ZLinkActorFrameRoute.Forward, state.ResolveFrameRoute(nextSource, out var forwarded));
        Assert.Equal(nextTarget, forwarded);
    }

    private static void Capture(ZLinkActorRuntimeState state, string bodyText, string sessionRid)
    {
        using var body = Message.From(System.Text.Encoding.UTF8.GetBytes(bodyText));
        var frame = new ZLinkSpotActorFrame(
            ActorRef("node-a", 1),
            RoutingId.From("session-node"),
            RoutingId.From(sessionRid),
            1,
            0,
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.None,
                null,
                "Packet",
                ZlinkStreamMetadata.Empty),
            body);

        Assert.True(state.TryCaptureHandoffFrame(frame));
    }

    private static string DecodeBody(ZLinkActorHandoffFrame frame)
        => System.Text.Encoding.UTF8.GetString(frame.Body);

    private static ZLinkBackendActorRef ActorRef(string nodeRid, ulong generation)
        => new(RoutingId.From(nodeRid), "actor-1", generation);
}
