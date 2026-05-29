// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Systems.Zlink;

/// <summary>
/// Represents zlink.
/// </summary>
public static class Zlink
{
    /// <summary>
    /// Gets or sets the unhandled callback exception.
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
    /// Gets the error string for a native error code.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static string Strerror(int errnum)
    {
        return ZlinkRuntime.Strerror(errnum);
    }

    /// <summary>
    /// Gets or sets the version.
    /// </summary>
    public static (int Major, int Minor, int Patch) Version()
    {
        return ZlinkRuntime.Version();
    }

    /// <summary>
    /// Creates a context.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static IContext CreateContext()
    {
        return new Context();
    }

    /// <summary>
    /// Creates a atomic counter.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static IAtomicCounter CreateAtomicCounter()
    {
        return new AtomicCounter();
    }

    /// <summary>
    /// Creates a stopwatch.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static IZlinkStopwatch CreateStopwatch()
    {
        return new ZlinkStopwatch();
    }

    /// <summary>
    /// Creates a thread.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static IZlinkThread CreateThread(Action task)
    {
        return new ZlinkThread(task);
    }

    /// <summary>
    /// Creates a poller.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static IPoller CreatePoller()
    {
        return new Poller();
    }

    /// <summary>
    /// Creates a timer.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static IZlinkTimer CreateTimer()
    {
        return new Timer();
    }

    /// <summary>
    /// Creates a timer.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static IZlinkTimer CreateTimer(ISpot spot)
    {
        return Timer.FromSpot(SocketInterop.RequireSpot(spot, nameof(spot)));
    }

    /// <summary>
    /// Gets whether a capability is available.
    /// </summary>
    /// <returns>The operation result.</returns>
    public static bool Has(string capability)
    {
        return ZlinkRuntime.Has(capability);
    }

    /// <summary>
    /// Runs a proxy between two sockets.
    /// </summary>
    public static void Proxy(IZlinkSocket frontend, IZlinkSocket backend,
        IZlinkSocket? capture = null)
    {
        ZlinkRuntime.Proxy(frontend, backend, capture);
    }

    /// <summary>
    /// Runs a steerable proxy between two sockets.
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
