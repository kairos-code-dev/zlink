// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IAtomicCounter : IDisposable, IAsyncDisposable
{
    int Value { get; }

    void Set(int value);

    int Increment();

    int Decrement();
}
