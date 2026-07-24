using System.Globalization;
using System.Text;
using System.Text.Json;
using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis;

// Redis Actor transfer authority (spec 41 §3.1, gap 90 §12.29). Each transition
// runs one Lua script (ZLinkRedisLocationActorTransferScripts) so participant
// CAS, the single-active-transfer index and the recovery lease are fenced by
// the store clock. A crashed coordinator's Prepared/Committed transfer is
// recoverable once its recovery lease expires, via TakeOver.
public sealed partial class ZLinkRedisLocationStore
{
    public async ValueTask<ZLinkActorTransferWriteResult> PrepareActorTransferAsync(
        ZLinkActorTransferPrepareRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        var actorRowKey = EncodeActorTransferRowKey(request.MeshName, request.ActorId);
        var transferId = FormatTransferId(request.TransferId);
        var ttlMs = Math.Max(1L, (long)request.RecoveryLeaseTtl.TotalMilliseconds);
        return await ExecuteAsync(
                async database =>
                {
                    var raw = (RedisResult[])(await database.ScriptEvaluateAsync(
                        ZLinkRedisLocationActorTransferScripts.Prepare,
                        [_keys.TransferHashKey(actorRowKey, transferId), _keys.TransferByActorKey(actorRowKey)],
                        [
                            transferId,
                            ZLinkRedisActorTransferCodec.SerializeActorRef(request.Source),
                            ZLinkRedisActorTransferCodec.SerializeActorRef(request.Target),
                            request.ExpectedActorGeneration.ToString(CultureInfo.InvariantCulture),
                            request.ExpectedMembershipEpoch.ToString(CultureInfo.InvariantCulture),
                            ZLinkRedisActorTransferCodec.SerializeParticipants(request.Participants),
                            request.RecoveryOwnerId,
                            ttlMs
                        ]).ConfigureAwait(false))!;
                    return ToTransferResult(raw, request.MeshName, request.ActorId, request.TransferId);
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask<ZLinkActorTransferWriteResult> CommitActorTransferAsync(
        string meshName, string actorId, Guid transferId, string recoveryOwnerId,
        CancellationToken cancellationToken = default) =>
        GuardedTransitionAsync(
            ZLinkRedisLocationActorTransferScripts.Commit,
            meshName, actorId, transferId, recoveryOwnerId, cancellationToken);

    public ValueTask<ZLinkActorTransferWriteResult> ActivateActorTransferAsync(
        string meshName, string actorId, Guid transferId, string recoveryOwnerId,
        CancellationToken cancellationToken = default) =>
        GuardedTransitionAsync(
            ZLinkRedisLocationActorTransferScripts.Activate,
            meshName, actorId, transferId, recoveryOwnerId, cancellationToken);

    public ValueTask<ZLinkActorTransferWriteResult> AbortActorTransferAsync(
        string meshName, string actorId, Guid transferId, string recoveryOwnerId,
        CancellationToken cancellationToken = default) =>
        GuardedTransitionAsync(
            ZLinkRedisLocationActorTransferScripts.Abort,
            meshName, actorId, transferId, recoveryOwnerId, cancellationToken);

    public async ValueTask<ZLinkActorTransferWriteResult> TakeOverActorTransferAsync(
        string meshName, string actorId, Guid transferId, string successorOwnerId,
        TimeSpan recoveryLeaseTtl, CancellationToken cancellationToken = default)
    {
        var actorRowKey = EncodeActorTransferRowKey(meshName, actorId);
        var transfer = FormatTransferId(transferId);
        var ttlMs = Math.Max(1L, (long)recoveryLeaseTtl.TotalMilliseconds);
        return await ExecuteAsync(
                async database =>
                {
                    var raw = (RedisResult[])(await database.ScriptEvaluateAsync(
                        ZLinkRedisLocationActorTransferScripts.TakeOver,
                        [_keys.TransferHashKey(actorRowKey, transfer), _keys.TransferByActorKey(actorRowKey)],
                        [transfer, successorOwnerId, ttlMs]).ConfigureAwait(false))!;
                    return ToTransferResult(raw, meshName, actorId, transferId);
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkActorTransferRecord?> ResolveActorTransferAsync(
        string meshName, string actorId, CancellationToken cancellationToken = default)
    {
        var actorRowKey = EncodeActorTransferRowKey(meshName, actorId);
        return await ExecuteAsync(
                async database =>
                {
                    var active = await database.StringGetAsync(_keys.TransferByActorKey(actorRowKey))
                        .ConfigureAwait(false);
                    if (active.IsNullOrEmpty) return null;
                    var fields = await database.HashGetAsync(
                            _keys.TransferHashKey(actorRowKey, (string)active!),
                            ZLinkRedisActorTransferCodec.HashFields)
                        .ConfigureAwait(false);
                    return ZLinkRedisActorTransferCodec.Materialize(
                        meshName, actorId, Guid.ParseExact((string)active!, "D"), fields);
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorTransferWriteResult> GuardedTransitionAsync(
        string script, string meshName, string actorId, Guid transferId, string recoveryOwnerId,
        CancellationToken cancellationToken)
    {
        var actorRowKey = EncodeActorTransferRowKey(meshName, actorId);
        var transfer = FormatTransferId(transferId);
        return await ExecuteAsync(
                async database =>
                {
                    var raw = (RedisResult[])(await database.ScriptEvaluateAsync(
                        script,
                        [_keys.TransferHashKey(actorRowKey, transfer), _keys.TransferByActorKey(actorRowKey)],
                        [transfer, recoveryOwnerId]).ConfigureAwait(false))!;
                    return ToTransferResult(raw, meshName, actorId, transferId);
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static ZLinkActorTransferWriteResult ToTransferResult(
        RedisResult[] raw, string meshName, string actorId, Guid transferId)
    {
        var status = (string)raw[0]!;
        return status switch
        {
            "stored" => ZLinkActorTransferWriteResult.Stored(
                ZLinkRedisActorTransferCodec.Materialize(
                    meshName, actorId, transferId,
                    Array.ConvertAll((RedisResult[])raw[1]!, static field => (RedisValue)field))),
            "notfound" => ZLinkActorTransferWriteResult.NotFound,
            "conflict" => ZLinkActorTransferWriteResult.RejectedConflict,
            "invalid" => ZLinkActorTransferWriteResult.InvalidState,
            _ => ZLinkActorTransferWriteResult.IgnoredStale
        };
    }

    // Length-prefixed MeshName + Actor ID (spec 41 §3.1). Kept consistent with
    // the .NET canonical key encoder; UTF-8-byte-length parity with the
    // cross-language fixtures is tracked with the actor-row shape gap (90 §12.27).
    private static string EncodeActorTransferRowKey(string meshName, string actorId) =>
        new StringBuilder()
            .Append(meshName.Length.ToString(CultureInfo.InvariantCulture)).Append(':').Append(meshName)
            .Append(actorId.Length.ToString(CultureInfo.InvariantCulture)).Append(':').Append(actorId)
            .ToString();

    private static string FormatTransferId(Guid transferId) => transferId.ToString("D");
}

/// <summary>
/// Encodes and decodes the Actor transfer record's Redis fields: canonical
/// ActorRef JSON ({nodeRid, actorId, generation} camelCase) and the sorted
/// participant hex array (spec 41 §3.1).
/// </summary>
internal static class ZLinkRedisActorTransferCodec
{
    internal static readonly RedisValue[] HashFields =
    [
        "state", "source", "target", "expectedActorGeneration", "expectedMembershipEpoch",
        "participants", "recoveryOwnerId", "recoveryLeaseExpiresAtMs", "updatedAtMs"
    ];

    internal static string SerializeActorRef(ActorRef value)
    {
        var buffer = new System.Buffers.ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            writer.WriteString("nodeRid", value.NodeRid.IsEmpty ? string.Empty : value.NodeRid.ToHex());
            writer.WriteString("actorId", value.ActorId);
            writer.WriteNumber("generation", value.Generation);
            writer.WriteEndObject();
        }

        return Encoding.UTF8.GetString(buffer.WrittenSpan);
    }

    internal static ActorRef ParseActorRef(string json)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        var nodeHex = root.GetProperty("nodeRid").GetString() ?? string.Empty;
        return new ActorRef(
            string.IsNullOrEmpty(nodeHex) ? default : RoutingId.FromHex(nodeHex),
            root.GetProperty("actorId").GetString() ?? throw new JsonException("ActorRef.actorId is required."),
            root.GetProperty("generation").GetUInt64());
    }

    internal static string SerializeParticipants(IReadOnlySet<string> participants)
    {
        var values = participants
            .OrderBy(static value => value, StringComparer.Ordinal)
            .ToArray();
        return JsonSerializer.Serialize(values);
    }

    internal static IReadOnlySet<string> ParseParticipants(string json)
    {
        var values = JsonSerializer.Deserialize<string[]>(json) ?? [];
        var set = new HashSet<string>(StringComparer.Ordinal);
        foreach (var value in values)
            if (!string.IsNullOrEmpty(value))
                set.Add(value);
        return set;
    }

    internal static ZLinkActorTransferRecord Materialize(
        string meshName, string actorId, Guid transferId, RedisValue[] fields)
    {
        return new ZLinkActorTransferRecord(
            meshName,
            actorId,
            transferId,
            ParseActorRef((string)fields[1]!),
            ParseActorRef((string)fields[2]!),
            ulong.Parse((string)fields[3]!, CultureInfo.InvariantCulture),
            ulong.Parse((string)fields[4]!, CultureInfo.InvariantCulture),
            ParseParticipants((string)fields[5]!),
            ParseState((string)fields[0]!),
            (string)fields[6]!,
            DateTimeOffset.FromUnixTimeMilliseconds((long)fields[7]),
            DateTimeOffset.FromUnixTimeMilliseconds((long)fields[8]));
    }

    private static ZLinkActorTransferState ParseState(string state) => state switch
    {
        "Prepared" => ZLinkActorTransferState.Prepared,
        "Committed" => ZLinkActorTransferState.Committed,
        "Activated" => ZLinkActorTransferState.Activated,
        "Aborted" => ZLinkActorTransferState.Aborted,
        _ => throw new InvalidOperationException($"Unknown Actor transfer state '{state}'.")
    };
}
