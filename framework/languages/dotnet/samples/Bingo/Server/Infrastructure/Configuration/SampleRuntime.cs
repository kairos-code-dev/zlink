using Bingo.Server.Infrastructure;

namespace Bingo.Server.Infrastructure.Configuration;

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
            var value = Environment.GetEnvironmentVariable("BINGO_METADATA_DIR");
            return string.IsNullOrWhiteSpace(value)
                ? Path.Combine(Path.GetTempPath(), "zlink-bingo-sample-metadata")
                : value;
        }
    }
}
