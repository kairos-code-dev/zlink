// SPDX-License-Identifier: MPL-2.0

import { createError } from '../../contracts/errors/errors';
import type { AutoHwmProfileValue } from '../../contracts/core';
import {
  closeCall,
  configCall,
  lastError,
  nativeErrorMessage,
  readErrno,
} from '../errors/native_errors';
import { validateCString } from '../options/validation';
import { requireNative } from '../native/native';
import { ContextOption } from './context_options';

class NativeHandle {
  /** @internal */
  protected _native: unknown | null;

  protected constructor(native: unknown) {
    this._native = native;
  }

  /** @internal */
  nativeHandle(): unknown {
    return this._native;
  }

  close(): void {
    this._native = null;
  }
}

const OPTION_CREATE_TOKEN = Symbol('OptionFacade.create');

export class ContextOptions {
  /** @internal */
  protected readonly _context: Context;
  private _threadNamePrefix = '';

  /** @internal */
  private constructor(token: symbol, context: Context) {
    if (token !== OPTION_CREATE_TOKEN) {
      throw new TypeError('context options are created by contexts');
    }
    this._context = context;
  }

  /** @internal */
  static create(context: Context): ContextOptions {
    return new ContextOptions(OPTION_CREATE_TOKEN, context);
  }

  get ioThreads(): number { return this._context.getOptionRawInternal(ContextOption.IO_THREADS); }
  set ioThreads(value: number) { this._context.setOptionRawInternal(ContextOption.IO_THREADS, value | 0); }
  get maxSockets(): number { return this._context.getOptionRawInternal(ContextOption.MAX_SOCKETS); }
  set maxSockets(value: number) { this._context.setOptionRawInternal(ContextOption.MAX_SOCKETS, value | 0); }
  get socketLimit(): number { return this._context.getOptionRawInternal(ContextOption.SOCKET_LIMIT); }
  get maxMsgSize(): number { return this._context.getOptionRawInternal(ContextOption.MAX_MSGSZ); }
  set maxMsgSize(value: number) { this._context.setOptionRawInternal(ContextOption.MAX_MSGSZ, value | 0); }
  get msgTSize(): number { return this._context.getOptionRawInternal(ContextOption.MSG_T_SIZE); }
  get threadPriority(): number { return this._context.getOptionRawInternal(ContextOption.THREAD_PRIORITY); }
  set threadPriority(value: number) { this._context.setOptionRawInternal(ContextOption.THREAD_PRIORITY, value | 0); }
  get threadSchedulingPolicy(): number { return this._context.getOptionRawStrictInternal(ContextOption.THREAD_SCHED_POLICY); }
  set threadSchedulingPolicy(value: number) { this._context.setOptionRawInternal(ContextOption.THREAD_SCHED_POLICY, value | 0); }
  get blocky(): boolean { return this._context.getOptionRawInternal(ContextOption.BLOCKY) !== 0; }
  set blocky(value: boolean) { this._context.setOptionRawInternal(ContextOption.BLOCKY, value ? 1 : 0); }
  get autoHwmEnabled(): boolean { return this._context.getOptionRawInternal(ContextOption.AUTO_HWM_ENABLE) !== 0; }
  set autoHwmEnabled(value: boolean) { this._context.setOptionRawInternal(ContextOption.AUTO_HWM_ENABLE, value ? 1 : 0); }
  get autoHwmRecalcDebounceMs(): number { return this._context.getOptionRawInternal(ContextOption.AUTO_HWM_RECALC_DEBOUNCE_MS); }
  set autoHwmRecalcDebounceMs(value: number) { this._context.setOptionRawInternal(ContextOption.AUTO_HWM_RECALC_DEBOUNCE_MS, value | 0); }
  get autoHwmProfile(): AutoHwmProfileValue { return this._context.getOptionRawInternal(ContextOption.AUTO_HWM_PROFILE) as AutoHwmProfileValue; }
  set autoHwmProfile(value: AutoHwmProfileValue) { this._context.setOptionRawInternal(ContextOption.AUTO_HWM_PROFILE, value | 0); }
  get autoHwmMsgUnitBytes(): number { return this._context.getOptionRawInternal(ContextOption.AUTO_HWM_MSG_UNIT_BYTES); }
  set autoHwmMsgUnitBytes(value: number) {
    const normalized = value | 0;
    if (normalized < 0 || normalized !== value) {
      throw new RangeError('autoHwmMsgUnitBytes must be a non-negative int32');
    }
    this._context.setOptionRawInternal(ContextOption.AUTO_HWM_MSG_UNIT_BYTES, normalized);
  }
  get threadNamePrefix(): string { return this._threadNamePrefix; }
  set threadNamePrefix(value: string) {
    const normalized = validateCString(value, 'threadNamePrefix');
    this._context.setOptionRawInternal(ContextOption.THREAD_NAME_PREFIX, Buffer.from(normalized));
    this._threadNamePrefix = normalized;
  }
  addThreadAffinity(cpu: number): void { this._context.setOptionRawInternal(ContextOption.THREAD_AFFINITY_CPU_ADD, cpu | 0); }
  removeThreadAffinity(cpu: number): void { this._context.setOptionRawInternal(ContextOption.THREAD_AFFINITY_CPU_REMOVE, cpu | 0); }
}

export class Context extends NativeHandle {
  readonly options: ContextOptions;

  constructor() {
    super(requireNative().ctxNew());
    if (!this._native) throw lastError('config', 'context creation failed');
    this.options = ContextOptions.create(this);
  }

  /** @internal */
  nativeHandle(): unknown { return this._native; }

  /** @internal */
  setOptionRawInternal(option: number, value: Buffer | number): void {
    configCall('context option set failed', () => {
      requireNative().ctxSetOpt(this._native, option | 0, typeof value === 'number' ? value | 0 : value);
    });
  }

  /** @internal */
  getOptionRawInternal(option: number): number {
    try {
      return requireNative().ctxGetOpt(this._native, option | 0) as number;
    } catch (error) {
      if (
        (option | 0) === ContextOption.THREAD_PRIORITY ||
        (option | 0) === ContextOption.THREAD_SCHED_POLICY
      ) {
        return -1;
      }
      throw createError('config', readErrno(), nativeErrorMessage(error, 'context option get failed'));
    }
  }

  /** @internal */
  getOptionRawStrictInternal(option: number): number {
    try {
      return requireNative().ctxGetOpt(this._native, option | 0) as number;
    } catch (error) {
      const message = error instanceof Error && error.message
        ? error.message
        : 'ctx_getopt failed';
      throw createError('config', readErrno(), message);
    }
  }

  shutdown(): void {
    closeCall('context shutdown failed', () => {
      requireNative().ctxShutdown(this._native);
    });
  }

  recalculateAutoHwm(): void {
    configCall('context auto HWM recalculation failed', () => {
      requireNative().ctxRecalculateAutoHwm(this._native);
    });
  }

  close(): void {
    if (!this._native) return;
    closeCall('context close failed', () => {
      requireNative().ctxTerm(this._native);
    });
    this._native = null;
  }
}

export {
  Context as DefaultContext,
  ContextOptions as DefaultContextOptions,
};
