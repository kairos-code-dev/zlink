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
        var rawTotal = Sum(eligible, weight);
        if (rawTotal <= 0)
            return null;
        var divisor = CommonDivisor(eligible, weight);
        var total = rawTotal / divisor;
        var ordinal = Interlocked.Increment(ref cursor);
        // Traverse the weighted ring with a step that is coprime to its size.
        // This preserves the exact ratio over one full ring while avoiding a
        // contiguous burst of up to `weight` selections for one candidate.
        var step = CoprimeStep(total);
        var selected = (long)(((ulong)ordinal * (ulong)step) % (ulong)total);
        foreach (var candidate in eligible)
        {
            var candidateWeight = weight(candidate) / divisor;
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

    private static long CoprimeStep(long total)
    {
        if (total <= 2)
            return 1;

        var step = (total / 2) + 1;
        while (GreatestCommonDivisor(step, total) != 1)
            step++;
        return step;
    }

    private static int CommonDivisor<T>(
        IEnumerable<T> eligible,
        Func<T, int> weight)
    {
        var divisor = 0L;
        foreach (var candidate in eligible)
        {
            var candidateWeight = weight(candidate);
            if (candidateWeight <= 0)
                continue;
            divisor = divisor == 0
                ? candidateWeight
                : GreatestCommonDivisor(divisor, candidateWeight);
            if (divisor == 1)
                return 1;
        }

        return checked((int)divisor);
    }

    private static long GreatestCommonDivisor(long left, long right)
    {
        while (right != 0)
        {
            var remainder = left % right;
            left = right;
            right = remainder;
        }
        return left;
    }
}
