// SPDX-License-Identifier: MPL-2.0

export type TimerHandler = (timer: Timer, fireCount: bigint) => void;

export interface Timer {
  start(intervalNs: bigint, repeatCount: bigint): void;
  stop(): void;
  recv(): bigint | null;
  onFire(handler: TimerHandler): void;
  close(): void;
}

export interface Thread {
  join(): void;
}

export interface Stopwatch {
  intermediate(): number;
  stop(): number;
  close(): void;
}

export interface AtomicCounter {
  set(value: number): void;
  inc(): number;
  dec(): number;
  value(): number;
  close(): void;
}
