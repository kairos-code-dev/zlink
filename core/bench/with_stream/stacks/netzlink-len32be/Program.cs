using System;
using System.Runtime.InteropServices;
using System.Threading;
using Zlink;

namespace NetZlinkStreamLen32BeServer;

internal sealed class ServerOptions
{
    public string Host { get; set; } = "0.0.0.0";
    public int Port { get; set; } = 38009;
    public int Size { get; set; } = 1024;
    public int SndBuf { get; set; } = 1024 * 1024;
    public int RcvBuf { get; set; } = 1024 * 1024;
    public int Backlog { get; set; } = 32768;
    public int TcpNoDelay { get; set; } = 1;
    public int IoThreads { get; set; } = 4;

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
            }
        }

        if (options.Size > MaxPayloadSize)
        {
            Console.Error.WriteLine(
              $"netzlink-len32be stream: size too large {options.Size}");
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

    public long RecvMsgs => Interlocked.Read(ref _recvMsgs);
    public long ParseError => Interlocked.Read(ref _parseError);
    public long ProtocolError => Interlocked.Read(ref _protocolError);
    public long SendError => Interlocked.Read(ref _sendError);

    public void AddRecvMsg() => Interlocked.Increment(ref _recvMsgs);
    public void AddParseError() => Interlocked.Increment(ref _parseError);
    public void AddProtocolError() => Interlocked.Increment(ref _protocolError);
    public void AddSendError() => Interlocked.Increment(ref _sendError);
}

internal sealed class StreamEchoServer : IDisposable
{
    private const int ZlinkMsgSize = 64;
    private const int ZlinkStreamDispatchLen32Be = 0x0001;
    private readonly Zlink.Socket _socket;
    private readonly Metrics _metrics;
    private readonly StreamOnPacketsDelegate _callback;
    private bool _attached;

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int StreamOnPacketsDelegate(
        IntPtr routingId,
        IntPtr messages,
        nuint messageCount);

    [DllImport("zlink", CallingConvention = CallingConvention.Cdecl)]
    private static extern int zlink_stream_attach(
        IntPtr socket,
        StreamOnPacketsDelegate onPackets,
        int flags);

    [DllImport("zlink", CallingConvention = CallingConvention.Cdecl)]
    private static extern int zlink_stream_detach(IntPtr socket);

    [DllImport("zlink", CallingConvention = CallingConvention.Cdecl)]
    private static extern int zlink_stream_send(
        IntPtr socket,
        IntPtr routingId,
        IntPtr data,
        nuint size,
        int flags);

    [DllImport("zlink", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr zlink_msg_data(IntPtr msg);

    [DllImport("zlink", CallingConvention = CallingConvention.Cdecl)]
    private static extern nuint zlink_msg_size(IntPtr msg);

    internal StreamEchoServer(Zlink.Socket socket, Metrics metrics)
    {
        _socket = socket ?? throw new ArgumentNullException(nameof(socket));
        _metrics = metrics ?? throw new ArgumentNullException(nameof(metrics));
        _callback = OnPackets;
    }

    internal void Attach()
    {
        if (_attached)
            return;
        int rc = zlink_stream_attach(
          _socket.Handle, _callback, ZlinkStreamDispatchLen32Be);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        _attached = true;
    }

    private int OnPackets(IntPtr rid, IntPtr msgs, nuint msgCount)
    {
        if (rid == IntPtr.Zero || msgs == IntPtr.Zero || msgCount == 0)
            return 0;

        int count = checked((int) msgCount);
        for (int i = 0; i < count; i++)
        {
            IntPtr msg = IntPtr.Add(msgs, i * ZlinkMsgSize);
            IntPtr payloadPtr = zlink_msg_data(msg);
            nuint payloadSizeNu = zlink_msg_size(msg);
            if (payloadSizeNu > int.MaxValue)
            {
                _metrics.AddParseError();
                _metrics.AddProtocolError();
                continue;
            }

            int payloadSize = (int) payloadSizeNu;
            if (payloadSize <= 0)
                continue;

            if (payloadSize > ServerOptions.MaxPayloadSize
                || payloadPtr == IntPtr.Zero)
            {
                _metrics.AddParseError();
                _metrics.AddProtocolError();
                continue;
            }

            if (payloadSize < ServerOptions.MinPayloadSize)
                continue;

            _metrics.AddRecvMsg();
            byte[] payloadCopy = new byte[payloadSize];
            if (payloadSize > 0)
                Marshal.Copy(payloadPtr, payloadCopy, 0, payloadSize);

            unsafe
            {
                fixed (byte *payloadPtrCopy = payloadCopy)
                {
                    int sent = zlink_stream_send(_socket.Handle, rid,
                                                 (IntPtr) payloadPtrCopy,
                                                 (nuint) payloadSize, 0);
                    if (sent != payloadSize)
                        _metrics.AddSendError();
                }
            }
        }

        return 0;
    }

    public void Dispose()
    {
        if (!_attached)
            return;
        try
        {
            int rc = zlink_stream_detach(_socket.Handle);
            if (rc != 0)
                throw ZlinkException.FromLastError();
        }
        finally
        {
            _attached = false;
        }
    }
}

internal static class Program
{
    private const int ZlinkTcpNoDelay = 118;

    [DllImport("zlink", CallingConvention = CallingConvention.Cdecl)]
    private static extern int zlink_setsockopt(IntPtr socket, int option,
                                               IntPtr optval, nuint optvallen);

    private static string Endpoint(string host, int port)
        => $"tcp://{host}:{port}";

    private static unsafe void ApplySocketTuning(Zlink.Socket socket,
                                                 ServerOptions options)
    {
        socket.SetOption(SocketOption.SndBuf, options.SndBuf);
        socket.SetOption(SocketOption.RcvBuf, options.RcvBuf);
        socket.SetOption(SocketOption.Backlog, options.Backlog);
        socket.SetOption(SocketOption.SndHwm, 0);
        socket.SetOption(SocketOption.RcvHwm, 0);

        int tcpNoDelay = options.TcpNoDelay;
        int rc = zlink_setsockopt(
          socket.Handle, ZlinkTcpNoDelay, (IntPtr) (&tcpNoDelay),
          (nuint) sizeof(int));
        if (rc != 0)
            throw ZlinkException.FromLastError();
    }

    public static int Main(string[] args)
    {
        if (args.Length == 0)
        {
            Console.WriteLine(
              "test_scenario_stream_netzlink_len32be: no args -> skip");
            return 0;
        }

        if (!ServerOptions.TryParse(args, out ServerOptions options))
            return 2;

        int rc = 0;
        Metrics metrics = new();
        using CancellationTokenSource cts = new();
        PosixSignalRegistration? sigIntReg = null;
        PosixSignalRegistration? sigTermReg = null;

        void RequestCancel()
        {
            if (!cts.IsCancellationRequested)
                cts.Cancel();
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
                sigIntReg = PosixSignalRegistration.Create(
                  PosixSignal.SIGINT, context =>
                  {
                      context.Cancel = true;
                      RequestCancel();
                  });
                sigTermReg = PosixSignalRegistration.Create(
                  PosixSignal.SIGTERM, context =>
                  {
                      context.Cancel = true;
                      RequestCancel();
                  });
            }
        }
        catch
        {
        }

        try
        {
            using Context ctx = new();
            ctx.SetOption(ContextOption.IoThreads, options.IoThreads);
            using Zlink.Socket server = new(ctx, SocketType.Stream);
            ApplySocketTuning(server, options);
            server.Bind(Endpoint(options.Host, options.Port));
            using StreamEchoServer echo = new(server, metrics);
            echo.Attach();

            while (!cts.IsCancellationRequested)
                Thread.Sleep(200);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"netzlink-len32be stream: {ex.Message}");
            rc = 2;
        }
        finally
        {
            Console.WriteLine(
                $"METRIC stack=netzlink-len32be mode=echo size={options.Size} recv_msgs={metrics.RecvMsgs} " +
                $"parse_error={metrics.ParseError} protocol_error={metrics.ProtocolError} " +
                $"send_error={metrics.SendError} connections=0");

            Console.CancelKeyPress -= cancelHandler;
            AppDomain.CurrentDomain.ProcessExit -= processExitHandler;
            sigIntReg?.Dispose();
            sigTermReg?.Dispose();
        }

        return rc;
    }
}
