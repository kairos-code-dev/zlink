using System.Globalization;

namespace Zlink.Framework.Runtime.Configuration;

internal static class ZLinkApplicationHwmResolver
{
    private static readonly string[] RootCgroupLimitFiles =
    [
        "/sys/fs/cgroup/memory.max",
        "/sys/fs/cgroup/memory/memory.limit_in_bytes"
    ];

    public static ulong Resolve(ZLinkInboundDispatchOptionsModel options)
    {
        if (options.ApplicationHwmBytes is { } configured)
            return configured;

        var memoryLimit = options.ProcessMemoryLimitBytes ?? ReadCgroupMemoryLimit();
        if (memoryLimit is not { } finiteLimit)
            throw new ZLinkConfigurationException(
                "ApplicationHwmBytes uses Auto sizing, but no finite process memory limit was configured or detected.");

        var percent = options.ApplicationHwmProfile switch
        {
            ZLinkApplicationHwmProfile.Compact => 2UL,
            ZLinkApplicationHwmProfile.LowLatency => 5UL,
            ZLinkApplicationHwmProfile.Balanced => 10UL,
            ZLinkApplicationHwmProfile.Throughput => 20UL,
            _ => throw new ZLinkConfigurationException(
                $"Unknown ApplicationHwmProfile value '{(int)options.ApplicationHwmProfile}'.")
        };

        // Compute floor(limit * percent / 100) without overflowing UInt64.
        var hwm = finiteLimit / 100UL * percent
                  + finiteLimit % 100UL * percent / 100UL;
        if (hwm == 0)
            throw new ZLinkConfigurationException(
                "Application Auto HWM must resolve to a positive finite byte value.");
        return hwm;
    }

    internal static ulong? ReadCgroupMemoryLimit()
    {
        foreach (var path in EnumerateCgroupLimitFiles())
        {
            string value;
            try
            {
                if (!File.Exists(path)) continue;
                value = File.ReadAllText(path).Trim();
            }
            catch (IOException)
            {
                continue;
            }
            catch (UnauthorizedAccessException)
            {
                continue;
            }

            if (string.Equals(value, "max", StringComparison.OrdinalIgnoreCase))
                continue;
            if (!ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var limit)
                || limit == 0)
                continue;

            // cgroup v1 reports values near Int64.MaxValue when no finite limit exists.
            if (limit >= 0x7FFF_FFFF_FFFF_0000UL)
                continue;
            return limit;
        }

        return null;
    }

    private static IEnumerable<string> EnumerateCgroupLimitFiles()
    {
        string[] membershipLines = [];
        try
        {
            if (File.Exists("/proc/self/cgroup"))
                membershipLines = File.ReadAllLines("/proc/self/cgroup");
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }

        foreach (var line in membershipLines)
        {
            var fields = line.Split(':', 3);
            if (fields.Length != 3) continue;
            var relative = fields[2].TrimStart('/');
            if (fields[0] == "0")
                yield return Path.Combine("/sys/fs/cgroup", relative, "memory.max");
            else if (fields[1].Split(',').Contains("memory", StringComparer.Ordinal))
                yield return Path.Combine(
                    "/sys/fs/cgroup/memory",
                    relative,
                    "memory.limit_in_bytes");
        }

        foreach (var path in RootCgroupLimitFiles)
            yield return path;
    }
}
