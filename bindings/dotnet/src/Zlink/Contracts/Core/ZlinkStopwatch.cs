// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the zlink stopwatch contract.
/// </summary>
public interface IZlinkStopwatch : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets or sets the intermediate.
    /// </summary>
    ulong Intermediate();

    /// <summary>
    /// Gets or sets the stop.
    /// </summary>
    ulong Stop();
}
