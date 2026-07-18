namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Lua sources for the Actor transfer authority (spec 41 §3.1). Every state
/// transition is one atomic script: prepare fences a single active transfer per
/// actor, commit/activate/abort walk the prepared → committed → activated /
/// aborted state machine under a recovery-lease-owner guard, and takeover moves
/// an expired recovery lease to a successor. Each script reads Redis TIME itself
/// so recovery-lease expiry and UpdatedAt use the store clock, never a caller's
/// wall clock.
///
/// Key layout under prefix P (actorRowKey is the length-prefixed MeshName +
/// Actor ID, transferId is the lowercase 8-4-4-4-12 UUID string):
///   P:transfer:{actorRowKey}:{transferId}  HASH  full transfer record fields
///   P:transfer-by-actor:{actorRowKey}      STRING active transfer id
///
/// The HASH stores the record as fields state, source, target,
/// expectedActorGeneration, expectedMembershipEpoch, participants,
/// recoveryOwnerId, recoveryLeaseExpiresAtMs, updatedAtMs. Transitions that
/// succeed return {'stored', {9 record fields}}; expected races return one of
/// {'notfound'|'stale'|'conflict'|'invalid'}.
/// </summary>
internal static class ZLinkRedisLocationActorTransferScripts
{
    private const string Prologue = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        local function readRecord()
            return {
                redis.call('HGET', KEYS[1], 'state'),
                redis.call('HGET', KEYS[1], 'source'),
                redis.call('HGET', KEYS[1], 'target'),
                redis.call('HGET', KEYS[1], 'expectedActorGeneration'),
                redis.call('HGET', KEYS[1], 'expectedMembershipEpoch'),
                redis.call('HGET', KEYS[1], 'participants'),
                redis.call('HGET', KEYS[1], 'recoveryOwnerId'),
                redis.call('HGET', KEYS[1], 'recoveryLeaseExpiresAtMs'),
                redis.call('HGET', KEYS[1], 'updatedAtMs')
            }
        end
        """;

    /// <summary>
    /// KEYS[1] transfer HASH, KEYS[2] active index.
    /// ARGV[1] transfer id, ARGV[2] source json, ARGV[3] target json,
    /// ARGV[4] expected actor generation, ARGV[5] expected membership epoch,
    /// ARGV[6] participants json, ARGV[7] recovery owner id,
    /// ARGV[8] recovery lease TTL milliseconds.
    /// </summary>
    internal const string Prepare = Prologue + """

        local active = redis.call('GET', KEYS[2])
        if active then
            -- Exactly one active transfer per actor. A repeat of the same id
            -- while still Prepared is idempotent; anything else is a race.
            if active == ARGV[1] and redis.call('HGET', KEYS[1], 'state') == 'Prepared' then
                return {'stored', readRecord()}
            end
            return {'conflict'}
        end

        local expiresAt = nowMs + tonumber(ARGV[8])
        redis.call('HSET', KEYS[1],
            'state', 'Prepared',
            'source', ARGV[2],
            'target', ARGV[3],
            'expectedActorGeneration', ARGV[4],
            'expectedMembershipEpoch', ARGV[5],
            'participants', ARGV[6],
            'recoveryOwnerId', ARGV[7],
            'recoveryLeaseExpiresAtMs', expiresAt,
            'updatedAtMs', nowMs)
        redis.call('SET', KEYS[2], ARGV[1])
        return {'stored', readRecord()}
        """;

    /// <summary>
    /// Prepared → Committed under a recovery-lease-owner guard.
    /// KEYS[1] transfer HASH, KEYS[2] active index.
    /// ARGV[1] transfer id, ARGV[2] recovery owner id.
    /// </summary>
    internal const string Commit = Prologue + """

        if redis.call('EXISTS', KEYS[1]) == 0 or redis.call('GET', KEYS[2]) ~= ARGV[1] then
            return {'notfound'}
        end
        if redis.call('HGET', KEYS[1], 'state') ~= 'Prepared' then return {'invalid'} end
        if redis.call('HGET', KEYS[1], 'recoveryOwnerId') ~= ARGV[2] then return {'conflict'} end
        redis.call('HSET', KEYS[1], 'state', 'Committed', 'updatedAtMs', nowMs)
        return {'stored', readRecord()}
        """;

    /// <summary>
    /// Committed → Activated; clears the active index so the actor can be
    /// transferred again. KEYS/ARGV as Commit.
    /// </summary>
    internal const string Activate = Prologue + """

        if redis.call('EXISTS', KEYS[1]) == 0 then return {'notfound'} end
        if redis.call('HGET', KEYS[1], 'state') ~= 'Committed' then return {'invalid'} end
        if redis.call('HGET', KEYS[1], 'recoveryOwnerId') ~= ARGV[2] then return {'conflict'} end
        redis.call('HSET', KEYS[1], 'state', 'Activated', 'updatedAtMs', nowMs)
        if redis.call('GET', KEYS[2]) == ARGV[1] then redis.call('DEL', KEYS[2]) end
        return {'stored', readRecord()}
        """;

    /// <summary>
    /// Prepared → Aborted; preserves the source owner fence and clears the
    /// active index. KEYS/ARGV as Commit.
    /// </summary>
    internal const string Abort = Prologue + """

        if redis.call('EXISTS', KEYS[1]) == 0 then return {'notfound'} end
        if redis.call('HGET', KEYS[1], 'state') ~= 'Prepared' then return {'invalid'} end
        if redis.call('HGET', KEYS[1], 'recoveryOwnerId') ~= ARGV[2] then return {'conflict'} end
        redis.call('HSET', KEYS[1], 'state', 'Aborted', 'updatedAtMs', nowMs)
        if redis.call('GET', KEYS[2]) == ARGV[1] then redis.call('DEL', KEYS[2]) end
        return {'stored', readRecord()}
        """;

    /// <summary>
    /// Moves an expired recovery lease to a successor owner. Only a still-live
    /// (Prepared or Committed) transfer whose recovery lease already expired can
    /// be taken over. KEYS[1] transfer HASH, KEYS[2] active index.
    /// ARGV[1] transfer id, ARGV[2] successor owner id, ARGV[3] TTL ms.
    /// </summary>
    internal const string TakeOver = Prologue + """

        if redis.call('EXISTS', KEYS[1]) == 0 then return {'notfound'} end
        local state = redis.call('HGET', KEYS[1], 'state')
        if state ~= 'Prepared' and state ~= 'Committed' then return {'invalid'} end
        if tonumber(redis.call('HGET', KEYS[1], 'recoveryLeaseExpiresAtMs')) > nowMs then
            return {'conflict'}
        end
        local expiresAt = nowMs + tonumber(ARGV[3])
        redis.call('HSET', KEYS[1],
            'recoveryOwnerId', ARGV[2],
            'recoveryLeaseExpiresAtMs', expiresAt,
            'updatedAtMs', nowMs)
        return {'stored', readRecord()}
        """;
}
