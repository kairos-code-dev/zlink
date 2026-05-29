// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the atomic counter contract.
/// </summary>
public interface IAtomicCounter : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets or sets the value.
    /// </summary>
    int Value { get; }

    /// <summary>
    /// Sets the value.
    /// </summary>
    void Set(int value);

    /// <summary>
    /// Gets or sets the increment.
    /// </summary>
    int Increment();

    /// <summary>
    /// Gets or sets the decrement.
    /// </summary>
    int Decrement();
}
