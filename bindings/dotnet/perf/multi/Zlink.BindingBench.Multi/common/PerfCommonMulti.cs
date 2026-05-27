using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading;
using Systems.Zlink;

internal static partial class PerfRunner
{
    // Wire-level stop token shared with single perf and other bindings.
    // PERF_MULTI_TEST_POLICY § 1.3.1 / PERF_SINGLE_TEST_POLICY § 1.4.
    internal static readonly byte[] MultiStopToken = StopToken.Bytes;

    internal const int MaxStreamFrameBytes = 16 * 1024 * 1024;

    internal enum PerfPhase : byte
    {
        Warmup = 0,
        Active = 1,
        Cooldown = 2,
    }

    internal static string NormalizePerfPattern(string pattern)
    {
        return PerfShared.NormalizePattern(pattern, trimMultiPrefix: true);
    }

    internal static int ResolveMultiClients(PerfOptions options)
    {
        return options.Clients;
    }

    internal static int ResolveMultiDurationSeconds(PerfOptions options)
    {
        return options.DurationSeconds;
    }

    internal static int ResolveMultiSndTimeoutMs(PerfOptions options)
    {
        return options.SndTimeoutMs;
    }

    internal static int ResolveMultiRcvTimeoutMs(PerfOptions options)
    {
        return options.RcvTimeoutMs;
    }

    internal static int ResolveMultiConnectReadyTimeoutMs(PerfOptions options)
    {
        return options.ConnectReadyTimeoutMs;
    }

    // PERF_MULTI_TEST_POLICY § 1.3.1: client/server poller wait timeouts
    // are unconditionally -1 (signal-driven wait). Deadlines are tracked
    // by application clock; phase end / shutdown is signaled either by
    // stdin STOP (server) or wire-level stop token (client/receiver).
    internal const int MultiClientPollTimeoutMs = -1;

    internal static int ResolveMultiClientPollTimeoutMs(PerfOptions options)
    {
        _ = options;
        return MultiClientPollTimeoutMs;
    }

    internal static int ResolveMultiSpotControlStabilizeMs()
    {
        return PerfEnv.ReadNonNegative("PERF_MULTI_SPOT_CONTROL_STABILIZE_MS",
            1000);
    }

    internal static int ResolveMultiSpotControlSettleMs()
    {
        return PerfEnv.ReadNonNegative("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25);
    }

    internal static string MultiEndpointFor(string transport, string name,
        PerfOptions options)
    {
        int bindPort = options.ServerBindPort;
        if (bindPort > 0)
            return $"{transport}://127.0.0.1:{bindPort}";
        return EndpointFor(transport, name);
    }

    // ITEM 3 (MULTI_SPOT fix): the registry API exposes no resolved
    // endpoint readback after a wildcard bind, so a "tcp://127.0.0.1:*"
    // bind cannot be reused as the connect target (discovery.ConnectRegistry
    // and the READY advertisement need a concrete address). Reserve a
    // concrete free TCP port up front so the SAME endpoint string is valid
    // for registry.Bind, the server's own discovery.ConnectRegistry, and the
    // client (via the READY line). Mirrors C, whose registry endpoints are
    // concrete addresses the discovery layer reuses verbatim.
    internal static int ReserveFreeTcpPort()
    {
        var listener = new System.Net.Sockets.TcpListener(
            System.Net.IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            return ((System.Net.IPEndPoint)listener.LocalEndpoint).Port;
        }
        finally
        {
            listener.Stop();
        }
    }

    // ITEM 3 (MULTI_SPOT fix): zlink_discovery_connect_registry is
    // non-blocking; immediately after registry.Bind the registry PUB socket
    // is not yet connectable and the call returns EAGAIN (errno 11, code
    // 604). C's discovery layer tolerates this via its connect/retry loop;
    // reproduce that here with a bounded retry so MULTI_SPOT is actually
    // exercised instead of being misreported as UNSUPPORTED.
    internal static void ConnectRegistryWithRetry(IDiscovery discovery,
        string registryPubEndpoint, int timeoutMs)
    {
        int budget = Math.Max(2000, timeoutMs);
        var sw = Stopwatch.StartNew();
        ZlinkException? last = null;
        while (sw.ElapsedMilliseconds < budget)
        {
            try
            {
                discovery.ConnectRegistry(registryPubEndpoint);
                return;
            }
            catch (ZlinkException ex) when (PerfShared.IsWouldBlock(
                       ex.InternalErrno) || PerfShared.IsInterrupted(
                       ex.InternalErrno))
            {
                last = ex;
                Thread.Sleep(20);
            }
        }
        if (last != null)
            throw last;
        discovery.ConnectRegistry(registryPubEndpoint);
    }

    internal static string MultiRegistryEndpoint(string transport,
        PerfOptions options)
    {
        int bindPort = options.ServerBindPort;
        if (bindPort > 0)
            return $"{transport}://127.0.0.1:{bindPort}";
        // Preserve the original data-plane transport scheme (parity with the
        // prior MultiEndpointFor behaviour) but pin a concrete loopback port
        // so the Bind endpoint string can be reused verbatim for
        // discovery.ConnectRegistry and the READY advertisement.
        return $"{transport}://127.0.0.1:{ReserveFreeTcpPort()}";
    }

    internal static string BindSpotNodeWithRetry(ISpotNode node,
        string transport, string endpointName, PerfOptions options)
    {
        const int maxAttempts = 8;
        for (int attempt = 1; attempt <= maxAttempts; attempt++)
        {
            string endpoint = MultiEndpointFor(transport, endpointName,
                options);
            try
            {
                node.SetPubBind(endpoint);
                return node.LastEndpoint;
            }
            catch (ZlinkException ex) when (options.ServerBindPort <= 0
                                            && IsAddressInUse(ex.InternalErrno)
                                            && attempt < maxAttempts)
            {
                Thread.Sleep(10 * attempt);
            }
        }

        string finalEndpoint = MultiEndpointFor(transport, endpointName,
            options);
        node.SetPubBind(finalEndpoint);
        return node.LastEndpoint;
    }

    private static bool IsAddressInUse(int errno)
    {
        return errno is 98 or 48 or 502;
    }

    internal static string MultiSpotChannelName(string registryEndpoint)
    {
        var builder = new StringBuilder("bench-svc");
        foreach (char ch in registryEndpoint)
        {
            if (char.IsLetterOrDigit(ch))
            {
                builder.Append(char.ToLowerInvariant(ch));
            }
            else if (builder[^1] != '-')
            {
                builder.Append('-');
            }
        }

        return builder.ToString().TrimEnd('-');
    }

    internal static bool IsMonitorReady(MonitorEventType eventValue)
    {
        return eventValue == (MonitorEventType)SocketEvent.ConnectionReady;
    }

    internal static bool WaitConnectReadyCount(MonitorSocket monitor,
        int expectedReady, int timeoutMs)
    {
        if (expectedReady <= 0)
            return true;

        int readyCount = DrainReadyEvents(monitor);
        if (readyCount >= expectedReady)
            return true;

        using var readyPoller = new MonitorReadyPoller();
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
                return false;

            int rc = readyPoller.Poll(
                new System.Collections.Generic.List<MonitorSocket> { monitor },
                new[] { 0 }, 1, deadlineTicks, nowTicks);
            if (rc < 0)
                return false;
            if (rc == 0)
                continue;

            try
            {
                readyCount += DrainReadyEvents(monitor);
                if (readyCount >= expectedReady)
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }
            catch
            {
                return false;
            }
        }
    }

    internal static bool IsStopTokenPayload(ReadOnlySpan<byte> payload)
    {
        return StopToken.IsStopToken(payload);
    }

    internal static bool ManualSocketOverridesEnabled()
    {
        return PerfEnv.ReadPositive("PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES", 0) > 0
            || PerfEnv.ReadPositive("PERF_ALLOW_MANUAL_SOCKET_OVERRIDES", 0) > 0;
    }

    internal static AutoHwmProfile ResolveContextAutoHwmProfile()
    {
        string value = PerfEnv.ReadString("PERF_CTX_AUTO_HWM_PROFILE", string.Empty);
        if (string.IsNullOrWhiteSpace(value))
            value = PerfEnv.ReadString("PERF_AUTO_HWM_PROFILE", string.Empty);

        return value switch
        {
            "compact" => AutoHwmProfile.Compact,
            "low_latency" or "low-latency" => AutoHwmProfile.LowLatency,
            "throughput" => AutoHwmProfile.Throughput,
            _ => AutoHwmProfile.Balanced,
        };
    }

    internal static void ApplyMultiServerContextOptions(IContext ctx,
        PerfOptions options)
    {
        if (options.IoThreads > 0)
            ctx.Options.IoThreads = options.IoThreads;

        if (options.MaxSockets > 0)
            ctx.Options.MaxSockets = options.MaxSockets;

        ctx.Options.Blocky = PerfEnv.ReadBool("PERF_CTX_BLOCKY", false);
        ctx.Options.AutoHwmEnabled =
            PerfEnv.ReadNonNegative("PERF_CTX_AUTO_HWM_ENABLE", 1) != 0;
        ctx.Options.AutoHwmProfile = ResolveContextAutoHwmProfile();
    }

    internal static void ApplyMultiClientContextOptions(IContext ctx,
        PerfOptions options)
    {
        ApplyMultiServerContextOptions(ctx, options);
    }

    internal static void ApplyMultiSocketOptions(ISocket socket,
        PerfOptions options)
    {
        int sndTimeo = ResolveMultiSndTimeoutMs(options);
        int rcvTimeo = ResolveMultiRcvTimeoutMs(options);

        socket.Options.Linger = TimeSpan.Zero;
        if (ManualSocketOverridesEnabled())
        {
            int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
            int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
            if (sndHwm > 0)
                socket.Options.SendHighWaterMark = sndHwm;
            if (rcvHwm > 0)
                socket.Options.ReceiveHighWaterMark = rcvHwm;
            if (options.MultiSndBuf > 0)
                socket.Options.SendBufferSize = options.MultiSndBuf;
            if (options.MultiRcvBuf > 0)
                socket.Options.ReceiveBufferSize = options.MultiRcvBuf;
        }
        socket.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeo);
        socket.Options.ReceiveTimeout = TimeSpan.FromMilliseconds(rcvTimeo);
    }

    internal static void ApplyMultiSpotSocketOptions(ISpot spot,
        PerfOptions options)
    {
        int sndTimeo = ResolveMultiSndTimeoutMs(options);
        int rcvTimeo = ResolveMultiRcvTimeoutMs(options);

        spot.Linger = TimeSpan.Zero;
        if (ManualSocketOverridesEnabled())
        {
            int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
            int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
            if (sndHwm > 0)
                spot.SendHighWaterMark = sndHwm;
            if (rcvHwm > 0)
                spot.ReceiveHighWaterMark = rcvHwm;
            if (options.MultiSndBuf > 0)
                spot.SendBufferSize = options.MultiSndBuf;
            if (options.MultiRcvBuf > 0)
                spot.ReceiveBufferSize = options.MultiRcvBuf;
        }
        spot.SendTimeout = TimeSpan.FromMilliseconds(sndTimeo);
        spot.ReceiveTimeout = TimeSpan.FromMilliseconds(rcvTimeo);
    }

    internal static void ApplyAutoHwmMsgUnit(IContext ctx, int msgSize)
    {
        if (msgSize <= 0)
            return;
        try
        {
            ctx.Options.AutoHwmMessageUnitBytes = msgSize;
        }
        catch (ZlinkException)
        {
        }
    }

    internal static void RecalculateAutoHwm(IContext ctx)
    {
        try
        {
            ctx.RecalculateAutoHwm();
        }
        catch (ZlinkException)
        {
        }
    }

    private static readonly object AutoHwmDetailLock = new();
    private static readonly HashSet<string> AutoHwmDetailSeen =
        new(StringComparer.Ordinal);

    internal static bool AutoHwmDetailEnabled()
    {
        string value = PerfEnv.ReadString("PERF_MULTI_PRINT_AUTO_HWM_DETAIL",
            string.Empty);
        if (string.IsNullOrEmpty(value))
            value = PerfEnv.ReadString("PERF_PRINT_AUTO_HWM_DETAIL",
                string.Empty);
        return string.IsNullOrEmpty(value)
            || !string.Equals(value, "0", StringComparison.Ordinal);
    }

    internal static void PrintAutoHwmSnapshot(ISocket socket, string label,
        string transport, int msgSize)
    {
        if (!AutoHwmDetailEnabled())
            return;

        SocketType socketType = ResolveSocketType(socket);
        MonitorSnapshot? snapshot = TryReadSocketMonitorSnapshot(socket);
        bool snapshotFromMonitor = snapshot != null;
        AutoHwmSnapshotFields fields = snapshotFromMonitor
            ? AutoHwmSnapshotFields.FromSnapshot(snapshot!)
            : AutoHwmSnapshotFields.FromSocketOptions(socket);

        PrintAutoHwmDetailLine(label, transport, msgSize,
            SocketTypeName(socketType),
            snapshotFromMonitor ? "monitor_snapshot" : "option_fallback",
            fields);
    }

    internal static void PrintSpotNodeAutoHwmSnapshot(ISpotNode node,
        string label, string transport, int msgSize)
    {
        if (!AutoHwmDetailEnabled())
            return;

        SpotNodeSocketSnapshotEntry[] entries;
        try
        {
            entries = node.InternalSocketsSnapshot();
        }
        catch (ZlinkException)
        {
            return;
        }

        bool controlOnly = label.Contains("control",
            StringComparison.OrdinalIgnoreCase);
        foreach (SpotNodeSocketSnapshotEntry entry in entries)
        {
            if (!entry.AutoHwmVisible)
                continue;
            if (controlOnly
                && entry.SocketName != "peer_ctrl_pub"
                && entry.SocketName != "peer_ctrl_sub")
            {
                continue;
            }

            MonitorSnapshot snapshot = entry.Snapshot;
            var fields = AutoHwmSnapshotFields.FromSnapshot(snapshot);
            string owner = entry.Owner == SpotNodeSocketOwner.Node
                ? "node"
                : "spot";
            string scope = entry.Owner == SpotNodeSocketOwner.Node
                ? "shared"
                : "per-spot";
            string socketType = SpotNodeSocketTypeName(entry.SocketType);
            string pattern = AutoHwmEnvOrDefault("PERF_MULTI_PATTERN",
                "unknown");
            string component = AutoHwmEnvOrDefault("PERF_MULTI_COMPONENT",
                "process");
            string detail =
                "AUTO_HWM_DETAIL"
                + $",pattern={pattern}"
                + $",transport={transport}"
                + $",component={component}"
                + $",label={entry.SocketName}"
                + $",owner={owner}"
                + $",owner_id={entry.OwnerId}"
                + $",socket={entry.SocketName}"
                + $",socket_type={socketType}"
                + $",msg_size={msgSize}"
                + ",source=spotnode_snapshot"
                + $",enabled={BoolInt(snapshot.AutoHwmEnabled)}"
                + $",role={AutoHwmRoleName(snapshot.AutoHwmRole)}"
                + $",role_id={snapshot.AutoHwmRole}"
                + $",profile={AutoHwmProfileName(snapshot.AutoHwmProfile)}"
                + $",profile_id={snapshot.AutoHwmProfile}"
                + $",policy_class={AutoHwmPolicyClassName(snapshot.AutoHwmPolicyClass)}"
                + $",policy_class_id={snapshot.AutoHwmPolicyClass}"
                + $",unit_budget_bytes={snapshot.AutoHwmUnitBudgetBytes}"
                + $",size_cap={snapshot.AutoHwmSizeCap}"
                + $",scope={scope}"
                + $",sndhwm={fields.SndHwm}"
                + $",rcvhwm={fields.RcvHwm}"
                + $",socket_message_slots={snapshot.AutoHwmSocketMessageSlots}"
                + $",effective_message_bytes={snapshot.AutoHwmEffectiveMessageBytes}"
                + $",effective_sndbuf={AutoHwmSndbufDisplay(socketType, snapshot.AutoHwmRole, fields.EffectiveSndbuf)}"
                + $",effective_rcvbuf={AutoHwmRcvbufDisplay(socketType, snapshot.AutoHwmRole, fields.EffectiveRcvbuf)}"
                + $",last_recalc_reason={AutoHwmRecalcReasonName(snapshot.AutoHwmLastRecalcReason)}";

            WriteAutoHwmDetailLine(detail);
        }
    }

    private static void PrintAutoHwmDetailLine(string label, string transport,
        int msgSize, string socketType, string source, AutoHwmSnapshotFields fields)
    {
        string pattern = AutoHwmEnvOrDefault("PERF_MULTI_PATTERN", "unknown");
        string component = AutoHwmEnvOrDefault("PERF_MULTI_COMPONENT",
            "process");
        string resolvedTransport = string.IsNullOrEmpty(transport)
            ? AutoHwmEnvOrDefault("PERF_MULTI_TRANSPORT", "unknown")
            : transport;
        string resolvedLabel = string.IsNullOrEmpty(label) ? "socket" : label;

        string detail =
            "AUTO_HWM_DETAIL"
            + $",pattern={pattern}"
            + $",transport={resolvedTransport}"
            + $",component={component}"
            + $",label={resolvedLabel}"
            + $",socket_type={socketType}"
            + $",msg_size={msgSize}"
            + $",source={source}"
            + $",enabled={BoolInt(fields.Enabled)}"
            + $",role={AutoHwmRoleName(fields.Role)}"
            + $",role_id={fields.Role}"
            + $",profile={AutoHwmProfileName(fields.Profile)}"
            + $",profile_id={fields.Profile}"
            + $",policy_class={AutoHwmPolicyClassName(fields.PolicyClass)}"
            + $",policy_class_id={fields.PolicyClass}"
            + $",unit_budget_bytes={fields.UnitBudgetBytes}"
            + $",size_cap={fields.SizeCap}"
            + $",sndhwm={fields.SndHwm}"
            + $",rcvhwm={fields.RcvHwm}"
            + $",socket_message_slots={fields.SocketMessageSlots}"
            + $",effective_message_bytes={fields.EffectiveMessageBytes}"
            + $",effective_sndbuf={AutoHwmSndbufDisplay(socketType, fields.Role, fields.EffectiveSndbuf)}"
            + $",effective_rcvbuf={AutoHwmRcvbufDisplay(socketType, fields.Role, fields.EffectiveRcvbuf)}"
            + $",last_recalc_ms={fields.LastRecalcMs}"
            + $",last_recalc_reason={AutoHwmRecalcReasonName(fields.LastRecalcReason)}"
            + $",send_blocked_ratio_ppm={fields.SendBlockedRatioPpm}"
            + $",deferred_sndhwm={fields.DeferredSndHwm}"
            + $",deferred_rcvhwm={fields.DeferredRcvHwm}";

        WriteAutoHwmDetailLine(detail);
    }

    private static void WriteAutoHwmDetailLine(string detail)
    {
        lock (AutoHwmDetailLock)
        {
            if (!AutoHwmDetailSeen.Add(detail))
                return;
        }
        WriteStdoutLine(detail);
    }

    private static MonitorSnapshot? TryReadSocketMonitorSnapshot(ISocket socket)
    {
        try
        {
            using ISocketMonitor monitor = socket.MonitorOpen(SocketEvent.All);
            return monitor.Snapshot();
        }
        catch (Exception ex) when (ex is ZlinkException
                                  || ex is ObjectDisposedException
                                  || ex is InvalidOperationException)
        {
            return null;
        }
    }

    private static int ReadSocketOption(Func<int> getter)
    {
        try
        {
            return getter();
        }
        catch (ZlinkException)
        {
            return 0;
        }
    }

    private static SocketType ResolveSocketType(ISocket socket)
        => socket switch
        {
            IDealerSocket => SocketType.Dealer,
            IRouterSocket => SocketType.Router,
            IPubSocket => SocketType.Pub,
            ISubSocket => SocketType.Sub,
            IStreamSocket => SocketType.Stream,
            _ => SocketType.Any,
        };

    private static string SocketTypeName(SocketType type)
        => type switch
        {
            SocketType.Pair => "pair",
            SocketType.Pub => "pub",
            SocketType.Sub => "sub",
            SocketType.Dealer => "dealer",
            SocketType.Router => "router",
            SocketType.XPub => "xpub",
            SocketType.XSub => "xsub",
            SocketType.Stream => "stream",
            _ => "any",
        };

    private static string SpotNodeSocketTypeName(SpotNodeSocketType type)
        => type switch
        {
            SpotNodeSocketType.Pair => "pair",
            SpotNodeSocketType.Pub => "pub",
            SpotNodeSocketType.Sub => "sub",
            SpotNodeSocketType.Dealer => "dealer",
            SpotNodeSocketType.Router => "router",
            SpotNodeSocketType.XPub => "xpub",
            SpotNodeSocketType.XSub => "xsub",
            SpotNodeSocketType.Stream => "stream",
            _ => "any",
        };

    private static string AutoHwmEnvOrDefault(string name, string fallback)
    {
        string value = PerfEnv.ReadString(name, string.Empty);
        return string.IsNullOrEmpty(value) ? fallback : value;
    }

    private static int BoolInt(bool value) => value ? 1 : 0;

    // Byte-identical port of perf_multi_runtime.hpp
    // perf_auto_hwm_send_side_visible / perf_auto_hwm_recv_side_visible.
    // A SUB/XSUB carrying recv_ingress|control never sends; a PUB/XPUB
    // carrying spot_data|control never receives. The C multi benchmark
    // emits effective_sndbuf=0 / effective_rcvbuf=0 for those inactive
    // directions (sndhwm/rcvhwm stay raw), so the AUTO_HWM_DETAIL line and
    // the resulting report tables match the C reference exactly.
    private static bool AutoHwmSendSideVisible(string socketType, uint role)
    {
        string roleName = AutoHwmRoleName(role);
        if ((socketType == "sub" || socketType == "xsub")
            && (roleName == "recv_ingress" || roleName == "control"))
            return false;
        return true;
    }

    private static bool AutoHwmRecvSideVisible(string socketType, uint role)
    {
        string roleName = AutoHwmRoleName(role);
        if ((socketType == "pub" || socketType == "xpub")
            && (roleName == "spot_data" || roleName == "control"))
            return false;
        return true;
    }

    private static string AutoHwmSndbufDisplay(string socketType, uint role,
        int effectiveSndbuf)
        => AutoHwmSendSideVisible(socketType, role)
            ? $"{effectiveSndbuf}"
            : "0";

    private static string AutoHwmRcvbufDisplay(string socketType, uint role,
        int effectiveRcvbuf)
        => AutoHwmRecvSideVisible(socketType, role)
            ? $"{effectiveRcvbuf}"
            : "0";

    private static string AutoHwmRoleName(uint role)
        => role switch
        {
            1 => "control",
            2 => "routed",
            3 => "fanout",
            4 => "recv_ingress",
            5 => "spot_data",
            6 => "peer_queue",
            7 => "stream",
            _ => "none",
        };

    private static string AutoHwmProfileName(uint profile)
        => profile switch
        {
            0 => "compact",
            1 => "low_latency",
            2 => "balanced",
            3 => "throughput",
            _ => "unknown",
        };

    private static string AutoHwmPolicyClassName(uint policyClass)
        => policyClass switch
        {
            1 => "fanout",
            2 => "spot_data",
            3 => "recv_ingress",
            4 => "routed",
            5 => "peer_queue",
            6 => "stream",
            7 => "control",
            _ => "none",
        };

    private static string AutoHwmRecalcReasonName(uint reason)
        => reason switch
        {
            1 => "initial",
            2 => "role_change",
            3 => "policy_toggle",
            4 => "refresh",
            5 => "deferred_shrink",
            _ => "none",
        };

    private readonly struct AutoHwmSnapshotFields
    {
        private AutoHwmSnapshotFields(bool enabled, uint profile, uint role,
            uint policyClass, ulong unitBudgetBytes, uint sizeCap,
            ulong socketMessageSlots, ulong effectiveMessageBytes,
            int sndHwm, int rcvHwm, int effectiveSndbuf,
            int effectiveRcvbuf, ulong lastRecalcMs, uint lastRecalcReason,
            uint sendBlockedRatioPpm, int deferredSndHwm, int deferredRcvHwm)
        {
            Enabled = enabled;
            Profile = profile;
            Role = role;
            PolicyClass = policyClass;
            UnitBudgetBytes = unitBudgetBytes;
            SizeCap = sizeCap;
            SocketMessageSlots = socketMessageSlots;
            EffectiveMessageBytes = effectiveMessageBytes;
            SndHwm = sndHwm;
            RcvHwm = rcvHwm;
            EffectiveSndbuf = effectiveSndbuf;
            EffectiveRcvbuf = effectiveRcvbuf;
            LastRecalcMs = lastRecalcMs;
            LastRecalcReason = lastRecalcReason;
            SendBlockedRatioPpm = sendBlockedRatioPpm;
            DeferredSndHwm = deferredSndHwm;
            DeferredRcvHwm = deferredRcvHwm;
        }

        internal bool Enabled { get; }
        internal uint Profile { get; }
        internal uint Role { get; }
        internal uint PolicyClass { get; }
        internal ulong UnitBudgetBytes { get; }
        internal uint SizeCap { get; }
        internal ulong SocketMessageSlots { get; }
        internal ulong EffectiveMessageBytes { get; }
        internal int SndHwm { get; }
        internal int RcvHwm { get; }
        internal int EffectiveSndbuf { get; }
        internal int EffectiveRcvbuf { get; }
        internal ulong LastRecalcMs { get; }
        internal uint LastRecalcReason { get; }
        internal uint SendBlockedRatioPpm { get; }
        internal int DeferredSndHwm { get; }
        internal int DeferredRcvHwm { get; }

        internal static AutoHwmSnapshotFields FromSnapshot(
            MonitorSnapshot snapshot)
            => new(snapshot.AutoHwmEnabled, snapshot.AutoHwmProfile,
                snapshot.AutoHwmRole, snapshot.AutoHwmPolicyClass,
                snapshot.AutoHwmUnitBudgetBytes, snapshot.AutoHwmSizeCap,
                snapshot.AutoHwmSocketMessageSlots,
                snapshot.AutoHwmEffectiveMessageBytes,
                snapshot.AutoHwmAppliedSndHwm,
                snapshot.AutoHwmAppliedRcvHwm,
                snapshot.AutoHwmEffectiveSndbuf,
                snapshot.AutoHwmEffectiveRcvbuf,
                snapshot.AutoHwmLastRecalcMs,
                snapshot.AutoHwmLastRecalcReason,
                snapshot.AutoHwmSendBlockedRatioPpm,
                snapshot.AutoHwmDeferredSndHwm,
                snapshot.AutoHwmDeferredRcvHwm);

        internal static AutoHwmSnapshotFields FromSocketOptions(ISocket socket)
            => new(false, 0, 0, 0, 0, 0, 0, 0,
                ReadSocketOption(() => socket.Options.SendHighWaterMark),
                ReadSocketOption(() => socket.Options.ReceiveHighWaterMark),
                ReadSocketOption(() => socket.Options.SendBufferSize),
                ReadSocketOption(() => socket.Options.ReceiveBufferSize),
                0, 0, 0, 0, 0);
    }

    internal static int ResolveMultiLatencySampleCap(PerfOptions options)
    {
        return options.LatencySampleCap;
    }

    internal static int ResolveMultiOnewayLatencySampleStride()
    {
        return PerfEnv.ReadPositive("PERF_MULTI_ONEWAY_LATENCY_SAMPLE_STRIDE",
            PerfEnv.ReadPositive("PERF_MULTI_SPOT_LATENCY_SAMPLE_STRIDE", 32));
    }

    internal static bool IsCoreStreamServerTransport(string transport)
    {
        return transport.Equals("tcp", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("ws", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("wss", StringComparison.OrdinalIgnoreCase);
    }

}
