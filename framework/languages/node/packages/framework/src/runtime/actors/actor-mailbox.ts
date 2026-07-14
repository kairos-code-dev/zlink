import type { ZLinkRuntimeMetrics } from '../diagnostics';

export class ZLinkActorDispatchMailbox {
  private tail: Promise<unknown> = Promise.resolve();

  constructor(private readonly metrics?: ZLinkRuntimeMetrics) {}

  submit<T>(operation: () => Promise<T> | T): Promise<T> {
    this.metrics?.change('zlink.actor.mailbox.depth', 1);
    const invoke = async (): Promise<T> => {
      this.metrics?.change('zlink.actor.mailbox.depth', -1);
      return await operation();
    };
    const next = this.tail.then(invoke, invoke);
    this.tail = next.catch(() => undefined);
    return next;
  }
}

export class ZLinkActorDispatchMailboxSet {
  private readonly mailboxes = new Map<string, ZLinkActorDispatchMailbox>();

  constructor(private readonly metrics?: ZLinkRuntimeMetrics) {}

  submit<T>(actorId: string, operation: () => Promise<T> | T): Promise<T> {
    let mailbox = this.mailboxes.get(actorId);
    if (mailbox === undefined) {
      mailbox = new ZLinkActorDispatchMailbox(this.metrics);
      this.mailboxes.set(actorId, mailbox);
    }
    return mailbox.submit(operation);
  }
}
