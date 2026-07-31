package systems.zlink.samples.gamequest.server.gameapi.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

public final class GameQuestPlayerActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;

    public GameQuestPlayerActor(String actorId, ZLinkActorContext context) {
        this.actorId = actorId;
        this.context = context;
    }

    public String actorId() {
        return actorId;
    }

    @Override
    public ZLinkActorContext context() {
        return context;
    }

    public void push(Messages.QuestProcessingMsg message) {
        message.progressNotifications().forEach(notification ->
            context.boundSession().send(notification).submit());
        message.completedNotifications().forEach(notification ->
            context.boundSession().send(notification).submit());
    }
}
