using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Configuration;

public sealed class MonitoringExactContracts
{
    [Fact]
    [ContractExample(typeof(IZLinkClientServerRuntime))]
    public void ClientServerRuntimeHasExactChannelScopedReadOnlySurface()
    {
        Assert.Equal(
            new string[]
            {
                "IsReady",
                "ObserveAsync",
                "Snapshot"
            },
            typeof(IZLinkClientServerRuntime)
                .GetMethods()
                .Select(static method => method.Name)
                .Order(StringComparer.Ordinal)
                .ToArray());
        Assert.DoesNotContain(
            typeof(IZLinkClientServerRuntime).GetMethods()
                .SelectMany(static method => method.GetParameters()),
            static parameter =>
                StringComparer.Ordinal.Equals(
                    parameter.Name,
                    "meshName"));
        Assert.Equal(
            typeof(ValueTask),
            typeof(IZLinkRuntimeMessageFlowObserver)
                .GetMethod(nameof(
                    IZLinkRuntimeMessageFlowObserver.OnMessageFlowAsync))!
                .ReturnType);
        Assert.Equal(
            typeof(ValueTask),
            typeof(IZLinkRuntimeErrorSink)
                .GetMethod(nameof(IZLinkRuntimeErrorSink.OnRuntimeErrorAsync))!
                .ReturnType);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkMessageFlowRuntime),
        typeof(IZLinkRuntimeMessageFlowObserver),
        typeof(IZLinkRuntimeErrorSink))]
    public void RuntimeMessageFlowModeAndEventFollowExactContract()
    {
        Assert.Equal(
            [
                ZLinkRuntimeMessageFlowMode.Off,
                ZLinkRuntimeMessageFlowMode.ErrorsOnly,
                ZLinkRuntimeMessageFlowMode.KeyTransitions,
                ZLinkRuntimeMessageFlowMode.Verbose
            ],
            Enum.GetValues<ZLinkRuntimeMessageFlowMode>());
        Assert.Equal(
            new string[]
            {
                "Action",
                "ActivationState",
                "ActorId",
                "ChannelName",
                "ChannelRouteKind",
                "CorrelationId",
                "DurationSeconds",
                "EventId",
                "FlowId",
                "FlowOrigin",
                "InstanceSpotType",
                "MeshName",
                "MessageKind",
                "MessageSizeBytes",
                "Outcome",
                "PacketName",
                "Phase",
                "Reason",
                "ServerRid",
                "SourceRid",
                "SpotId",
                "Surface",
                "TargetRid",
                "Timestamp",
                "Topic"
            },
            typeof(ZLinkRuntimeMessageFlowEvent)
                .GetProperties()
                .Select(static property => property.Name)
                .Order(StringComparer.Ordinal)
                .ToArray());
    }

    [Fact]
    public void InstanceSpotTypeSnapshotIsAnAggregateWithoutIdentityLeaks()
    {
        var names = typeof(ZLinkInstanceSpotTypeSnapshot)
            .GetProperties()
            .Select(static property => property.Name)
            .ToHashSet(StringComparer.Ordinal);

        Assert.Equal(
            new string[]
            {
                "ActivatingCount",
                "ActiveCount",
                "ClosingCount",
                "InstanceSpotType",
                "LastActivationOutcome",
                "PendingByteCount",
                "PendingMessageCount"
            },
            names.Order(StringComparer.Ordinal).ToArray());
        Assert.DoesNotContain("SpotId", names);
        Assert.DoesNotContain("ObjectGeneration", names);
        Assert.DoesNotContain("AuthorityOwnerGeneration", names);
    }
}
