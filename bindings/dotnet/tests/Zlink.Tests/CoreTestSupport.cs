using System;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace Zlink.Tests;

internal static class CoreTestSupport
{
    private static readonly Regex VersionRegex = new(
        @"^#define\s+ZLINK_VERSION_(MAJOR|MINOR|PATCH)\s+(\d+)\s*$",
        RegexOptions.Compiled);

    internal static bool IsNativeAvailable()
    {
        try
        {
            _ = ZlinkVersion.Get();
            return true;
        }
        catch (DllNotFoundException)
        {
            return false;
        }
        catch (EntryPointNotFoundException)
        {
            return false;
        }
    }

    internal static (int major, int minor, int patch) ReadCoreHeaderVersion()
    {
        string? header = FindCoreHeader();
        if (header == null)
            throw new FileNotFoundException("core/include/zlink.h not found.");

        int major = -1;
        int minor = -1;
        int patch = -1;

        foreach (string line in File.ReadLines(header))
        {
            Match m = VersionRegex.Match(line);
            if (!m.Success)
                continue;

            int value = int.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
            switch (m.Groups[1].Value)
            {
                case "MAJOR":
                    major = value;
                    break;
                case "MINOR":
                    minor = value;
                    break;
                case "PATCH":
                    patch = value;
                    break;
            }
        }

        if (major < 0 || minor < 0 || patch < 0)
            throw new InvalidOperationException("Failed to parse zlink version macros.");

        return (major, minor, patch);
    }

    internal static string NewEndpoint(string transport, string prefix)
    {
        if (transport == "inproc")
            return $"inproc://{prefix}-{Guid.NewGuid():N}";
        if (transport == "ipc")
        {
            string file = $"{prefix}-{Guid.NewGuid():N}.sock";
            string path = Path.Combine(Path.GetTempPath(), file);
            return $"ipc://{path}";
        }

        int port = ReservePort();
        return $"{transport}://127.0.0.1:{port}";
    }

    internal static bool IsTransportSupported(string transport)
    {
        if (transport == "tcp" || transport == "inproc")
            return true;
        if (transport == "ipc")
            return Runtime.Has("ipc");
        if (transport == "ws")
            return Runtime.Has("ws");
        if (transport == "wss")
            return Runtime.Has("wss");
        if (transport == "tls")
            return Runtime.Has("tls");
        return false;
    }

    internal static bool WaitUntil(Func<bool> predicate, int timeoutMs,
        int sleepMs = 10)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
                return true;
            Thread.Sleep(sleepMs);
        }
        return false;
    }

    internal static void SendBytesWithRetry(Zlink.Socket socket,
        ReadOnlySpan<byte> payload, SendFlags flags, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                socket.Send(payload, flags | SendFlags.DontWait);
                return;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }
        throw new TimeoutException("send timeout");
    }

    internal static void SendGatewayWithRetry(Gateway gateway, string serviceName,
        ReadOnlySpan<byte> payload, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                gateway.Send(serviceName, payload, SendFlags.DontWait);
                return;
            }
            catch (ZlinkException)
            {
                Thread.Sleep(10);
            }
        }
        throw new TimeoutException("gateway send timeout");
    }

    internal static byte[] ReceiveBytesWithTimeout(Zlink.Socket socket,
        int maxSize, int timeoutMs)
    {
        byte[] buffer = new byte[maxSize];
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                int bytes = socket.Receive(buffer, ReceiveFlags.DontWait);
                byte[] result = new byte[bytes];
                if (bytes > 0)
                    Buffer.BlockCopy(buffer, 0, result, 0, bytes);
                return result;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }
        throw new TimeoutException("receive timeout");
    }

    internal static string ReceiveStringWithTimeout(Zlink.Socket socket,
        int maxSize, int timeoutMs)
    {
        byte[] bytes = ReceiveBytesWithTimeout(socket, maxSize, timeoutMs);
        return Encoding.UTF8.GetString(bytes).Trim('\0');
    }

    internal static string ReceiveRouterPayloadWithTimeout(Zlink.Socket router,
        string expectedPayload, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            string first = ReceiveStringWithTimeout(router, 256, 500);
            if (first == expectedPayload)
                return first;

            int more = router.GetOption(SocketOption.RcvMore);
            if (more != 0)
            {
                string second = ReceiveStringWithTimeout(router, 256, 500);
                if (second == expectedPayload)
                    return second;
            }
        }
        throw new TimeoutException("router payload timeout");
    }

    internal static bool TryReceiveMultipartLastPart(Zlink.Socket socket,
        int maxSize, out byte[] lastPart)
    {
        byte[] buffer = new byte[maxSize];
        int bytes;
        try
        {
            bytes = socket.Receive(buffer, ReceiveFlags.DontWait);
        }
        catch (ZlinkException ex) when (IsRetryable(ex))
        {
            lastPart = Array.Empty<byte>();
            return false;
        }

        lastPart = new byte[bytes];
        if (bytes > 0)
            Buffer.BlockCopy(buffer, 0, lastPart, 0, bytes);

        while (socket.GetOption(SocketOption.RcvMore) != 0)
            lastPart = ReceiveBytesWithTimeout(socket, maxSize, 500);
        return true;
    }

    internal static bool ExpectNoMessage(Zlink.Socket socket, int probeMs)
    {
        byte[] buffer = new byte[64];
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(probeMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                _ = socket.Receive(buffer, ReceiveFlags.DontWait);
                return false;
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(5);
        }
        return true;
    }

    internal static GatewayMessage ReceiveGatewayWithTimeout(Gateway gateway,
        int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                return gateway.Receive(ReceiveFlags.DontWait);
            }
            catch (ZlinkException)
            {
                Thread.Sleep(10);
            }
        }
        throw new TimeoutException("gateway receive timeout");
    }

    internal static SpotMessage ReceiveSpotWithTimeout(Spot spot, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                return spot.Receive(ReceiveFlags.DontWait);
            }
            catch (ZlinkException)
            {
                Thread.Sleep(10);
            }
        }
        throw new TimeoutException("spot receive timeout");
    }

    internal static int ExtractPort(string endpoint)
    {
        int idx = endpoint.LastIndexOf(':');
        if (idx <= 0 || idx == endpoint.Length - 1)
            throw new ArgumentException($"invalid endpoint: {endpoint}",
                nameof(endpoint));
        return int.Parse(endpoint.AsSpan(idx + 1), CultureInfo.InvariantCulture);
    }

    private static int ReservePort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }

    private static bool IsRetryable(ZlinkException ex)
    {
        ErrorCode code = ZlinkException.MapErrorCode(ex.Errno);
        return code == ErrorCode.EAgain || code == ErrorCode.EIntr;
    }

    private static string? FindCoreHeader()
    {
        DirectoryInfo? current = new DirectoryInfo(Directory.GetCurrentDirectory());
        for (int i = 0; i < 10 && current != null; i++)
        {
            string candidate = Path.Combine(current.FullName, "core", "include",
                "zlink.h");
            if (File.Exists(candidate))
                return candidate;
            current = current.Parent;
        }
        return null;
    }
}
