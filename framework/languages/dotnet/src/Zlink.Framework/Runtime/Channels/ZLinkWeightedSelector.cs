namespace Zlink.Framework.Runtime.Channels;

internal static class ZLinkWeightedSelector
{
    internal static T? Select<T>(
        IReadOnlyList<T> eligible,
        Func<T, int> weight,
        ref long cursor)
        where T : class
    {
        if (eligible.Count == 0)
            return null;
        var total = Sum(eligible, weight);
        if (total <= 0)
            return null;
        var ordinal = Interlocked.Increment(ref cursor);
        var selected = (long)((ulong)ordinal % (ulong)total);
        foreach (var candidate in eligible)
        {
            var candidateWeight = weight(candidate);
            if (candidateWeight <= 0)
                continue;
            if (selected < candidateWeight)
                return candidate;
            selected -= candidateWeight;
        }
        throw new InvalidOperationException(
            "Weighted selection did not select an eligible target.");
    }

    internal static long Sum<T>(
        IEnumerable<T> eligible,
        Func<T, int> weight) =>
        eligible.Aggregate(
            0L,
            (sum, candidate) => checked(sum + weight(candidate)));
}
