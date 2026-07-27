using System.Collections.Concurrent;
using System.Diagnostics;
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal sealed class RelocationBulkWorkload(
    SpotActorTransferScenarioContext context,
    string scenario,
    string targetKind,
    IReadOnlyList<string> targetIds,
    int operationsPerSecond)
{
    private readonly ConcurrentQueue<double> _requestLatencyMs = new();
    private readonly ConcurrentDictionary<string, ConcurrentQueue<long>>
        _acceptedRequests = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, ConcurrentQueue<long>>
        _acceptedOneWay = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, string>
        _requestOperationIds = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, string>
        _oneWayOperationIds = new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, byte>
        _requestCorrelationIds = new(StringComparer.Ordinal);
    private long _requestOffered;
    private long _requestSubmitted;
    private long _requestSucceeded;
    private long _requestFailed;
    private long _oneWayOffered;
    private long _oneWaySubmitted;
    private long _oneWaySucceeded;
    private long _oneWayFailed;

    public async Task<RelocationBulkWorkloadResult> RunAsync(
        TimeSpan duration,
        CancellationToken cancellationToken = default)
    {
        if (targetIds.Count == 0)
            throw new ArgumentException(
                "At least one workload target is required.",
                nameof(targetIds));

        var started = Stopwatch.StartNew();
        using var durationCancellation =
            CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken);
        durationCancellation.CancelAfter(duration);
        var operations = new ConcurrentBag<Task>();
        await Task.WhenAll(
            RunRequestPacerAsync(
                durationCancellation.Token,
                operations),
            RunOneWayPacerAsync(
                durationCancellation.Token,
                operations));
        await Task.WhenAll(operations.ToArray());

        started.Stop();
        return new RelocationBulkWorkloadResult(
            scenario,
            targetKind,
            started.Elapsed,
            Interlocked.Read(ref _requestOffered),
            Interlocked.Read(ref _requestSubmitted),
            Interlocked.Read(ref _requestSucceeded),
            Interlocked.Read(ref _requestFailed),
            Interlocked.Read(ref _oneWayOffered),
            Interlocked.Read(ref _oneWaySubmitted),
            Interlocked.Read(ref _oneWaySucceeded),
            Interlocked.Read(ref _oneWayFailed),
            Percentile(_requestLatencyMs, 0.50),
            Percentile(_requestLatencyMs, 0.95),
            Percentile(_requestLatencyMs, 0.99),
            _requestLatencyMs.Count == 0
                ? 0
                : _requestLatencyMs.Max(),
            _acceptedRequests.ToDictionary(
                static pair => pair.Key,
                static pair =>
                    (IReadOnlyList<long>)pair.Value.ToArray(),
                StringComparer.Ordinal),
            _acceptedOneWay.ToDictionary(
                static pair => pair.Key,
                static pair => (IReadOnlyList<long>)pair.Value.ToArray(),
                StringComparer.Ordinal),
            new Dictionary<string, string>(
                _requestOperationIds,
                StringComparer.Ordinal),
            new Dictionary<string, string>(
                _oneWayOperationIds,
                StringComparer.Ordinal),
            _requestCorrelationIds.Keys.ToArray());
    }

    private async Task RunRequestPacerAsync(
        CancellationToken cancellationToken,
        ConcurrentBag<Task> operations)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(
            1d / Math.Max(1, operationsPerSecond)));
        var sequence = 0L;
        try
        {
            while (await timer.WaitForNextTickAsync(cancellationToken))
            {
                var current = Interlocked.Increment(ref sequence);
                Interlocked.Increment(ref _requestOffered);
                Interlocked.Increment(ref _requestSubmitted);
                operations.Add(ExecuteRequestAsync(current));
            }
        }
        catch (OperationCanceledException)
            when (cancellationToken.IsCancellationRequested)
        {
        }
    }

    private async Task RunOneWayPacerAsync(
        CancellationToken cancellationToken,
        ConcurrentBag<Task> operations)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(
            1d / Math.Max(1, operationsPerSecond)));
        var sequence = 0L;
        try
        {
            while (await timer.WaitForNextTickAsync(cancellationToken))
            {
                var current = Interlocked.Increment(ref sequence);
                Interlocked.Increment(ref _oneWayOffered);
                Interlocked.Increment(ref _oneWaySubmitted);
                operations.Add(ExecuteOneWayAsync(current));
            }
        }
        catch (OperationCanceledException)
            when (cancellationToken.IsCancellationRequested)
        {
        }
    }

    private async Task ExecuteRequestAsync(long sequence)
    {
        var (targetId, submittingNode, request) = CreateCall(sequence);
        var requestWatch = Stopwatch.StartNew();
        try
        {
            var reply = targetKind == "actor"
                ? await context.RequestActorWorkloadAsync(
                    submittingNode,
                    request)
                : await context.RequestSpotWorkloadAsync(
                    submittingNode,
                    request);
            requestWatch.Stop();
            if (reply.Sequence != sequence
                || reply.TargetId != targetId)
            {
                Interlocked.Increment(ref _requestFailed);
                return;
            }
            if (reply.OperationId != request.OperationId
                || !reply.WithinDeadline
                || reply.ObjectGeneration <= 0)
            {
                Interlocked.Increment(ref _requestFailed);
                return;
            }
            if (targetKind == "actor"
                && string.IsNullOrWhiteSpace(reply.CorrelationId))
            {
                Interlocked.Increment(ref _requestFailed);
                return;
            }
            if (reply.CorrelationId is { Length: > 0 }
                && !_requestCorrelationIds.TryAdd(
                    reply.CorrelationId,
                    0))
            {
                Interlocked.Increment(ref _requestFailed);
                return;
            }

            _requestLatencyMs.Enqueue(
                requestWatch.Elapsed.TotalMilliseconds);
            _acceptedRequests
                .GetOrAdd(
                    targetId,
                    static _ => new ConcurrentQueue<long>())
                .Enqueue(sequence);
            _requestOperationIds.TryAdd(
                OperationKey(targetId, sequence),
                request.OperationId);
            Interlocked.Increment(ref _requestSucceeded);
        }
        catch
        {
            Interlocked.Increment(ref _requestFailed);
        }
    }

    private async Task ExecuteOneWayAsync(long sequence)
    {
        var (targetId, submittingNode, request) = CreateCall(sequence);
        try
        {
            if (targetKind == "actor")
                await context.SendActorWorkloadAsync(
                    submittingNode,
                    request);
            else
                await context.SendSpotWorkloadAsync(
                    submittingNode,
                    request);
            _acceptedOneWay
                .GetOrAdd(
                    targetId,
                    static _ => new ConcurrentQueue<long>())
                .Enqueue(sequence);
            _oneWayOperationIds.TryAdd(
                OperationKey(targetId, sequence),
                request.OperationId);
            Interlocked.Increment(ref _oneWaySucceeded);
        }
        catch
        {
            Interlocked.Increment(ref _oneWayFailed);
        }
    }

    private (string TargetId, ZLinkHttpClient SubmittingNode,
        RelocationWorkloadCallReq Request) CreateCall(long sequence)
    {
        var targetId = targetIds[
            checked((int)((sequence - 1) % targetIds.Count))];
        var submittingNode = sequence % 2 == 0
            ? context.NodeB
            : context.NodeC;
        var sent =
            DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        return (
            targetId,
            submittingNode,
            new RelocationWorkloadCallReq(
                targetId,
                scenario,
                sequence,
                Guid.NewGuid().ToString("N"),
                sent,
                sent + 5_000));
    }

    internal static string OperationKey(
        string targetId,
        long sequence) =>
        targetId + "\n" + sequence;

    private static double Percentile(
        IEnumerable<double> source,
        double percentile)
    {
        var values = source.Order().ToArray();
        if (values.Length == 0)
            return 0;
        var index = (int)Math.Ceiling(
            percentile * values.Length) - 1;
        return values[Math.Clamp(index, 0, values.Length - 1)];
    }
}

internal sealed record RelocationBulkWorkloadResult(
    string Scenario,
    string TargetKind,
    TimeSpan Elapsed,
    long RequestOffered,
    long RequestSubmitted,
    long RequestSucceeded,
    long RequestFailed,
    long OneWayOffered,
    long OneWaySubmitted,
    long OneWaySucceeded,
    long OneWayFailed,
    double RequestP50Milliseconds,
    double RequestP95Milliseconds,
    double RequestP99Milliseconds,
    double RequestMaxMilliseconds,
    IReadOnlyDictionary<string, IReadOnlyList<long>>
        AcceptedRequests,
    IReadOnlyDictionary<string, IReadOnlyList<long>>
        AcceptedOneWay,
    IReadOnlyDictionary<string, string> RequestOperationIds,
    IReadOnlyDictionary<string, string> OneWayOperationIds,
    IReadOnlyList<string> RequestCorrelationIds)
{
    public double RequestsPerSecond =>
        RequestSucceeded / Math.Max(
            Elapsed.TotalSeconds,
            double.Epsilon);
}

internal static class RelocationBulkWorkloadVerification
{
    internal static async Task VerifyAsync(
        SpotActorTransferScenarioContext context,
        RelocationBulkWorkloadResult result)
    {
        ZlinkStreamAssert.Ensure(
            result.RequestOffered == result.RequestSubmitted
            && result.RequestSubmitted
               == result.RequestSucceeded + result.RequestFailed
            && result.OneWayOffered == result.OneWaySubmitted
            && result.OneWaySubmitted
               == result.OneWaySucceeded + result.OneWayFailed
            && result.RequestFailed == 0
            && result.OneWayFailed == 0,
            $"{result.Scenario} {result.TargetKind} traffic failed: "
            + $"requestOffered={result.RequestOffered};"
            + $"requestSubmitted={result.RequestSubmitted};"
            + $"request={result.RequestFailed};"
            + $"oneWayOffered={result.OneWayOffered};"
            + $"oneWaySubmitted={result.OneWaySubmitted};"
            + $"oneWay={result.OneWayFailed}.");

        IReadOnlyList<ActorEvidence> evidence = [];
        var deadline =
            DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        do
        {
            evidence = (await Task.WhenAll(
                    context.GetEvidenceAsync(context.NodeA),
                    context.GetEvidenceAsync(context.NodeB),
                    context.GetEvidenceAsync(context.NodeC)))
                .SelectMany(static items => items)
                .ToArray();
            if (HasAllOneWay(evidence, result))
                break;
            await Task.Delay(100);
        } while (DateTimeOffset.UtcNow < deadline);

        foreach (var (targetId, accepted) in
                 result.AcceptedRequests)
        {
            VerifyHandlerSet(
                evidence,
                result,
                targetId,
                accepted,
                "workload_request",
                result.RequestOperationIds);
        }
        foreach (var (targetId, accepted) in
                 result.AcceptedOneWay)
        {
            VerifyHandlerSet(
                evidence,
                result,
                targetId,
                accepted,
                "workload_one_way",
                result.OneWayOperationIds);
        }
    }

    private static void VerifyHandlerSet(
        IEnumerable<ActorEvidence> evidence,
        RelocationBulkWorkloadResult result,
        string targetId,
        IReadOnlyList<long> accepted,
        string kind,
        IReadOnlyDictionary<string, string> operationIds)
    {
        var handled = evidence
            .Where(item =>
                item.Scenario == result.Scenario
                && item.ActorId == targetId
                && item.Kind == kind)
            .Select(item => ParseHandlerEvidence(item.Value))
            .ToArray();
        ZlinkStreamAssert.Ensure(
            handled.Length == accepted.Count
            && handled.Select(static item => item.Sequence)
                   .Distinct().Count() == handled.Length
            && handled.Select(static item => item.Sequence)
                .Order().SequenceEqual(accepted.Order())
            && handled.All(item =>
                item.WithinDeadline
                && operationIds.TryGetValue(
                    RelocationBulkWorkload.OperationKey(
                        targetId,
                        item.Sequence),
                    out var expectedOperation)
                && expectedOperation == item.OperationId),
            $"{result.Scenario} {targetId} accepted {kind} "
            + "operation identity/deadline was lost or duplicated.");
    }

    private static (
        long Sequence,
        string OperationId,
        bool WithinDeadline) ParseHandlerEvidence(
        string value)
    {
        var fields = value.Split(';')
            .Select(static field => field.Split('=', 2))
            .Where(static field => field.Length == 2)
            .ToDictionary(
                static field => field[0],
                static field => field[1],
                StringComparer.Ordinal);
        return (
            long.Parse(fields["sequence"]),
            fields["operation"],
            bool.Parse(fields["withinDeadline"]));
    }

    private static bool HasAllOneWay(
        IEnumerable<ActorEvidence> evidence,
        RelocationBulkWorkloadResult result)
    {
        var snapshot = evidence.ToArray();
        return result.AcceptedOneWay.All(pair =>
            snapshot.Count(item =>
                item.Scenario == result.Scenario
                && item.ActorId == pair.Key
                && item.Kind == "workload_one_way")
            >= pair.Value.Count);
    }

    internal static void VerifyContinuity(
        RelocationBulkWorkloadResult baseline,
        RelocationBulkWorkloadResult relocation)
    {
        var minimumThroughput =
            baseline.RequestsPerSecond * 0.90;
        var maximumP99 = Math.Max(
            baseline.RequestP99Milliseconds * 2,
            250);
        ZlinkStreamAssert.Ensure(
            relocation.RequestsPerSecond >= minimumThroughput,
            $"{relocation.Scenario} throughput "
            + $"{relocation.RequestsPerSecond:F2}/s was below "
            + $"{minimumThroughput:F2}/s.");
        ZlinkStreamAssert.Ensure(
            relocation.RequestP99Milliseconds <= maximumP99,
            $"{relocation.Scenario} request p99 "
            + $"{relocation.RequestP99Milliseconds:F2} ms exceeded "
            + $"{maximumP99:F2} ms.");
    }

    internal static void Report(
        RelocationBulkWorkloadResult result)
    {
        var successRate = result.RequestSucceeded / Math.Max(
            1d,
            result.RequestSucceeded
            + result.RequestFailed) * 100;
        Console.WriteLine(
            $"{result.Scenario} kind={result.TargetKind}"
            + $" request_offered={result.RequestOffered}"
            + $" request_submitted={result.RequestSubmitted}"
            + $" requests={result.RequestSucceeded}"
            + $" request_errors={result.RequestFailed}"
            + $" one_way_offered={result.OneWayOffered}"
            + $" one_way_submitted={result.OneWaySubmitted}"
            + $" one_way_accepted={result.OneWaySucceeded}"
            + $" one_way_errors={result.OneWayFailed}"
            + $" success_rate={successRate:F3}%"
            + $" throughput={result.RequestsPerSecond:F2}/s"
            + $" p50_ms={result.RequestP50Milliseconds:F2}"
            + $" p95_ms={result.RequestP95Milliseconds:F2}"
            + $" p99_ms={result.RequestP99Milliseconds:F2}"
            + $" max_ms={result.RequestMaxMilliseconds:F2}"
            + $" correlation_count="
            + result.RequestCorrelationIds.Count);
    }

    internal static async Task VerifyRelocationTerminalsAsync(
        SpotActorTransferScenarioContext context,
        IReadOnlyList<RelocationLocationSnapshot> initial,
        IReadOnlyCollection<string> actorIds,
        IReadOnlyCollection<string> spotIds,
        IReadOnlyCollection<RelocationBulkWorkloadResult> traffic,
        bool requireSpotWideAggregatePublication)
    {
        var final = await context.GetRelocationLocationsAsync(
            context.NodeB,
            actorIds,
            spotIds);
        var initialByKey = initial.ToDictionary(LocationKey);
        var finalByKey = final.ToDictionary(LocationKey);
        ZlinkStreamAssert.Ensure(
            finalByKey.Count == actorIds.Count + spotIds.Count,
            "Relocation final location count is incomplete.");
        foreach (var (key, before) in initialByKey)
        {
            ZlinkStreamAssert.Ensure(
                finalByKey.TryGetValue(key, out var after)
                && after.ObjectGeneration
                   == before.ObjectGeneration
                && after.NodeRid != before.NodeRid,
                $"Relocation final owner/generation mismatch for "
                + $"{before.ObjectKind} '{before.ObjectId}'.");
        }

        var terminals = (await Task.WhenAll(
                context.GetRelocationTerminalsAsync(context.NodeA),
                context.GetRelocationTerminalsAsync(context.NodeB),
                context.GetRelocationTerminalsAsync(context.NodeC)))
            .SelectMany(static items => items)
            .ToArray();
        foreach (var after in final)
        {
            ZlinkStreamAssert.Ensure(
                terminals.Any(terminal =>
                    terminal.ObjectKind == after.ObjectKind
                    && terminal.ObjectId == after.ObjectId
                    && terminal.ObjectGeneration
                       == after.ObjectGeneration
                    && terminal.NodeRid == after.NodeRid),
                $"Relocation terminal callback evidence is missing for "
                + $"{after.ObjectKind} '{after.ObjectId}'.");
        }

        var handlerEvidence = (await Task.WhenAll(
                context.GetEvidenceAsync(context.NodeA),
                context.GetEvidenceAsync(context.NodeB),
                context.GetEvidenceAsync(context.NodeC)))
            .SelectMany(static items => items)
            .ToArray();
        foreach (var result in traffic)
        {
            foreach (var targetId in result.AcceptedRequests.Keys
                         .Concat(result.AcceptedOneWay.Keys)
                         .Distinct(StringComparer.Ordinal))
            {
                var kind = result.TargetKind;
                if (!finalByKey.TryGetValue(
                        kind + "\n" + targetId,
                        out var location))
                    continue;
                var terminal = terminals
                    .Where(item =>
                        item.ObjectKind == kind
                        && item.ObjectId == targetId
                        && item.NodeRid == location.NodeRid)
                    .OrderBy(static item =>
                        item.ObservedUnixTimeMilliseconds)
                    .First();
                var targetAdmissions = handlerEvidence.Where(item =>
                    item.Scenario == result.Scenario
                    && item.ActorId == targetId
                    && item.NodeRid == location.NodeRid
                    && item.Kind is "workload_request"
                        or "workload_one_way");
                ZlinkStreamAssert.Ensure(
                    targetAdmissions.All(item =>
                        item.ObservedUnixTimeMilliseconds
                        >= terminal.ObservedUnixTimeMilliseconds),
                    $"Target application admission preceded relocation "
                    + $"terminal for {kind} '{targetId}'.");
            }
        }

        var gaps = new List<string>
        {
            "authority_owner_generation_unobservable"
        };
        if (traffic.Any(result =>
                result.TargetKind == "spot"
                && result.RequestSucceeded > 0
                && result.RequestCorrelationIds.Count
                   != result.RequestSucceeded))
        {
            gaps.Add("spot_request_transport_correlation_unobservable");
        }
        if (requireSpotWideAggregatePublication)
        {
            var finalOwners = final
                .Select(static item => item.NodeRid)
                .Distinct(StringComparer.Ordinal)
                .ToArray();
            ZlinkStreamAssert.Ensure(
                finalOwners.Length == 1,
                "SpotWide member final owners diverged from the "
                + "aggregate Spot owner.");
            gaps.Add("spotwide_atomic_publication_unobservable");
        }

        throw new InvalidOperationException(
            "evidence_gap=" + string.Join(',', gaps)
            + "; public ActorRef/SpotRef omit authority owner generation, "
            + "Spot handlers omit transport correlation context, and "
            + "per-object FindAsync cannot prove aggregate CAS visibility.");
    }

    private static string LocationKey(
        RelocationLocationSnapshot item) =>
        item.ObjectKind + "\n" + item.ObjectId;
}

internal static class RelocationWorkloadEnvironment
{
    internal static int Count(string name, int canonical) =>
        PositiveInt(name, canonical);

    internal static TimeSpan Duration(
        string name,
        int canonicalSeconds) =>
        TimeSpan.FromSeconds(
            PositiveInt(name, canonicalSeconds));

    internal static int Rate(string name, int canonical) =>
        PositiveInt(name, canonical);

    private static int PositiveInt(
        string name,
        int fallback)
    {
        var raw = Environment.GetEnvironmentVariable(name);
        if (string.IsNullOrWhiteSpace(raw))
            return fallback;
        if (!int.TryParse(raw, out var value) || value <= 0)
            throw new InvalidOperationException(
                $"{name} must be a positive integer.");
        return value;
    }
}
