package systems.zlink.framework.spring;

import java.lang.annotation.Annotation;
import java.lang.reflect.Method;
import org.springframework.beans.BeansException;
import org.springframework.beans.factory.config.AutowireCapableBeanFactory;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkHandlerGroups;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.handlers.ZLinkPublish;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.handlers.ZLinkStreamPacket;
import systems.zlink.framework.handlers.ZLinkStreamRaw;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

final class ZLinkSpringHandlerFactory implements ZLinkHandlerActivator {
    private final AutowireCapableBeanFactory beanFactory;

    ZLinkSpringHandlerFactory(AutowireCapableBeanFactory beanFactory) {
        this.beanFactory = beanFactory;
    }

    @Override
    public Object create(Class<?> handlerType) {
        if (!isZLinkManagedType(handlerType)) {
            try {
                return beanFactory.getBean(handlerType);
            } catch (BeansException ex) {
                return beanFactory.createBean(handlerType);
            }
        }
        try {
            return beanFactory.createBean(handlerType);
        } catch (BeansException ex) {
            return beanFactory.getBean(handlerType);
        }
    }

    private static boolean isZLinkManagedType(Class<?> type) {
        return ZLinkHandlerFilter.class.isAssignableFrom(type)
            || ZLinkActorFactory.class.isAssignableFrom(type)
            || ZLinkSendHandler.class.isAssignableFrom(type)
            || ZLinkRequestHandler.class.isAssignableFrom(type)
            || ZLinkPublishHandler.class.isAssignableFrom(type)
            || ZLinkRouteSendHandler.class.isAssignableFrom(type)
            || ZLinkRouteRequestHandler.class.isAssignableFrom(type)
            || ZLinkSpot.class.isAssignableFrom(type)
            || ZLinkEntrySpot.class.isAssignableFrom(type)
            || ZLinkSpotPacketHandler.class.isAssignableFrom(type)
            || ZLinkSpotRequestHandler.class.isAssignableFrom(type)
            || ZLinkSpotSubscriptionHandler.class.isAssignableFrom(type)
            || ZLinkSpotTimerHandler.class.isAssignableFrom(type)
            || ZLinkEntrySpotActorSendHandler.class.isAssignableFrom(type)
            || ZLinkEntrySpotActorRequestHandler.class.isAssignableFrom(type)
            || ZLinkSpotActorSendHandler.class.isAssignableFrom(type)
            || ZLinkSpotActorRequestHandler.class.isAssignableFrom(type)
            || ZLinkSession.class.isAssignableFrom(type)
            || ZLinkTypedSessionPacketHandler.class.isAssignableFrom(type)
            || hasZLinkHandlerAnnotation(type);
    }

    private static boolean hasZLinkHandlerAnnotation(Class<?> type) {
        if (type.isAnnotationPresent(ZLinkHandlerGroup.class)
            || type.isAnnotationPresent(ZLinkHandlerGroups.class)) {
            return true;
        }
        for (Method method : type.getDeclaredMethods()) {
            for (Annotation annotation : method.getDeclaredAnnotations()) {
                if (annotation.annotationType() == ZLinkSend.class
                    || annotation.annotationType() == ZLinkRequest.class
                    || annotation.annotationType() == ZLinkPublish.class
                    || annotation.annotationType() == ZLinkPacket.class
                    || annotation.annotationType() == ZLinkStreamPacket.class
                    || annotation.annotationType() == ZLinkStreamRaw.class
                    || annotation.annotationType() == ZLinkSpotSubscription.class
                    || annotation.annotationType() == ZLinkSpotRequest.class
                    || annotation.annotationType() == ZLinkSpotActorSend.class
                    || annotation.annotationType() == ZLinkSpotActorRequest.class) {
                    return true;
                }
            }
        }
        return false;
    }
}
