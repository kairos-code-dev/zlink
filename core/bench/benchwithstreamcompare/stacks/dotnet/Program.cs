using System.Buffers;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;

namespace StreamScenarioDotnet;

internal sealed class ServerOptions
{
    public string Host { get; set; } = "0.0.0.0";
    public int Port { get; set; } = 38004;
    public int Size { get; set; } = 1024;
    public int SndBuf { get; set; } = 1024 * 1024;
    public int RcvBuf { get; set; } = 1024 * 1024;
    public int Backlog { get; set; } = 32768;
    public int TcpNoDelay { get; set; } = 1;
    public int IoThreads { get; set; } = 8;
    public int RawEcho { get; set; } = 0;

    public const int MinPayloadSize = 16;
    public const int MaxPayloadSize = 4 * 1024 * 1024;

    public static bool TryParse(string[] args, out ServerOptions options)
    {
        options = new ServerOptions();
        for (int i = 0; i < args.Length; i++)
        {
            string key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
                continue;

            if ((i + 1) >= args.Length)
                break;

            string value = args[++i];
            switch (key)
            {
                case "--host":
                    options.Host = value;
                    break;
                case "--port":
                    options.Port = ParseInt(value, options.Port, 1);
                    break;
                case "--size":
                    options.Size = ParseInt(value, options.Size, MinPayloadSize);
                    break;
                case "--sndbuf":
                    options.SndBuf = ParseInt(value, options.SndBuf, 1);
                    break;
                case "--rcvbuf":
                    options.RcvBuf = ParseInt(value, options.RcvBuf, 1);
                    break;
                case "--backlog":
                    options.Backlog = ParseInt(value, options.Backlog, 1);
                    break;
                case "--tcp-nodelay":
                    options.TcpNoDelay = ParseInt(value, options.TcpNoDelay, 0);
                    break;
                case "--io-threads":
                    options.IoThreads = ParseInt(value, options.IoThreads, 1);
                    break;
                case "--raw-echo":
                    options.RawEcho = ParseInt(value, options.RawEcho, 0);
                    break;
            }
        }

        if (options.Size > MaxPayloadSize)
        {
            Console.Error.WriteLine($"dotnet stream: size too large {options.Size}");
            return false;
        }

        return true;
    }

    private static int ParseInt(string text, int fallback, int min)
    {
        if (!int.TryParse(text, out int parsed))
            return fallback;
        if (parsed < min)
            return min;
        return parsed;
    }
}

internal sealed class Metrics
{
    private long _recvMsgs;
    private long _parseError;
    private long _protocolError;
    private long _sendError;
    private long _activeConnections;

    public long RecvMsgs => Interlocked.Read(ref _recvMsgs);
    public long ParseError => Interlocked.Read(ref _parseError);
    public long ProtocolError => Interlocked.Read(ref _protocolError);
    public long SendError => Interlocked.Read(ref _sendError);
    public long ActiveConnections => Interlocked.Read(ref _activeConnections);

    public void AddRecvMsg() => Interlocked.Increment(ref _recvMsgs);
    public void AddParseError() => Interlocked.Increment(ref _parseError);
    public void AddProtocolError() => Interlocked.Increment(ref _protocolError);
    public void AddSendError() => Interlocked.Increment(ref _sendError);
    public void AddConnection() => Interlocked.Increment(ref _activeConnections);
    public void RemoveConnection()
    {
        if (Interlocked.Read(ref _activeConnections) > 0)
            Interlocked.Decrement(ref _activeConnections);
    }
}

internal sealed class EchoServer
{
    private readonly ServerOptions _options;
    private readonly Metrics _metrics;
    private readonly List<Task> _connections = new();
    private readonly object _connectionsLock = new();

    public EchoServer(ServerOptions options, Metrics metrics)
    {
        _options = options;
        _metrics = metrics;
    }

    public async Task<int> RunAsync(CancellationToken token)
    {
        if (!IPAddress.TryParse(_options.Host, out IPAddress? address))
        {
            Console.Error.WriteLine($"dotnet stream: invalid host {_options.Host}");
            return 2;
        }

        ThreadPool.GetMinThreads(out int currentWorker, out int currentIo);
        int desired = Math.Max(currentWorker, _options.IoThreads * 2);
        ThreadPool.SetMinThreads(desired, Math.Max(currentIo, _options.IoThreads * 2));

        Socket listener = new(address.AddressFamily, SocketType.Stream, ProtocolType.Tcp);
        listener.NoDelay = _options.TcpNoDelay != 0;
        listener.SendBufferSize = _options.SndBuf;
        listener.ReceiveBufferSize = _options.RcvBuf;
        listener.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);

        try
        {
            listener.Bind(new IPEndPoint(address, _options.Port));
            listener.Listen(_options.Backlog);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"dotnet stream: bind/listen failed: {ex.Message}");
            listener.Dispose();
            return 2;
        }

        using CancellationTokenRegistration reg = token.Register(() =>
        {
            try
            {
                listener.Close();
            }
            catch
            {
                // ignored
            }
        });

        while (!token.IsCancellationRequested)
        {
            Socket? socket = null;
            try
            {
                socket = await listener.AcceptAsync(token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            catch (Exception)
            {
                if (!token.IsCancellationRequested)
                    _metrics.AddSendError();
                continue;
            }

            socket.NoDelay = _options.TcpNoDelay != 0;
            socket.SendBufferSize = _options.SndBuf;
            socket.ReceiveBufferSize = _options.RcvBuf;

            _metrics.AddConnection();
            Task connTask = RunConnectionAsync(socket, token);
            lock (_connectionsLock)
            {
                _connections.Add(connTask);
            }

            _ = connTask.ContinueWith(t =>
            {
                lock (_connectionsLock)
                {
                    _connections.Remove(t);
                }
            }, TaskScheduler.Default);
        }

        Task[] pending;
        lock (_connectionsLock)
        {
            pending = _connections.ToArray();
        }

        try
        {
            await Task.WhenAll(pending).ConfigureAwait(false);
        }
        catch
        {
            // ignored
        }

        return 0;
    }

    private async Task RunConnectionAsync(Socket socket, CancellationToken token)
    {
        if (_options.RawEcho != 0)
        {
            await RunConnectionRawAsync(socket, token).ConfigureAwait(false);
            return;
        }

        int initialFrame = Math.Max(ServerOptions.MinPayloadSize, _options.Size);
        byte[] frameBuffer = ArrayPool<byte>.Shared.Rent(initialFrame + 4);

        try
        {
            while (!token.IsCancellationRequested)
            {
                if (!await ReadExactAsync(socket, frameBuffer, 0, 4, token)
                        .ConfigureAwait(false))
                {
                    break;
                }

                int bodySize = ReadU32Be(frameBuffer, 0);
                if (bodySize < ServerOptions.MinPayloadSize
                    || bodySize > ServerOptions.MaxPayloadSize)
                {
                    _metrics.AddParseError();
                    _metrics.AddProtocolError();
                    return;
                }

                int frameSize = 4 + bodySize;
                if (frameSize > frameBuffer.Length)
                {
                    ArrayPool<byte>.Shared.Return(frameBuffer);
                    frameBuffer = ArrayPool<byte>.Shared.Rent(frameSize);
                    WriteU32Be(frameBuffer, 0, bodySize);
                }

                if (!await ReadExactAsync(socket, frameBuffer, 4, bodySize, token)
                        .ConfigureAwait(false))
                {
                    break;
                }

                if (bodySize != _options.Size)
                    _metrics.AddProtocolError();

                int sent = 0;
                while (sent < frameSize)
                {
                    int nsend = await socket.SendAsync(
                        new ArraySegment<byte>(frameBuffer, sent, frameSize - sent),
                        SocketFlags.None,
                        token).ConfigureAwait(false);
                    if (nsend <= 0)
                    {
                        _metrics.AddSendError();
                        return;
                    }
                    sent += nsend;
                }

            }
        }
        catch (OperationCanceledException)
        {
            // ignored
        }
        catch (SocketException)
        {
            _metrics.AddSendError();
        }
        finally
        {
            try
            {
                socket.Shutdown(SocketShutdown.Both);
            }
            catch
            {
                // ignored
            }

            socket.Dispose();
            ArrayPool<byte>.Shared.Return(frameBuffer);
            _metrics.RemoveConnection();
        }
    }

    private async Task RunConnectionRawAsync(Socket socket, CancellationToken token)
    {
        int bufSize = Math.Max(64 * 1024, Math.Max(ServerOptions.MinPayloadSize, _options.Size));
        byte[] buffer = ArrayPool<byte>.Shared.Rent(bufSize);

        try
        {
            while (!token.IsCancellationRequested)
            {
                int nrecv = await socket.ReceiveAsync(
                    new ArraySegment<byte>(buffer, 0, buffer.Length),
                    SocketFlags.None,
                    token).ConfigureAwait(false);
                if (nrecv <= 0)
                    break;

                _metrics.AddRecvMsg();

                int sent = 0;
                while (sent < nrecv)
                {
                    int nsend = await socket.SendAsync(
                        new ArraySegment<byte>(buffer, sent, nrecv - sent),
                        SocketFlags.None,
                        token).ConfigureAwait(false);
                    if (nsend <= 0)
                    {
                        _metrics.AddSendError();
                        return;
                    }
                    sent += nsend;
                }
            }
        }
        catch (OperationCanceledException)
        {
            // ignored
        }
        catch (SocketException)
        {
            _metrics.AddSendError();
        }
        finally
        {
            try
            {
                socket.Shutdown(SocketShutdown.Both);
            }
            catch
            {
                // ignored
            }

            socket.Dispose();
            ArrayPool<byte>.Shared.Return(buffer);
            _metrics.RemoveConnection();
        }
    }

    private static async ValueTask<bool> ReadExactAsync(
        Socket socket,
        byte[] buffer,
        int offset,
        int length,
        CancellationToken token)
    {
        int read = 0;
        while (read < length)
        {
            int n = await socket.SendReceiveAsyncHelper(
                buffer,
                offset + read,
                length - read,
                token).ConfigureAwait(false);
            if (n <= 0)
                return false;
            read += n;
        }
        return true;
    }

    private static int ReadU32Be(byte[] data, int offset)
    {
        return (data[offset] << 24)
               | (data[offset + 1] << 16)
               | (data[offset + 2] << 8)
               | data[offset + 3];
    }

    private static void WriteU32Be(byte[] data, int offset, int value)
    {
        data[offset] = (byte)((value >> 24) & 0xFF);
        data[offset + 1] = (byte)((value >> 16) & 0xFF);
        data[offset + 2] = (byte)((value >> 8) & 0xFF);
        data[offset + 3] = (byte)(value & 0xFF);
    }
}

internal static class SocketExtensions
{
    public static ValueTask<int> SendReceiveAsyncHelper(
        this Socket socket,
        byte[] buffer,
        int offset,
        int count,
        CancellationToken token)
    {
        return socket.ReceiveAsync(new ArraySegment<byte>(buffer, offset, count), SocketFlags.None, token);
    }
}

internal static class Program
{
    public static async Task<int> Main(string[] args)
    {
        if (args.Length == 0)
        {
            Console.WriteLine("dotnet_stream_server: no args -> skip");
            return 0;
        }

        if (!ServerOptions.TryParse(args, out ServerOptions options))
            return 2;

        using CancellationTokenSource cts = new();
        PosixSignalRegistration? sigIntReg = null;
        PosixSignalRegistration? sigTermReg = null;
        void RequestCancel()
        {
            try
            {
                if (!cts.IsCancellationRequested)
                    cts.Cancel();
            }
            catch (ObjectDisposedException)
            {
                // ignored
            }
        }

        ConsoleCancelEventHandler cancelHandler = (_, e) =>
        {
            e.Cancel = true;
            RequestCancel();
        };
        EventHandler processExitHandler = (_, _) => RequestCancel();
        Console.CancelKeyPress += cancelHandler;
        AppDomain.CurrentDomain.ProcessExit += processExitHandler;

        try
        {
            if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
            {
                sigIntReg = PosixSignalRegistration.Create(PosixSignal.SIGINT, context =>
                {
                    context.Cancel = true;
                    RequestCancel();
                });
                sigTermReg = PosixSignalRegistration.Create(PosixSignal.SIGTERM, context =>
                {
                    context.Cancel = true;
                    RequestCancel();
                });
            }
        }
        catch
        {
            // Keep benchmark runnable even if signal registration is unavailable.
        }

        try
        {
            Metrics metrics = new();
            EchoServer server = new(options, metrics);
            int rc = await server.RunAsync(cts.Token).ConfigureAwait(false);

            Console.WriteLine(
                $"METRIC stack=dotnet mode=echo size={options.Size} recv_msgs={metrics.RecvMsgs} " +
                $"parse_error={metrics.ParseError} protocol_error={metrics.ProtocolError} " +
                $"send_error={metrics.SendError} connections={metrics.ActiveConnections}");

            return rc;
        }
        finally
        {
            Console.CancelKeyPress -= cancelHandler;
            AppDomain.CurrentDomain.ProcessExit -= processExitHandler;
            sigIntReg?.Dispose();
            sigTermReg?.Dispose();
        }
    }
}
