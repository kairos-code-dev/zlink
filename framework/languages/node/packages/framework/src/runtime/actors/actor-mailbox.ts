export class ZLinkActorDispatchMailbox {
  private tail: Promise<unknown> = Promise.resolve();

  submit<T>(operation: () => Promise<T> | T): Promise<T> {
    const invoke = async (): Promise<T> => {
      return await operation();
    };
    const next = this.tail.then(invoke, invoke);
    this.tail = next.catch(() => undefined);
    return next;
  }
}

export class ZLinkActorDispatchMailboxSet {
  private readonly mailboxes = new Map<string, ZLinkActorDispatchMailbox>();

  submit<T>(actorId: string, operation: () => Promise<T> | T): Promise<T> {
    let mailbox = this.mailboxes.get(actorId);
    if (mailbox === undefined) {
      mailbox = new ZLinkActorDispatchMailbox();
      this.mailboxes.set(actorId, mailbox);
    }
    return mailbox.submit(operation);
  }
}
