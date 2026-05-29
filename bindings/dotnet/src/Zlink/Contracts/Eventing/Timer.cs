// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the zlink timer contract.
/// </summary>
public interface IZlinkTimer : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets or sets the start.
    /// </summary>
    void Start(TimeSpan interval, ulong repeatCount);

    /// <summary>
    /// Gets or sets the stop.
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
