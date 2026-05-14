// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IContextOptions
{
    int IoThreads { get; set; }

    int MaxSockets { get; set; }

    int SocketLimit { get; }

    int ThreadPriority { get; set; }

    int ThreadSchedulingPolicy { get; set; }

    int MaxMessageSize { get; set; }

    int MessageThreadSize { get; }

    bool Blocky { get; set; }

    AutoHwmProfile AutoHwmProfile { get; set; }

    bool AutoHwmEnabled { get; set; }

    TimeSpan AutoHwmRecalcDebounce { get; set; }

    string ThreadNamePrefix { get; set; }

    void AddThreadAffinityCpu(int cpu);

    void RemoveThreadAffinityCpu(int cpu);
}
