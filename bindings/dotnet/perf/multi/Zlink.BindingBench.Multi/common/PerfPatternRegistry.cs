internal static class MultiPerfPatternRegistry
{
    private static readonly PerfPatternRegistry Registry = new(
        new IPerfPattern[]
        {
            new MultiPerfPattern("DEALER_DEALER",
                static options => PerfDealerDealerServer.Run(options),
                static options => PerfDealerDealerClient.Run(options)),
            new MultiPerfPattern("DEALER_ROUTER",
                static options => PerfDealerRouterServer.Run(options),
                static options => PerfDealerRouterClient.Run(options)),
            new MultiPerfPattern("ROUTER_ROUTER",
                static options => PerfRouterRouterServer.Run(options),
                static options => PerfRouterRouterClient.Run(options)),
            new MultiPerfPattern("PUBSUB",
                static options => PerfPubSubServer.Run(options),
                static options => PerfPubSubClient.Run(options)),
            new MultiPerfPattern("SPOT",
                static options => PerfSpotServer.Run(options),
                static options => PerfSpotClient.Run(options)),
            new MultiPerfPattern("STREAM",
                static options => PerfStreamServer.Run(options),
                static options => PerfRunner.PrintUnsupported(options.Pattern,
                    options.Transport, options.Size, "stream_client_external")),
        });

    internal static bool TryGet(string pattern, out IPerfPattern perfPattern)
    {
        return Registry.TryGet(pattern, out perfPattern);
    }

    private sealed class MultiPerfPattern : IPerfPattern
    {
        private readonly System.Func<PerfOptions, int> _runServer;
        private readonly System.Func<PerfOptions, int> _runClient;

        internal MultiPerfPattern(string name,
            System.Func<PerfOptions, int> runServer,
            System.Func<PerfOptions, int> runClient)
        {
            Name = name;
            _runServer = runServer;
            _runClient = runClient;
        }

        public string Name { get; }

        public int RunSingle(PerfOptions options)
        {
            _ = options;
            return 1;
        }

        public int RunMultiServer(PerfOptions options)
        {
            return _runServer(options);
        }

        public int RunMultiClient(PerfOptions options)
        {
            return _runClient(options);
        }
    }
}
