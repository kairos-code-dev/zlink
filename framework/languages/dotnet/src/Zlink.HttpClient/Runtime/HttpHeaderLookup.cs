/* SPDX-License-Identifier: MPL-2.0 */

namespace Zlink.HttpClient.Runtime;

internal static class HttpHeaderLookup
{
    public static string? Find(IReadOnlyDictionary<string, string> headers, string name)
    {
        if (headers.TryGetValue(name, out var direct)) return direct;

        foreach (var (key, value) in headers)
            if (key.Equals(name, StringComparison.OrdinalIgnoreCase))
                return value;

        return null;
    }

    public static IReadOnlyDictionary<string, string> Without(
        IReadOnlyDictionary<string, string> headers,
        params string[] names)
    {
        var copy = new Dictionary<string, string>(headers, StringComparer.OrdinalIgnoreCase);
        foreach (var name in names) copy.Remove(name);
        return copy;
    }
}
