using System;

if (args.Length >= 4 && string.Equals(args[0], "--multi-server",
    StringComparison.OrdinalIgnoreCase))
{
    var pattern = args[1].ToUpperInvariant();
    var transport = args[2];
    if (!int.TryParse(args[3], out var size))
        return 1;
    return PerfRunner.RunMultiServer(pattern, transport, size);
}

if (args.Length >= 6 && string.Equals(args[0], "--multi-client",
    StringComparison.OrdinalIgnoreCase))
{
    var pattern = args[1].ToUpperInvariant();
    var transport = args[2];
    if (!int.TryParse(args[3], out var size))
        return 1;

    string endpoint = string.Empty;
    for (int i = 4; i + 1 < args.Length; i++)
    {
        if (string.Equals(args[i], "--endpoint",
            StringComparison.OrdinalIgnoreCase))
        {
            endpoint = args[i + 1];
            break;
        }
    }
    if (string.IsNullOrWhiteSpace(endpoint))
        return 1;

    return PerfRunner.RunMultiClient(pattern, transport, size, endpoint);
}

return 1;
