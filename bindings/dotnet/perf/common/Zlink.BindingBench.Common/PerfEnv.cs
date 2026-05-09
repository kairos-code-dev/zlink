using System;

public static class PerfEnv
{
    public static string ReadString(string name, string fallback)
    {
        string? value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? fallback : value.Trim();
    }

    public static int ReadPositive(string name, int fallback)
    {
        string? raw = Environment.GetEnvironmentVariable(name);
        return int.TryParse(raw, out int parsed) && parsed > 0
            ? parsed
            : fallback;
    }

    public static int ReadNonNegative(string name, int fallback)
    {
        string? raw = Environment.GetEnvironmentVariable(name);
        return int.TryParse(raw, out int parsed) && parsed >= 0
            ? parsed
            : fallback;
    }

    public static bool ReadBool(string name, bool fallback)
    {
        string? raw = Environment.GetEnvironmentVariable(name);
        if (string.IsNullOrWhiteSpace(raw))
            return fallback;
        return int.TryParse(raw.Trim(), out int parsed) ? parsed != 0 : fallback;
    }
}
