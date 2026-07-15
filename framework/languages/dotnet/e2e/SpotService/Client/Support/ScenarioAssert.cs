namespace SpotService.Client.Support;

internal static class ScenarioAssert
{
    public static int FindIndex(string[] lines, string pattern)
    {
        for (var i = 0; i < lines.Length; i++)
            if (lines[i].Contains(pattern, StringComparison.Ordinal))
                return i;

        return -1;
    }
}
