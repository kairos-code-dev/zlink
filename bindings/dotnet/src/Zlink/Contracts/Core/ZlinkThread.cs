// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the zlink thread contract.
/// </summary>
public interface IZlinkThread : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Starts a join operation.
    /// </summary>
    void Join();

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}
