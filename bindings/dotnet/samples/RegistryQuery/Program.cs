using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static void Main(string[] args)
    {
        // --8<-- [start:doc]
        if (!SampleSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        var registry = ctx.CreateRegistry();
        var query = ctx.CreateRegistryQueryClient();
        var provider = ctx.CreatePubSocket();
        var discovery = ctx.CreateDiscovery(AutoConnectType.Fanout, "sample");
        string registryPub = SampleSupport.NewEndpoint("tcp", "sample");
        string registryRouter = SampleSupport.NewEndpoint("tcp", "sample");
        string serviceEndpoint = SampleSupport.NewEndpoint("tcp", "sample");

        registry.Bind(registryPub, registryRouter);
        discovery.ConnectRegistry(registryRouter);
        provider.AttachDiscovery(discovery);
        provider.Bind(serviceEndpoint);
        query.Connect(registryRouter);

        try
        {
            SampleSupport.WaitOrThrow(
                () => Array.Exists(query.Topology(),
                    entry => entry.ChannelName == "sample"),
                5000,
                "registry query sample");
            Console.WriteLine("[registry-query] service: \"sample\" -> snapshot: found");
        }
        finally
        {
            discovery.Dispose();
            query.Dispose();
            registry.Dispose();
        }
        // --8<-- [end:doc]
    }
}
