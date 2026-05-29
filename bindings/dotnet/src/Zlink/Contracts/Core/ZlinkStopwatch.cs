// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the zlink stopwatch contract.
/// </summary>
public interface IZlinkStopwatch : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets the elapsed intermediate time.
    /// </summary>
    ulong Intermediate();

    /// <summary>
    /// Stops the timer.
    /// </summary>
    ulong Stop();
}
