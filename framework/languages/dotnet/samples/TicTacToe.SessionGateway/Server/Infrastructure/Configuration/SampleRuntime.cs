using TicTacToe.SessionGateway.Infrastructure;

namespace TicTacToe.SessionGateway.Infrastructure.Configuration;

public static class SampleRuntime
{
    public static IRegistryDiscoveryMetadata OpenRegistryMetadata()
    {
        return new FileRegistryDiscoveryMetadata(MetadataDirectory);
    }

    public static string MetadataDirectory
    {
        get
        {
            var value = Environment.GetEnvironmentVariable("TICTACTOE_SG_METADATA_DIR");
            return string.IsNullOrWhiteSpace(value)
                ? Path.Combine(Path.GetTempPath(), "zlink-tictactoe-session-gateway-metadata")
                : value;
        }
    }
}
