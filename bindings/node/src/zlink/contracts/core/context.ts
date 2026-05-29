// SPDX-License-Identifier: MPL-2.0

export const AutoHwmProfile = Object.freeze({
  Compact: 0,
  LowLatency: 1,
  Balanced: 2,
  Throughput: 3
} as const);
export type AutoHwmProfileValue = typeof AutoHwmProfile[keyof typeof AutoHwmProfile];

export interface ContextOptions {
  ioThreads: number;
  maxSockets: number;
  readonly socketLimit: number;
  maxMsgSize: number;
  readonly msgTSize: number;
  threadPriority: number;
  threadSchedulingPolicy: number;
  blocky: boolean;
  autoHwmEnabled: boolean;
  autoHwmRecalcDebounceMs: number;
  autoHwmProfile: AutoHwmProfileValue;
  autoHwmMsgUnitBytes: number;
  threadNamePrefix: string;
  addThreadAffinity(cpu: number): void;
  removeThreadAffinity(cpu: number): void;
}

export interface Context {
  readonly options: ContextOptions;
  shutdown(): void;
  recalculateAutoHwm(): void;
  close(): void;
}
