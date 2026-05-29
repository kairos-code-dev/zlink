// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the zlink timer contract.
/// </summary>
public interface IZlinkTimer : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Starts the timer.
    /// </summary>
    void Start(TimeSpan interval, ulong repeatCount);

    /// <summary>
    /// Stops the timer.
    /// </summary>
    void Stop();

    /// <summary>
    /// Receives the next available item.
    /// </summary>
    ulong? Recv(RecvFlags flags = RecvFlags.None);

    /// <summary>
    /// Registers a handler for fire.
    /// </summary>
    void OnFire(Action<IZlinkTimer, ulong> handler);

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}
