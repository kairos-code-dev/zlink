using StoreFailure.Shared;
using Zlink.HttpClient;

namespace StoreFailure.Client.Support;

/// <summary>
/// Reads the public runtime-query surface of a node over HTTP and waits
/// for expected states. Every SF verdict goes through these endpoints,
/// the request path, or evidence — never framework internals.
/// </summary>
internal static class SfProbe
{
    public static async Task<RuntimeStatusRes> GetStatusAsync(ZLinkHttpClient node)
    {
        return (await node.Get("/query/status").SubmitAsync<RuntimeStatusRes>()).Body;
    }

    public static async Task<PeerRowRes[]?> TryGetPeersAsync(ZLinkHttpClient node)
    {
        try
        {
            return (await node.Get("/query/peers").SubmitAsync<PeerRowRes[]>()).Body;
        }
        catch
        {
            // 503: the store is unreachable, so the raw row list is too.
            return null;
        }
    }

    public static async Task<PeerRowRes[]> WaitPeersAsync(
        ZLinkHttpClient node,
        Func<PeerRowRes[], bool> accept,
        TimeSpan timeout,
        string failure)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        PeerRowRes[]? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            last = await TryGetPeersAsync(node);
            if (last is not null && accept(last)) return last;

            await Task.Delay(150);
        }

        var seen = last is null
            ? "<store unreachable>"
            : string.Join(", ", last.Select(row => $"{row.Rid}@{row.Endpoint}"));
        throw new InvalidOperationException($"{failure} (last peers: {seen})");
    }

    public static async Task<RuntimeStatusRes> WaitStatusAsync(
        ZLinkHttpClient node,
        Func<RuntimeStatusRes, bool> accept,
        TimeSpan timeout,
        string failure)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        RuntimeStatusRes? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            last = await GetStatusAsync(node);
            if (accept(last)) return last;

            await Task.Delay(150);
        }

        throw new InvalidOperationException(
            $"{failure} (last status: healthy={last?.StoreHealthy}, lease={last?.OwnerLeaseHealthy}, error={last?.LastError})");
    }

    public static bool HasRid(PeerRowRes[] peers, string rid) =>
        peers.Any(row => string.Equals(row.Rid, rid, StringComparison.Ordinal));

    /// <summary>
    /// One request through the consumer with a bounded per-request
    /// timeout and no consumer-side retry: the strict probe for "the
    /// established path still works right now".
    /// </summary>
    public static async Task<ProfileRes> RequestAsync(
        ZLinkHttpClient consumer,
        string marker,
        int timeoutMilliseconds = 3000)
    {
        return (await consumer.Post($"/profile/request/timeout/{timeoutMilliseconds}")
            .Body(new ProfileReq("fast", marker))
            .SubmitAsync<ProfileRes>()).Body;
    }

    /// <summary>
    /// Sends requests at a steady cadence for a window and asserts every
    /// one of them succeeded — the fail-static "no interruption" check.
    /// </summary>
    public static async Task<IReadOnlyList<ProfileRes>> DriveRequestsAsync(
        ZLinkHttpClient consumer,
        string markerPrefix,
        TimeSpan window,
        string scenario)
    {
        var replies = new List<ProfileRes>();
        var deadline = DateTimeOffset.UtcNow + window;
        var index = 0;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var marker = $"{markerPrefix}-{index++}";
            var reply = await RequestAsync(consumer, marker);
            ScenarioAssert.That(
                reply.Value == "profile:fast",
                $"{scenario}: request '{marker}' returned an unexpected value.");
            replies.Add(reply);
            await Task.Delay(150);
        }

        ScenarioAssert.That(replies.Count > 0, $"{scenario}: the request window produced no traffic.");
        return replies;
    }
}
