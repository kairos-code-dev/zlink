using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using Zlink;

namespace NetZlinkStreamServer;

internal sealed class ServerOptions
{
    public string Host { get; set; } = "0.0.0.0";
    public int Port { get; set; } = 38007;
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
            Console.Error.WriteLine($"netzlink stream: size too large {options.Size}");
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
    private const int FramePrefixSize = 4;
    private readonly Zlink.Socket _socket;
    private readonly Metrics _metrics;
    private readonly StreamOnRawDelegate _callback;
    private readonly StashShard[] _shards;
    private bool _attached;

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int StreamOnRawDelegate(
        IntPtr routingId,
        IntPtr message);

    [DllImport("zlink", CallingConvention = CallingConvention.Cdecl)]
    private static extern int zlink_stream_attach_raw(
        IntPtr socket,
        StreamOnRawDelegate onRaw);

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

    private sealed class StashBuffer
    {
        private byte[] _data = new byte[4096];

        internal int Offset { get; set; }
        internal int Length { get; private set; }
        internal int Available => Length - Offset;
        internal byte[] Data => _data;

        internal void Append(IntPtr src, int size)
        {
            if (size <= 0 || src == IntPtr.Zero)
                return;
            Compact();
            EnsureCapacity(Length + size);
            Marshal.Copy(src, _data, Length, size);
            Length += size;
        }

        internal void Compact()
        {
            if (Offset <= 0)
                return;
            if (Offset >= Length)
            {
                Offset = 0;
                Length = 0;
                return;
            }
            if (Offset > 4096 || Offset * 2 >= Length)
            {
                int remain = Length - Offset;
                Buffer.BlockCopy(_data, Offset, _data, 0, remain);
                Offset = 0;
                Length = remain;
            }
        }

        internal void Reset()
        {
            Offset = 0;
            Length = 0;
        }

        private void EnsureCapacity(int needed)
        {
            if (_data.Length >= needed)
                return;
            int next = Math.Max(needed, _data.Length * 2);
            Array.Resize(ref _data, next);
        }
    }

    private sealed class StashShard
    {
        internal readonly object Sync = new();
        internal readonly Dictionary<uint, StashBuffer> Buffers = new();
    }

    internal StreamEchoServer(Zlink.Socket socket, Metrics metrics)
    {
        _socket = socket ?? throw new ArgumentNullException(nameof(socket));
        _metrics = metrics ?? throw new ArgumentNullException(nameof(metrics));
        _callback = OnRawPacket;
        _shards = new StashShard[64];
        for (int i = 0; i < _shards.Length; i++)
            _shards[i] = new StashShard();
    }

    internal void Attach()
    {
        if (_attached)
            return;
        int rc = zlink_stream_attach_raw(_socket.Handle, _callback);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        _attached = true;
    }

    private int OnRawPacket(IntPtr rid, IntPtr msg)
        => OnPacketsRaw(rid, msg, 1);

    private int OnPacketsRaw(IntPtr rid, IntPtr msgs, nuint msgCount)
    {
        if (!TryReadRoutingIdKey(rid, out uint ridKey))
        {
            _metrics.AddParseError();
            _metrics.AddProtocolError();
            return 0;
        }

        int count = checked((int)msgCount);
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

            int payloadSize = (int)payloadSizeNu;
            if (payloadSize <= 0)
                continue;
            if (payloadPtr == IntPtr.Zero)
            {
                _metrics.AddParseError();
                _metrics.AddProtocolError();
                continue;
            }

            List<byte[]> completeFrames =
              ExtractRawFrames(ridKey, payloadPtr, payloadSize,
                               out bool invalidFrame,
                               out bool droppedNonData);

            if (invalidFrame)
            {
                _metrics.AddParseError();
                _metrics.AddProtocolError();
                continue;
            }
            if (droppedNonData)
                continue;

            for (int f = 0; f < completeFrames.Count; f++)
            {
                byte[] frame = completeFrames[f];
                unsafe
                {
                    fixed (byte *framePtr = frame)
                    {
                        int sent = zlink_stream_send(_socket.Handle, rid,
                                                     (IntPtr) framePtr,
                                                     (nuint) frame.Length, 0);
                        if (sent != frame.Length)
                            _metrics.AddSendError();
                    }
                }
            }
        }

        return 0;
    }

    private List<byte[]> ExtractRawFrames(uint ridKey, IntPtr chunk, int chunkLen,
                                          out bool invalidFrame,
                                          out bool droppedNonData)
    {
        invalidFrame = false;
        droppedNonData = false;
        List<byte[]> completeFrames = new();
        StashShard shard = _shards[ridKey % (uint)_shards.Length];
        lock (shard.Sync)
        {
            if (!shard.Buffers.TryGetValue(ridKey, out StashBuffer? stash))
            {
                stash = new StashBuffer();
                shard.Buffers[ridKey] = stash;
            }

            stash.Append(chunk, chunkLen);

            int consumed = 0;
            int available = stash.Available;
            while (consumed + FramePrefixSize <= available)
            {
                int frameOffset = stash.Offset + consumed;
                int bodySize = ReadU32Be(stash.Data, frameOffset);
                if (bodySize < ServerOptions.MinPayloadSize
                    || bodySize > ServerOptions.MaxPayloadSize)
                {
                    if (consumed == 0)
                    {
                        droppedNonData = true;
                    }
                    else
                    {
                        invalidFrame = true;
                    }
                    break;
                }

                int frameSize = FramePrefixSize + bodySize;
                if (consumed + frameSize > available)
                    break;

                byte[] frame = new byte[frameSize];
                Buffer.BlockCopy(stash.Data, frameOffset, frame, 0, frameSize);
                completeFrames.Add(frame);
                _metrics.AddRecvMsg();
                consumed += frameSize;
            }

            if (invalidFrame || droppedNonData)
            {
                stash.Reset();
                if (stash.Available == 0)
                    shard.Buffers.Remove(ridKey);
            }
            else if (consumed > 0)
            {
                stash.Offset += consumed;
                stash.Compact();
                if (stash.Available == 0)
                    shard.Buffers.Remove(ridKey);
            }
        }

        return completeFrames;
    }

    private static bool TryReadRoutingIdKey(IntPtr rid, out uint key)
    {
        key = 0;
        if (rid == IntPtr.Zero)
            return false;

        int size = Marshal.ReadByte(rid, 0);
        if (size != 4)
            return false;

        uint b0 = Marshal.ReadByte(rid, 1);
        uint b1 = Marshal.ReadByte(rid, 2);
        uint b2 = Marshal.ReadByte(rid, 3);
        uint b3 = Marshal.ReadByte(rid, 4);
        key = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
        return true;
    }

    private static int ReadU32Be(byte[] data, int offset)
    {
        return (data[offset] << 24)
               | (data[offset + 1] << 16)
               | (data[offset + 2] << 8)
               | data[offset + 3];
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

    private static string Endpoint(string host, int port) => $"tcp://{host}:{port}";

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
            Console.WriteLine("test_scenario_stream_netzlink: no args -> skip");
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
            Console.Error.WriteLine($"netzlink stream: {ex.Message}");
            rc = 2;
        }
        finally
        {
            Console.WriteLine(
                $"METRIC stack=netzlink mode=echo size={options.Size} recv_msgs={metrics.RecvMsgs} " +
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
