import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import {
  ZLinkSubmitStatus,
  type ZLinkSubmitResult
} from './submission-result';
import { ZLinkAsyncSubmitter } from './index';

interface ZLinkMeshSubmitterEntry {
  submitter: ZLinkAsyncSubmitter;
  wake?: () => void;
}

type ZLinkMeshSendTimeoutResolver = (meshName: string) => number;
type ZLinkMeshCapacityResolver = (meshName: string) => number;

class ZLinkMeshTerminalResult extends Error {
  constructor(readonly result: ZLinkSubmitResult) {
    super(`Mesh submit completed with '${result.status}'.`);
  }
}

/** Owns the bounded admission queues driven by MeshNode SEND_READY records. */
export class ZLinkMeshSubmitterRegistry {
  private readonly entries = new Map<string, ZLinkMeshSubmitterEntry>();
  private disposed = false;

  constructor(
    private readonly timeoutMs: number | ZLinkMeshSendTimeoutResolver,
    private readonly capacity: number | ZLinkMeshCapacityResolver
  ) {}

  async submit(
    meshName: string,
    attempt: () => ZLinkSubmitResult,
    signal?: AbortSignal
  ): Promise<ZLinkSubmitResult> {
    if (this.disposed) {
      return { status: ZLinkSubmitStatus.Shutdown };
    }
    const entry = this.requireEntry(meshName);
    try {
      await entry.submitter.submitCommand(() => {
        const result = attempt();
        if (result.status === ZLinkSubmitStatus.Submitted) return true;
        if (result.status === ZLinkSubmitStatus.Backpressured) return false;
        throw new ZLinkMeshTerminalResult(result);
      }, signal);
      return { status: ZLinkSubmitStatus.Submitted };
    } catch (error) {
      if (error instanceof ZLinkMeshTerminalResult) return error.result;
      if (error instanceof ZLinkFrameworkException
        && error.kind === ZLinkFrameworkErrorKind.RuntimeShutdown) {
        return { status: ZLinkSubmitStatus.Shutdown };
      }
      if (error instanceof Error && /timed out/i.test(error.message)) {
        return { status: ZLinkSubmitStatus.TimedOut };
      }
      if (error instanceof Error && /queue is full/i.test(error.message)) {
        return { status: ZLinkSubmitStatus.Backpressured };
      }
      if (error instanceof Error && /disposed|shutdown|terminated/i.test(error.message)) {
        return { status: ZLinkSubmitStatus.Shutdown };
      }
      throw error;
    }
  }

  notify(meshName: string): void {
    if (this.disposed) return;
    this.entries.get(meshName)?.wake?.();
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    for (const entry of this.entries.values()) entry.submitter.dispose();
    this.entries.clear();
  }

  private requireEntry(meshName: string): ZLinkMeshSubmitterEntry {
    let entry = this.entries.get(meshName);
    if (entry !== undefined) return entry;
    entry = {
      submitter: undefined as unknown as ZLinkAsyncSubmitter
    };
    entry.submitter = new ZLinkAsyncSubmitter(
      (handler) => { entry!.wake = handler; },
      {
        timeoutMs: typeof this.timeoutMs === 'function'
          ? this.timeoutMs(meshName)
          : this.timeoutMs,
        capacity: typeof this.capacity === 'function'
          ? this.capacity(meshName)
          : this.capacity
      }
    );
    this.entries.set(meshName, entry);
    return entry;
  }
}
