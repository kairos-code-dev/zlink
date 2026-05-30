// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Systems.Zlink;

/// <summary>
/// Library entry point: factories for contexts, timers, pollers, and threads,
/// plus process-wide utilities (version, capabilities, proxying).
/// </summary>
public static class Zlink
{
    /// <summary>
    /// Raised when a user callback throws an exception. Callbacks run on a
    /// background dispatch thread, so such exceptions cannot propagate to the
    /// caller; subscribe here to observe them.
    /// </summary>
    public static event Action<Exception>? UnhandledCallbackException
    {
        add => CallbackExceptionHub.UnhandledCallbackException += value;
        remove => CallbackExceptionHub.UnhandledCallbackException -= value;
    }

    internal static int Errno()
    {
        return ZlinkRuntime.Errno();
    }

    /// <summary>
    /// Gets the human-readable message for a native zlink error code.
    /// </summary>
    public static string Strerror(int errnum)
    {
        return ZlinkRuntime.Strerror(errnum);
    }

    /// <summary>
    /// Gets the native zlink version.
    /// </summary>
    public static (int Major, int Minor, int Patch) Version()
    {
        return ZlinkRuntime.Version();
    }

    /// <summary>
    /// Creates a messaging context. The caller owns the returned context and
    /// must dispose it; disposing it terminates the sockets created from it.
    /// </summary>
    public static IContext CreateContext()
    {
        return new Context();
    }

    /// <summary>
    /// Creates an atomic counter. The caller owns the returned counter and must
    /// dispose it.
    /// </summary>
    public static IAtomicCounter CreateAtomicCounter()
    {
        return new AtomicCounter();
    }

    /// <summary>
    /// Creates a stopwatch. The caller owns the returned stopwatch and must
    /// dispose it.
    /// </summary>
    public static IZlinkStopwatch CreateStopwatch()
    {
        return new ZlinkStopwatch();
    }

    /// <summary>
    /// Creates and immediately starts a background thread running
    /// <paramref name="task"/>. The caller owns the returned thread and must
    /// dispose it (or <see cref="IZlinkThread.Join"/> then
    /// <see cref="IZlinkThread.Close"/>) to release it.
    /// </summary>
    public static IZlinkThread CreateThread(Action task)
    {
        return new ZlinkThread(task);
    }

    /// <summary>
    /// Creates a poller. The caller owns the returned poller and must dispose
    /// it.
    /// </summary>
    public static IPoller CreatePoller()
    {
        return new Poller();
    }

    /// <summary>
    /// Creates a standalone timer. The caller owns the returned timer and must
    /// dispose it.
    /// </summary>
    public static IZlinkTimer CreateTimer()
    {
        return new Timer();
    }

    /// <summary>
    /// Creates a timer bound to <paramref name="spot"/>'s event loop, so its
    /// callbacks run on the spot's dispatch thread. The caller owns the
    /// returned timer and must dispose it.
    /// </summary>
    public static IZlinkTimer CreateTimer(ISpot spot)
    {
        return Timer.FromSpot(SocketInterop.RequireSpot(spot, nameof(spot)));
    }

    /// <summary>
    /// Reports whether the native library was built with the named capability.
    /// </summary>
    public static bool Has(string capability)
    {
        return ZlinkRuntime.Has(capability);
    }

    /// <summary>
    /// Forwards messages between two sockets. This blocks the calling thread
    /// until the context is terminated, so run it on a dedicated thread.
    /// </summary>
    public static void Proxy(IZlinkSocket frontend, IZlinkSocket backend,
        IZlinkSocket? capture = null)
    {
        ZlinkRuntime.Proxy(frontend, backend, capture);
    }

    /// <summary>
    /// Forwards messages between two sockets under runtime control of
    /// <paramref name="control"/>. This blocks the calling thread until the
    /// proxy is terminated, so run it on a dedicated thread.
    /// </summary>
    public static void ProxySteerable(IZlinkSocket frontend, IZlinkSocket backend,
        IZlinkSocket? capture, IZlinkSocket control)
    {
        ZlinkRuntime.ProxySteerable(frontend, backend, capture, control);
    }

    /// <summary>
    /// Sleeps for the specified duration.
    /// </summary>
    public static void Sleep(TimeSpan duration)
    {
        ZlinkRuntime.Sleep(duration);
    }

    internal static void Sleep(int seconds)
    {
        ZlinkRuntime.Sleep(seconds);
    }

    /// <summary>
    /// Disposes each message in a multipart payload.
    /// </summary>
    public static void MultipartClose(IReadOnlyList<Message> parts)
    {
        ZlinkRuntime.MultipartClose(parts);
    }
}
