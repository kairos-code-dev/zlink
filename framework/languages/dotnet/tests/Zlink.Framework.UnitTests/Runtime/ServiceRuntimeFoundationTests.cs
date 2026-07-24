using System.Buffers.Binary;
using System.Diagnostics;
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
    public void RouteAdmission_RoundTripsDeterministicDescriptor()
    {
        var channels = new Dictionary<string, uint>(StringComparer.Ordinal)
        {
            ["worker"] = 75,
            ["admin"] = 0
        };

        var encoded = ZLinkServiceWireCodec.EncodeRouteAdmission(
            ServiceWireConstants.Command.Hello,
            "orders",
            "tcp://127.0.0.1:7070",
            17,
            23,
            channels);

        Assert.True(ZLinkServiceWireCodec.TryDecodeRouteAdmission(
            encoded,
            out var command,
            out var admission,
            out var error));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, error);
        Assert.Equal(ServiceWireConstants.Command.Hello, command);
        Assert.Equal("orders", admission.MeshName);
        Assert.Equal("tcp://127.0.0.1:7070", admission.AdvertisedEndpoint);
        Assert.Equal(17UL, admission.LifecycleGeneration);
        Assert.Equal(23UL, admission.DescriptorRevision);
        Assert.Equal(0U, admission.Channels["admin"]);
        Assert.Equal(75U, admission.Channels["worker"]);
        Assert.Equal("none", admission.SecurityIdentity);
        Assert.Equal((uint)int.MaxValue, admission.EffectiveMaxMessageBytes);
        Assert.Equal(1, admission.RuntimeState);
        Assert.Equal(0, admission.ApplicationVersion);
        Assert.Equal(0, admission.ObjectRole);
        Assert.Equal(100U, admission.PlacementWeight);
        Assert.Equal(10_000U, admission.ActiveCapacityLimit);
        Assert.Equal(128U, admission.PendingCapacityLimit);
        Assert.Equal(0U, admission.ActiveCapacityUsed);
        Assert.Equal(0U, admission.PendingCapacityUsed);
        Assert.Equal(
            new byte[] { 1, 2, 6, 7, 8, 9, 10, 11, 12 },
            admission.ExtensionFields.Keys);
    }

    [Fact]
    public void RouteAdmission_PreservesUnknownExtensionFields()
    {
        var encoded = ZLinkServiceWireCodec.EncodeRouteAdmission(
            ServiceWireConstants.Command.Hello,
            "orders",
            "tcp://127.0.0.1:7070",
            17,
            23,
            new Dictionary<string, uint>(StringComparer.Ordinal)
            {
                ["worker"] = 75
            });
        var extended = AppendDescriptorExtension(encoded, 13, [0xaa, 0xbb]);

        Assert.True(ZLinkServiceWireCodec.TryDecodeRouteAdmission(
            extended,
            out _,
            out var admission,
            out var error));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, error);
        Assert.Equal(new byte[] { 0xaa, 0xbb }, admission.ExtensionFields[13]);
        Assert.Equal(extended.AsSpan(10).ToArray(), admission.DescriptorBytes);
    }

    [Fact]
    public void AdmissionGuard_ValidatesRevisionAndImmutableFieldsBeforeMutation()
    {
        var channels = new Dictionary<string, uint>(StringComparer.Ordinal)
        {
            ["worker"] = 75
        };
        var current = DecodeAdmission(
            ZLinkServiceWireCodec.EncodeRouteAdmission(
                ServiceWireConstants.Command.Hello,
                "orders",
                "tcp://127.0.0.1:7070",
                17,
                23,
                channels));

        Assert.Equal(
            ZLinkServiceAdmissionDecision.Idempotent,
            ZLinkServiceAdmissionGuard.Evaluate(
                current,
                ServiceWireConstants.Command.Update,
                current));

        var newer = DecodeAdmission(
            ZLinkServiceWireCodec.EncodeRouteAdmission(
                ServiceWireConstants.Command.Update,
                "orders",
                "tcp://127.0.0.1:7070",
                17,
                24,
                new Dictionary<string, uint>(StringComparer.Ordinal)
                {
                    ["worker"] = 25
                }));
        Assert.Equal(
            ZLinkServiceAdmissionDecision.Accept,
            ZLinkServiceAdmissionGuard.Evaluate(
                current,
                ServiceWireConstants.Command.Update,
                newer));

        var sameRevisionDifferentBytes = DecodeAdmission(
            ZLinkServiceWireCodec.EncodeRouteAdmission(
                ServiceWireConstants.Command.Update,
                "orders",
                "tcp://127.0.0.1:7070",
                17,
                23,
                new Dictionary<string, uint>(StringComparer.Ordinal)
                {
                    ["worker"] = 25
                }));
        Assert.Equal(
            ZLinkServiceAdmissionDecision.Reject,
            ZLinkServiceAdmissionGuard.Evaluate(
                current,
                ServiceWireConstants.Command.Update,
                sameRevisionDifferentBytes));

        var immutableMutationBytes = ZLinkServiceWireCodec.EncodeRouteAdmission(
            ServiceWireConstants.Command.Update,
            "orders",
            "tcp://127.0.0.1:7070",
            17,
            24,
            channels);
        var securityOffset = FindSequence(immutableMutationBytes, "none"u8);
        "evil"u8.CopyTo(immutableMutationBytes.AsSpan(securityOffset));
        var immutableMutation = DecodeAdmission(immutableMutationBytes);
        Assert.Equal(
            ZLinkServiceAdmissionDecision.Reject,
            ZLinkServiceAdmissionGuard.Evaluate(
                current,
                ServiceWireConstants.Command.Update,
                immutableMutation));

        Assert.Equal(
            ZLinkServiceAdmissionDecision.Reject,
            ZLinkServiceAdmissionGuard.Evaluate(
                null,
                ServiceWireConstants.Command.Update,
                newer));
    }

    [Fact]
    public void ApplicationAndReplyRecords_RoundTripExactTerminalFields()
    {
        var request = ZLinkServiceWireCodec.EncodeApplication(
            ServiceWireConstants.Command.ChannelRequest,
            41,
            "worker",
            hasMetadata: true);
        Assert.True(ZLinkServiceWireCodec.TryDecodeApplication(
            request,
            out var application,
            out var requestError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, requestError);
        Assert.Equal(41UL, application.Correlation);
        Assert.Equal("worker", application.ChannelName);
        Assert.True(application.HasMetadata);

        var reply = ZLinkServiceWireCodec.EncodeReply(41, -3, 19);
        Assert.True(ZLinkServiceWireCodec.TryDecodeReply(
            reply,
            out var terminal,
            out var replyError));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, replyError);
        Assert.Equal(41UL, terminal.Correlation);
        Assert.Equal(-3, terminal.TerminalResult);
        Assert.Equal(19U, terminal.FailureCode);
    }

    [Fact]
    public void Liveness_RetransmitsOutstandingProbeAndExtendsOnlyOnExactAck()
    {
        var frequency = Stopwatch.Frequency;
        var liveness = new ZLinkServiceLiveness(0);

        Assert.False(liveness.TryGetProbe(5 * frequency - 1, out _));
        Assert.True(liveness.TryGetProbe(5 * frequency, out var firstProbe));
        Assert.NotEqual(0UL, firstProbe);
        Assert.True(liveness.TryGetProbe(10 * frequency, out var retransmit));
        Assert.Equal(firstProbe, retransmit);
        Assert.False(liveness.Acknowledge(firstProbe + 1, 11 * frequency));
        Assert.Equal(15 * frequency, liveness.DeadlineTimestamp);
        Assert.True(liveness.Acknowledge(firstProbe, 11 * frequency));
        Assert.Equal(26 * frequency, liveness.DeadlineTimestamp);
        Assert.False(liveness.IsExpired(26 * frequency - 1));
        Assert.True(liveness.IsExpired(26 * frequency));

        Assert.True(liveness.TryGetProbe(15 * frequency, out var nextProbe));
        Assert.NotEqual(firstProbe, nextProbe);
    }

    [Fact]
    public async Task ManagedNode_LocalRequestPublishesOneTerminalCompletion()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var node = new ZLinkManagedMeshNode(context, "orders");
        var nodeRid = RoutingId.From("orders-1");
        node.SetRoutingId(nodeRid);

        using var requestPart = Message.From(new byte[] { 1, 2, 3 });
        Assert.Equal(
            SubmitResult.Ok,
            node.RequestToNode(
                nodeRid,
                [requestPart],
                out var operationId,
                TimeSpan.FromSeconds(1)));

        using var ready = new MeshReadyBatch();
        Assert.False(node.DrainReady(
            MeshReadyDomains.All,
            ready,
            RecvFlags.DontWait));
        Assert.Equal(1, ready.Count);

        using var claim = ready.TakeClaim(0);
        using var received = new MeshReceiveBatch();
        Assert.True(claim.Receive(received, RecvFlags.DontWait));
        var request = received[0];
        Assert.Equal(MeshRecordKind.NodeRequest, request.Kind);

        using var replyPart = Message.From(new byte[] { 4, 5, 6 });
        Assert.Equal(SubmitResult.Ok, request.Reply([replyPart]));
        Assert.Equal(SubmitResult.Ok, request.Reply([replyPart]));

        received.Reset();
        Assert.False(claim.Receive(received, RecvFlags.DontWait));
        claim.Dispose();
        ready.Reset();
        node.DrainReady(
            MeshReadyDomains.Infrastructure,
            ready,
            RecvFlags.DontWait);
        using var completionClaim = ready.TakeClaim(0);
        Assert.True(completionClaim.Receive(received, RecvFlags.DontWait));
        Assert.Equal(1, received.Count);
        Assert.Equal(MeshRecordKind.Completion, received[0].Kind);
        Assert.Equal(operationId, received[0].OperationId);
        Assert.Equal((int)RequestResult.Ok, received[0].TerminalResult);
        Assert.False(completionClaim.Receive(received, RecvFlags.DontWait));
    }

    [Fact]
    public async Task ManagedNodes_AdmitAndRouteChannelOverRawRouter()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = new ZLinkManagedMeshNode(context, "orders");
        await using var target = new ZLinkManagedMeshNode(context, "orders");
        var sourceRid = RoutingId.From("orders-source");
        var targetRid = RoutingId.From("orders-target");
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://orders-source-{suffix}";
        var targetEndpoint = $"inproc://orders-target-{suffix}";

        source.SetRoutingId(sourceRid);
        source.SetBind(sourceEndpoint);
        source.ConnectPeer(targetEndpoint, targetRid);
        target.SetRoutingId(targetRid);
        target.SetBind(targetEndpoint);
        target.AddChannel("worker");
        target.ConnectPeer(sourceEndpoint, sourceRid);
        source.Start();
        target.Start();

        await WaitUntilAsync(
            () => source.Status().AdmittedPeerCount == 1
                  && target.Status().AdmittedPeerCount == 1);

        using var payload = Message.From(new byte[] { 7, 8, 9 });
        Assert.Equal(
            SubmitResult.Ok,
            source.EntrySpot().SendToChannel("worker", [payload]));

        using var ready = new MeshReadyBatch();
        await WaitUntilAsync(() =>
        {
            ready.Reset();
            target.DrainReady(
                MeshReadyDomains.All,
                ready,
                RecvFlags.DontWait);
            return ready.Count == 1;
        });

        using var claim = ready.TakeClaim(0);
        using var received = new MeshReceiveBatch();
        Assert.True(claim.Receive(received, RecvFlags.DontWait));
        Assert.Equal(MeshRecordKind.ChannelSend, received[0].Kind);
        Assert.Equal("worker", received[0].ChannelName);
        Assert.Equal(sourceRid, received[0].SourceNodeRid);
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
                "mesh", "spot", "cart", ownerRid, 3, "owner")));
        var location = claim.Snapshot.Location;
        var fence = new InstanceSpotFence(
            location.MeshName,
            location.SpotId,
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
                new ZLinkSpotLocationKey("spot")));
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

    private static ZLinkServiceWireCodec.AdmissionRecord DecodeAdmission(byte[] bytes)
    {
        Assert.True(ZLinkServiceWireCodec.TryDecodeRouteAdmission(
            bytes,
            out _,
            out var admission,
            out var error));
        Assert.Equal(ZLinkServiceWireCodec.DecodeError.None, error);
        return admission;
    }

    private static byte[] AppendDescriptorExtension(
        byte[] encoded,
        byte id,
        ReadOnlySpan<byte> value)
    {
        var offset = 10;
        offset += 1 + encoded[offset];
        offset += 1 + encoded[offset];
        offset += sizeof(uint) + sizeof(ulong) + sizeof(ulong);
        var endpointLength = BinaryPrimitives.ReadUInt16BigEndian(encoded.AsSpan(offset));
        offset += sizeof(ushort) + endpointLength;
        var channelCount = BinaryPrimitives.ReadUInt16BigEndian(encoded.AsSpan(offset));
        offset += sizeof(ushort);
        for (var index = 0; index < channelCount; index++)
        {
            offset += 1 + encoded[offset];
            offset += sizeof(uint);
        }

        var previousLength = BinaryPrimitives.ReadUInt32BigEndian(encoded.AsSpan(offset));
        var result = new byte[encoded.Length + 1 + sizeof(uint) + value.Length];
        encoded.CopyTo(result, 0);
        BinaryPrimitives.WriteUInt32BigEndian(
            result.AsSpan(offset),
            checked(previousLength + (uint)(1 + sizeof(uint) + value.Length)));
        var tail = encoded.Length;
        result[tail] = id;
        BinaryPrimitives.WriteUInt32BigEndian(
            result.AsSpan(tail + 1),
            checked((uint)value.Length));
        value.CopyTo(result.AsSpan(tail + 1 + sizeof(uint)));
        BinaryPrimitives.WriteUInt32BigEndian(
            result.AsSpan(6),
            checked((uint)(result.Length - 10)));
        return result;
    }

    private static int FindSequence(byte[] bytes, ReadOnlySpan<byte> sequence)
    {
        for (var offset = 0; offset <= bytes.Length - sequence.Length; offset++)
            if (bytes.AsSpan(offset, sequence.Length).SequenceEqual(sequence))
                return offset;
        throw new InvalidOperationException("Test sequence was not found.");
    }

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        var deadline = Stopwatch.GetTimestamp() + 5 * Stopwatch.Frequency;
        while (!condition())
        {
            if (Stopwatch.GetTimestamp() >= deadline)
                throw new TimeoutException("The managed MeshNode condition was not reached.");
            await Task.Delay(10);
        }
    }
}
