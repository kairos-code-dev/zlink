// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading;
using System.Threading.Tasks;

namespace Zlink;

public sealed class ZlinkThread : IDisposable, IAsyncDisposable
{
    private readonly Thread _thread;
    private int _joined;

    public ZlinkThread(Action task)
    {
        if (task == null)
            throw new ArgumentNullException(nameof(task));

        _thread = new Thread(() =>
        {
            try
            {
                task();
            }
            catch (Exception ex)
            {
                Runtime.ReportUnhandledCallbackException(ex);
                throw;
            }
        })
        {
            IsBackground = true,
            Name = "Zlink.Thread"
        };
        _thread.Start();
    }

    public void Join()
    {
        if (Interlocked.Exchange(ref _joined, 1) != 0)
            return;
        _thread.Join();
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        Join();
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }
}
