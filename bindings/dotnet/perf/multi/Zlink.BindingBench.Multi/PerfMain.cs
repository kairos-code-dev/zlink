if (!PerfOptions.TryParseMultiArgs(args, out PerfOptions options))
    return 1;

if (!PerfRunner.IsSupportedTransport(options.Transport))
    return 1;

return options.ExecutionKind == PerfExecutionKind.MultiServer
    ? PerfRunner.RunMultiServer(options.Pattern, options.Transport, options.Size)
    : PerfRunner.RunMultiClient(options.Pattern, options.Transport, options.Size,
        options.Endpoint, options.ControlEndpoint);
