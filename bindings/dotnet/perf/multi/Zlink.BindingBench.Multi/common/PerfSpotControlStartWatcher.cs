using System;
using System.Diagnostics;
using System.Text;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal sealed class PerfSpotControlStartWatcher : IDisposable
{
    private readonly Spot _controlSub;
    private readonly string _topic;
    private readonly int _size;
    private readonly ManualResetEventSlim _started = new(false);
    private readonly Thread _thread;
    private readonly bool _debug =
        PerfEnv.ReadPositive("PERF_DOTNET_CONTROL_DEBUG", 0) > 0;
    private int _disposed;

    internal PerfSpotControlStartWatcher(Spot controlSub, string topic, int size)
    {
        _controlSub = controlSub;
        _topic = topic;
        _size = size;
        _thread = new Thread(ReadLoop)
        {
            IsBackground = true,
        };
        _thread.Start();
    }

    internal bool Wait(int timeoutMs)
    {
        if (_started.IsSet)
            return true;
        _started.Wait(Math.Max(1, timeoutMs));
        return _started.IsSet;
    }

    public void Dispose()
    {
        Volatile.Write(ref _disposed, 1);
        _started.Dispose();
    }

    private void ReadLoop()
    {
        string expected = $"START,{_size}";
        while (Volatile.Read(ref _disposed) == 0 && !_started.IsSet)
        {
            try
            {
                using TopicMessage? received =
                    _controlSub.Subscribe(RecvFlags.DontWait);
                if (received == null)
                {
                    Thread.Yield();
                    continue;
                }

                if (received.Topic != _topic)
                    continue;

                string payload = Encoding.ASCII.GetString(
                    received.SinglePartOrThrow().AsReadOnlySpan());
                if (_debug)
                    Console.Error.WriteLine(
                        $"control_debug:recv:{received.Topic}:{payload}");
                if (payload == expected)
                    _started.Set();
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
                Thread.Yield();
            }
        }
    }
}
