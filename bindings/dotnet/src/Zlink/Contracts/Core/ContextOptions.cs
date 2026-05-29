// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the context options contract.
/// </summary>
public interface IContextOptions
{
    /// <summary>
    /// Gets or sets the io threads.
    /// </summary>
    int IoThreads { get; set; }

    /// <summary>
    /// Gets or sets the max sockets.
    /// </summary>
    int MaxSockets { get; set; }

    /// <summary>
    /// Gets or sets the socket limit.
    /// </summary>
    int SocketLimit { get; }

    /// <summary>
    /// Gets or sets the thread priority.
    /// </summary>
    int ThreadPriority { get; set; }

    /// <summary>
    /// Gets or sets the thread scheduling policy.
    /// </summary>
    int ThreadSchedulingPolicy { get; set; }

    /// <summary>
    /// Gets or sets the max message size.
    /// </summary>
    int MaxMessageSize { get; set; }

    /// <summary>
    /// Gets or sets the message thread size.
    /// </summary>
    int MessageThreadSize { get; }

    /// <summary>
    /// Gets or sets the blocky.
    /// </summary>
    bool Blocky { get; set; }

    /// <summary>
    /// Gets or sets the automatic high water mark profile.
    /// </summary>
    AutoHwmProfile AutoHwmProfile { get; set; }

    /// <summary>
    /// Gets or sets the automatic high water mark message unit bytes.
    /// </summary>
    int AutoHwmMessageUnitBytes { get; set; }

    /// <summary>
    /// Gets or sets the automatic high water mark enabled.
    /// </summary>
    bool AutoHwmEnabled { get; set; }

    /// <summary>
    /// Gets or sets the automatic high water mark recalc debounce.
    /// </summary>
    TimeSpan AutoHwmRecalcDebounce { get; set; }

    /// <summary>
    /// Gets or sets the thread name prefix.
    /// </summary>
    string ThreadNamePrefix { get; set; }

    /// <summary>
    /// Adds the thread affinity cpu.
    /// </summary>
    void AddThreadAffinityCpu(int cpu);

    /// <summary>
    /// Removes the thread affinity cpu.
    /// </summary>
    void RemoveThreadAffinityCpu(int cpu);
}
