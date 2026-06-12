using DeliveryDispatch.Shared.Contracts;

namespace DeliveryDispatch.Server.Configuration;

public sealed class EvidenceStore
{
    private readonly string _path;
    private readonly object _gate = new();

    public EvidenceStore()
    {
        var directory = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_WORK_DIR")
            ?? Path.Combine(Path.GetTempPath(), "zlink-deliverydispatch");
        Directory.CreateDirectory(directory);
        _path = Path.Combine(directory, "events.log");
    }

    public void Append(DeliveryStatusChanged status)
    {
        var line = string.Join(
            "|",
            status.DeliveryId,
            status.Status,
            status.CourierId ?? string.Empty,
            status.OccurredAt.ToUnixTimeMilliseconds().ToString());
        lock (_gate)
        {
            File.AppendAllLines(_path, [line]);
        }
    }

    public string[] ReadLines()
    {
        lock (_gate)
        {
            return File.Exists(_path)
                ? File.ReadAllLines(_path)
                : [];
        }
    }

    public bool HasSequence(string deliveryId, params DeliveryStatus[] expected)
    {
        var statuses = ReadLines()
            .Select(Parse)
            .Where(status => string.Equals(status.DeliveryId, deliveryId, StringComparison.Ordinal))
            .Select(status => status.Status)
            .ToArray();
        return statuses.SequenceEqual(expected);
    }

    private static DeliveryStatusChanged Parse(string line)
    {
        var parts = line.Split('|');
        return new DeliveryStatusChanged(
            parts[0],
            Enum.Parse<DeliveryStatus>(parts[1]),
            string.IsNullOrEmpty(parts[2]) ? null : parts[2],
            DateTimeOffset.FromUnixTimeMilliseconds(long.Parse(parts[3])));
    }
}
