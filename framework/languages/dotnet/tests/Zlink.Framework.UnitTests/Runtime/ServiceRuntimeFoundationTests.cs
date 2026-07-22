using System.Text.Json;
using Systems.Zlink.Framework.Runtime.Protocol;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class ServiceRuntimeFoundationTests
{
    [Fact]
    public void GeneratedLivenessFixtures_DecodeWithExactErrors()
    {
        var frameworkRoot = Common.FrameworkTestEnvironment.GetFrameworkRoot();
        var fixturePath = Path.GetFullPath(
            "../../runtime/protocol/golden/service-decoder-fixtures-v1.json",
            frameworkRoot);
        using var document = JsonDocument.Parse(File.ReadAllText(fixturePath));

        foreach (var fixture in document.RootElement.GetProperty("canonical").EnumerateArray())
        {
            var bytes = fixture.GetProperty("bytes").EnumerateArray()
                .Select(static item => item.GetByte()).ToArray();
            Assert.True(ZLinkServiceWireCodec.TryDecodeLiveness(
                bytes, out var record, out var error));
            Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, error);
            Assert.Equal(fixture.GetProperty("commandId").GetByte(), (byte)record.Command);
            Assert.Equal(0x0102030405060708UL, record.ProbeId);
            Assert.Equal(bytes, ZLinkServiceWireCodec.EncodeLiveness(record.Command, record.ProbeId));
        }

        foreach (var fixture in document.RootElement.GetProperty("malformed").EnumerateArray())
        {
            var bytes = fixture.GetProperty("bytes").EnumerateArray()
                .Select(static item => item.GetByte()).ToArray();
            Assert.False(ZLinkServiceWireCodec.TryDecodeLiveness(
                bytes, out _, out var error));
            Assert.Equal(ExpectedError(fixture.GetProperty("error").GetString()!), error);
        }
    }

    [Fact]
    public void WireCodec_UsesGeneratedConstantsAndUtf8Bounds()
    {
        var encoded = ZLinkServiceWireCodec.EncodeLiveness(
            ServiceWireConstants.Command.LivenessProbe,
            1);
        Assert.Equal(ServiceWireConstants.Magic0, encoded[0]);
        Assert.Equal(ServiceWireConstants.Magic1, encoded[1]);
        Assert.Equal(ServiceWireConstants.WireMajor, encoded[2]);

        var text = ZLinkServiceWireCodec.EncodeText("가");
        Assert.Equal(3, text[0]);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ZLinkServiceWireCodec.EncodeText(new string('a', 256)));
    }

    [Fact]
    public void CompletionTable_CompletesTaskExactlyOnce()
    {
        var table = new ZLinkMeshCompletionTable();
        var operation = new MeshOperationId(0, 7);
        var calls = 0;
        Assert.True(table.Register(operation, (_, _) => Interlocked.Increment(ref calls)));

        var record = Completion(operation);
        table.Complete(record, Array.Empty<Message>());
        table.Complete(record, Array.Empty<Message>());

        Assert.Equal(1, calls);
    }

    [Fact]
    public void CompletionTable_FailAllPreservesTerminalResult()
    {
        var table = new ZLinkMeshCompletionTable();
        var operation = new MeshOperationId(0, 8);
        RequestResult? completed = null;
        Assert.True(table.RegisterRequest(
            operation,
            (result, _) => completed = result));

        table.FailAll(RequestResult.Terminated);

        Assert.Equal(RequestResult.Terminated, completed);
    }

    [Fact]
    public async Task InstanceStore_UsesExactFenceAcrossReadyAndRelease()
    {
        var store = new ZLinkInMemoryLocationStore();
        var ownerRid = RoutingId.From("owner");
        await store.RenewOwnerLeaseAsync(
            "owner", ownerRid, TimeSpan.FromMinutes(1));
        var claim = Assert.IsType<InstanceSpotClaimResult.Claimed>(
            await store.ClaimInstanceSpotAsync(new InstanceSpotClaimRequest(
                "mesh", RoutingId.From("spot"), "cart", ownerRid, 3, "owner")));
        var location = claim.Snapshot.Location;
        var fence = new InstanceSpotFence(
            location.MeshName,
            location.SpotRid,
            location.OwnerId,
            location.OwnerNodeGeneration,
            location.LocationGeneration,
            location.ActivationEpoch);

        var ready = Assert.IsType<InstanceSpotWriteResult.Stored>(
            await store.CommitInstanceSpotReadyAsync(fence, 9));
        Assert.Equal(ZLinkSpotActivationState.Ready,
            ready.Snapshot.Location.ActivationState);
        Assert.Equal(9UL, ready.Snapshot.Location.SpotGeneration);

        var stale = fence with { ActivationEpoch = fence.ActivationEpoch + 1 };
        Assert.IsType<InstanceSpotWriteResult.Stale>(
            await store.CommitInstanceSpotReadyAsync(stale, 10));
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            await store.ReleaseInstanceSpotAsync(fence));
        Assert.IsType<InstanceSpotResolveResult.Missing>(
            await store.ResolveInstanceSpotAsync(
                new ZLinkSpotLocationKey("mesh", RoutingId.From("spot"))));
    }

    [Fact]
    public async Task MonitorClose_RejectsLateResourcePublication()
    {
        var monitor = new RawMeshMonitor();
        await monitor.DisposeAsync();
        monitor.Publish(MeshMonitorEventKind.StateChanged, MeshNodeState.Stopped);
        Assert.Null(monitor.Recv(RecvFlags.DontWait));
        Assert.Equal(MeshNodeState.Stopped, monitor.Status().State);
    }

    private static MeshReceiveRecord Completion(MeshOperationId operation) =>
        new(
            MeshRecordKind.Completion,
            MeshReadyDomains.Infrastructure,
            default,
            default,
            0,
            default,
            operation,
            MeshOperationKind.NodeRequest,
            null,
            null,
            null,
            0,
            0,
            0,
            0,
            null);

    private static ZLinkServiceWireCodec.DecodeError ExpectedError(string error) =>
        error switch
        {
            "invalid-magic" => ZLinkServiceWireCodec.DecodeError.InvalidMagic,
            "unknown-command" => ZLinkServiceWireCodec.DecodeError.UnknownCommand,
            "forbidden-flag" => ZLinkServiceWireCodec.DecodeError.ForbiddenFlag,
            "invalid-field" => ZLinkServiceWireCodec.DecodeError.InvalidField,
            "truncated-field" => ZLinkServiceWireCodec.DecodeError.TruncatedField,
            "trailing-byte" => ZLinkServiceWireCodec.DecodeError.TrailingByte,
            _ => throw new InvalidOperationException(error)
        };
}
