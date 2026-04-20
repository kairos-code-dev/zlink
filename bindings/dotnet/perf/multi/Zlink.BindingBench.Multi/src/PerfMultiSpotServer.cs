using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfMultiSpotServer
{
    private const string Pattern = "SPOT";
    private const uint RunId = 1;
    private const string ServiceName = "bench-svc";
    private const string Topic = "bench";

    internal static int Run(PerfOptions options)
    {
        SpotServerConfig config = BuildConfig(options);
        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx, options);
        using var control = new RouterSocket(ctx);
        using var channelDealer = new DealerSocket(ctx);
        using var nodePub = new SpotNode(ctx);
        nodePub.AttachChannelDealerManual(ServiceName, channelDealer);
        using var spotPub = new Spot(nodePub);

        ApplyMultiSocketOptions(control, options);
        ConfigureTlsServerIfNeeded(control, config.Transport);
        ConfigureSpotTlsPublisherIfNeeded(nodePub, config.Transport);
        ConfigureSpotNodePublisher(nodePub, options);

        control.Bind(config.ControlEndpoint);
        nodePub.Bind(config.DataEndpoint);
        WriteStdoutLine($"READY,{config.DataEndpoint}|{config.ControlEndpoint}");
        WriteStdoutLine($"CONTROL_READY,{config.ControlEndpoint}");

        using var controlState = new SpotServerControlState();
        StartStopWatcher(controlState);

        if (!WaitForReadyCounts(control, controlState, config))
            return 2;

        BroadcastStart(control, controlState.ConnectedPeers, config.Size);
        return RunActivePhase(spotPub, controlState, config);
    }

    private static SpotServerConfig BuildConfig(PerfOptions options)
    {
        return new SpotServerConfig(
            options.Transport,
            Math.Max(1, options.Size),
            Math.Max(1, ResolveMultiDurationSeconds(options)),
            Math.Max(1, ResolveMultiClients(options)),
            ResolveMultiConnectReadyTimeoutMs(options),
            MultiEndpointFor(options.Transport, "multi-spot-data", options),
            MultiEndpointFor(options.Transport, "multi-spot-control", options));
    }

    private static void ConfigureSpotNodePublisher(SpotNode node,
        PerfOptions options)
    {
        int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
        int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
        int sndTimeo = ResolveMultiSndTimeoutMs(options);
        int rcvTimeo = ResolveMultiRcvTimeoutMs(options);
        int xpubNoDrop = options.SpotXpubNoDrop > 0 ? 1 : 0;

        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.Linger, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.SndHwm, sndHwm);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.SndTimeo, sndTimeo);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.XPubNoDrop, xpubNoDrop);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.Linger, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.RcvHwm, rcvHwm);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.RcvTimeo, rcvTimeo);
    }

    private static bool WaitForReadyCounts(RouterSocket control,
        SpotServerControlState state, SpotServerConfig config)
    {
        using var poller = new Poller();
        var events = new List<PollEvent>(1);
        poller.Add(control, PollEvents.PollIn);
        long deadlineTicks = DeadlineTicksFromMilliseconds(config.ReadyTimeoutMs);

        while (!state.StopRequested && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            int timeoutMs = Math.Max(1,
                (int)Math.Ceiling((deadlineTicks - Stopwatch.GetTimestamp())
                    * 1000.0 / Stopwatch.Frequency));
            if (!WaitForEvents(poller, events, timeoutMs))
                continue;
            if ((events[0].Revents & PollEvents.PollIn) == 0)
                continue;

            while (TryRecvControl(control, out Received? received))
            {
                Received controlMessage = received
                    ?? throw new InvalidOperationException("Control recv returned null.");
                using (controlMessage)
                {
                    if (controlMessage.RoutingId == null)
                        return false;

                    string line = controlMessage.SinglePartOrThrow().GetString();
                    RoutingId peer = controlMessage.RoutingId.Value;
                    state.NotePeer(peer);

                    if (string.Equals(line, "CONNECTED",
                            StringComparison.Ordinal))
                    {
                        continue;
                    }

                    if (!TryParseReadyCount(line, config.Size, out int count))
                        continue;

                    state.AddReadyUnits(count);
                    if (state.ReadyUnits >= config.ClientCount)
                        return true;
                }
            }
        }

        return false;
    }

    private static void BroadcastStart(RouterSocket control,
        IReadOnlyList<RoutingId> peers, int msgSize)
    {
        byte[] payload = Encoding.ASCII.GetBytes($"START,{msgSize}");
        for (int i = 0; i < peers.Count; i++)
        {
            using var message = Message.FromBytes(payload);
            control.Send(peers[i], message);
        }
    }

    private static int RunActivePhase(Spot spotPub,
        SpotServerControlState state, SpotServerConfig config)
    {
        ulong seq = 1;
        var payload = new byte[Math.Max(config.Size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        long deadlineTicks = DeadlineTicksFromSeconds(config.DurationSeconds);

        while (!state.StopRequested && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), RunId, (uint)PerfPhase.Active,
                config.Size, seq++, EpochNs());
            try
            {
                using var message = Message.FromBytes(payload);
                spotPub.Publish(ServiceName, Topic, message, SendFlags.DontWait);
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
                continue;
            }
        }

        return 0;
    }

    private static void StartStopWatcher(SpotServerControlState state)
    {
        var watcher = new Thread(static arg =>
        {
            var control = (SpotServerControlState)arg!;
            while (true)
            {
                string? line = Console.ReadLine();
                if (line == null)
                {
                    control.RequestStop();
                    return;
                }

                if (string.Equals(line, "STOP", StringComparison.OrdinalIgnoreCase)
                    || string.Equals(line, "QUIT",
                        StringComparison.OrdinalIgnoreCase))
                {
                    control.RequestStop();
                    return;
                }
            }
        });
        watcher.IsBackground = true;
        watcher.Start(state);
    }

    private static bool TryRecvControl(RouterSocket control,
        out Received? received)
    {
        try
        {
            received = control.Recv(RecvFlags.DontWait);
            return true;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            received = null;
            return false;
        }
    }

    private static bool TryParseReadyCount(string line, int msgSize, out int count)
    {
        count = 0;
        const string prefix = "READY_COUNT,";
        if (!line.StartsWith(prefix, StringComparison.Ordinal))
            return false;

        string[] parts = line.Split(',', StringSplitOptions.TrimEntries);
        if (parts.Length != 3)
            return false;
        if (!int.TryParse(parts[1], out int parsedSize) || parsedSize != msgSize)
            return false;
        if (!int.TryParse(parts[2], out count) || count <= 0)
            return false;
        return true;
    }

    private static void TrySetSpotNodeSocketOption(SpotNode node,
        SpotNodeSocketRole role, SocketOptionKey<int> option, int value)
    {
        try
        {
            node.SetOption(role, option, value);
        }
        catch (ZlinkException ex) when (ShouldIgnoreSpotOptionError(ex.InternalErrno))
        {
        }
    }

    private static bool ShouldIgnoreSpotOptionError(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.ENotSup
               || code == ErrorCode.EInval
               || code == ErrorCode.EProtoNoSupport;
    }

    private readonly struct SpotServerConfig
    {
        internal SpotServerConfig(string transport, int size,
            int durationSeconds, int clientCount, int readyTimeoutMs,
            string dataEndpoint, string controlEndpoint)
        {
            Transport = transport;
            Size = size;
            DurationSeconds = durationSeconds;
            ClientCount = clientCount;
            ReadyTimeoutMs = readyTimeoutMs;
            DataEndpoint = dataEndpoint;
            ControlEndpoint = controlEndpoint;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int DurationSeconds { get; }
        internal int ClientCount { get; }
        internal int ReadyTimeoutMs { get; }
        internal string DataEndpoint { get; }
        internal string ControlEndpoint { get; }
    }

    private sealed class SpotServerControlState : IDisposable
    {
        private readonly List<RoutingId> _connectedPeers = new();
        private int _stopRequested;

        internal IReadOnlyList<RoutingId> ConnectedPeers => _connectedPeers;
        internal int ReadyUnits { get; private set; }
        internal bool StopRequested => Volatile.Read(ref _stopRequested) != 0;

        internal void AddReadyUnits(int count)
        {
            ReadyUnits += count;
        }

        internal void NotePeer(RoutingId peer)
        {
            for (int i = 0; i < _connectedPeers.Count; i++)
            {
                if (_connectedPeers[i].Equals(peer))
                    return;
            }

            _connectedPeers.Add(peer);
        }

        internal void RequestStop()
        {
            Volatile.Write(ref _stopRequested, 1);
        }

        public void Dispose()
        {
        }
    }
}
