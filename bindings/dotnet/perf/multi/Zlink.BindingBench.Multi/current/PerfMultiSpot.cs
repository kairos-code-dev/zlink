using System;
using System.Collections.Generic;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    private static int RunMultiSpotServer(string transport, int size)
    {
        size = Math.Max(1, size);
        int rcvTimeoutMs = ParsePositiveEnv("PERF_MULTI_RCVTIMEO_MS", 5000);
        int readyTimeoutMs =
            ParsePositiveEnv("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);
        string endpoint = MultiEndpointFor(transport, "multi-MULTI_SPOT");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Dealer);
        ApplyMultiSocketOptions(server);
        ConfigureTlsServerIfNeeded(server, transport);

        using var monitor = server.MonitorOpen(
            SocketEvent.ConnectionReady
            | SocketEvent.Accepted
            | SocketEvent.Connected);

        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        if (!WaitMonitorReady(monitor, readyTimeoutMs, true))
            return 2;

        var payload =
            new byte[Math.Max(256, Math.Max(size, MultiStopToken.Length))];

        while (true)
        {
            int n = ReceiveRetry(server, payload.AsSpan(), ReceiveFlags.None);
            if (n <= 0)
                continue;

            ReadOnlySpan<byte> body = payload.AsSpan(0, n);
            if (IsStopTokenPayload(body))
                break;
        }

        return 0;
    }

    private static int RunMultiSpotClient(string transport, int size,
        string endpoint)
    {
        size = Math.Max(1, size);
        int warmupSeconds = ParseNonNegativeEnv("PERF_MULTI_WARMUP_SECONDS", 3);
        int durationSeconds = ParsePositiveEnv("PERF_MULTI_DURATION_SECONDS", 5);
        int settleMs = ParseNonNegativeEnv("PERF_MULTI_SETTLE_MS", 500);
        int drainMs = ResolveMultiDrainMs("MULTI_SPOT");
        int sizeTransitionDrainMs = ParseNonNegativeEnv(
            "PERF_MULTI_SIZE_TRANSITION_DRAIN_MS", 300);
        bool activeWarmup =
            ParseNonNegativeEnv("PERF_MULTI_ACTIVE_WARMUP", 0) == 1;
        int warmupDrainMs = ParseNonNegativeEnv("PERF_MULTI_WARMUP_DRAIN_MS",
            Math.Max(drainMs, 1000));
        int sndTimeoutMs = ParsePositiveEnv("PERF_MULTI_SNDTIMEO_MS", 5000);
        int rcvTimeoutMs = ParsePositiveEnv("PERF_MULTI_RCVTIMEO_MS", 5000);
        int readyTimeoutMs =
            ParsePositiveEnv("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);

        int defaultClients = 1000;
        int clientCount = Math.Max(1,
            ParsePositiveEnv("PERF_MULTI_CLIENTS", defaultClients));

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        var clients = new List<Zlink.Socket>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new Zlink.Socket(ctx, Zlink.SocketType.Dealer);
                ApplyMultiSocketOptions(client);
                ConfigureTlsClientIfNeeded(client, transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
                var monitor = client.MonitorOpen(
                    SocketEvent.ConnectionReady | SocketEvent.Connected);
                client.Connect(endpoint);
                clients.Add(client);
                monitors.Add(monitor);
            }

            var activeClients = new List<Zlink.Socket>(clients.Count);
            for (int i = 0; i < monitors.Count; i++)
            {
                if (WaitMonitorReady(monitors[i], readyTimeoutMs, true))
                    activeClients.Add(clients[i]);
            }

            if (activeClients.Count == 0)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }

            var payload = new byte[size];
            Array.Fill(payload, (byte)'a');
            int index = 0;

            if (activeWarmup)
            {
                var warmupDeadline = DateTime.UtcNow.AddSeconds(
                    Math.Max(0, warmupSeconds));
                while (DateTime.UtcNow < warmupDeadline)
                {
                    Zlink.Socket client = activeClients[index];
                    SendRetry(client, payload.AsSpan(), SendFlags.None);
                    index = (index + 1) % activeClients.Count;
                }
                if (warmupDrainMs > 0)
                    Thread.Sleep(warmupDrainMs);
            }
            else if (warmupSeconds > 0)
            {
                Thread.Sleep(warmupSeconds * 1000);
            }

            if (settleMs > 0)
                Thread.Sleep(settleMs);

            long measureCount = 0;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var benchDeadline =
                DateTime.UtcNow.AddSeconds(Math.Max(1, durationSeconds));
            while (DateTime.UtcNow < benchDeadline)
            {
                Zlink.Socket client = activeClients[index];
                SendRetry(client, payload.AsSpan(), SendFlags.None);
                measureCount++;
                index = (index + 1) % activeClients.Count;
            }
            sw.Stop();

            if (drainMs > 0)
                Thread.Sleep(drainMs);
            if (sizeTransitionDrainMs > 0)
                Thread.Sleep(sizeTransitionDrainMs);

            double throughput = sw.Elapsed.TotalSeconds > 0.0
                ? measureCount / sw.Elapsed.TotalSeconds
                : 0.0;
            double latencyUs = (sw.Elapsed.TotalMilliseconds * 1000.0)
                / Math.Max(1.0, measureCount);

            try
            {
                SendRetry(activeClients[0], MultiStopToken.AsSpan(),
                    SendFlags.None);
            }
            catch
            {
            }

            PrintResult("MULTI_SPOT", transport, size, throughput, latencyUs);
            return 0;
        }
        finally
        {
            foreach (MonitorSocket monitor in monitors)
            {
                try
                {
                    monitor.Dispose();
                }
                catch
                {
                }
            }

            foreach (Zlink.Socket client in clients)
            {
                try
                {
                    client.Dispose();
                }
                catch
                {
                }
            }
        }
    }
}
