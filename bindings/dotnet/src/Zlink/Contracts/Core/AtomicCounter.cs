// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the atomic counter contract.
/// </summary>
public interface IAtomicCounter : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets the value.
    /// </summary>
    int Value { get; }

    /// <summary>
    /// Sets the value.
    /// </summary>
    void Set(int value);

    /// <summary>
    /// Increments the counter and returns the new value.
    /// </summary>
    int Increment();

    /// <summary>
    /// Decrements the counter and returns the new value.
    /// </summary>
    int Decrement();
}
