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

    internal static void SendWithRetry(Zlink.Socket socket,
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

    internal static Message ReceiveMessageWithTimeout(Zlink.Socket socket,
        int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                return socket.ReceiveMessage(ReceiveFlags.DontWait);
            }
            catch (ZlinkException ex) when (IsRetryable(ex))
            {
            }
            Thread.Sleep(10);
        }
        throw new TimeoutException("receive timeout");
    }

    internal static byte[] ReceiveBytesWithTimeout(Zlink.Socket socket,
        int maxSize, int timeoutMs)
    {
        _ = maxSize;
        using Message message = ReceiveMessageWithTimeout(socket, timeoutMs);
        return message.AsReadOnlySpan().ToArray();
    }

    internal static string ReceiveUtf8WithTimeout(Zlink.Socket socket, int timeoutMs)
    {
        using Message message = ReceiveMessageWithTimeout(socket, timeoutMs);
        return Encoding.UTF8.GetString(message.AsReadOnlySpan()).Trim('\0');
    }

    internal static string Utf8(Message message)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return Encoding.UTF8.GetString(message.AsReadOnlySpan()).Trim('\0');
    }

    internal static string Utf8(ReadOnlySpan<byte> payload)
    {
        return Encoding.UTF8.GetString(payload).Trim('\0');
    }

    internal static string ReceiveRouterPayloadWithTimeout(Zlink.Socket router,
        string expectedPayload, int timeoutMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            string first = ReceiveUtf8WithTimeout(router, 500);
            if (first == expectedPayload)
                return first;

            int more = router.GetOption(SocketOptions.RcvMore);
            if (more != 0)
            {
                string second = ReceiveUtf8WithTimeout(router, 500);
                if (second == expectedPayload)
                    return second;
            }
        }
        throw new TimeoutException("router payload timeout");
    }

    internal static bool TryReceiveMultipartLastPart(Zlink.Socket socket,
        int maxSize, out byte[] lastPart)
    {
        _ = maxSize;
        Message? first = null;
        try
        {
            first = socket.ReceiveMessage(ReceiveFlags.DontWait);
        }
        catch (ZlinkException ex) when (IsRetryable(ex))
        {
            lastPart = Array.Empty<byte>();
            return false;
        }

        try
        {
            lastPart = first.AsReadOnlySpan().ToArray();
            while (socket.GetOption(SocketOptions.RcvMore) != 0)
            {
                using Message next = ReceiveMessageWithTimeout(socket, 500);
                lastPart = next.AsReadOnlySpan().ToArray();
            }
            return true;
        }
        finally
        {
            first.Dispose();
        }
    }

    internal static bool ExpectNoMessage(Zlink.Socket socket, int probeMs)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(probeMs);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                using Message _ = socket.ReceiveMessage(ReceiveFlags.DontWait);
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
