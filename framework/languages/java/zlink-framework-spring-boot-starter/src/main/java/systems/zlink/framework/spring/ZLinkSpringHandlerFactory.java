package systems.zlink.framework.spring;

import org.springframework.beans.factory.NoSuchBeanDefinitionException;
import org.springframework.beans.factory.config.AutowireCapableBeanFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;

final class ZLinkSpringHandlerFactory implements ZLinkHandlerFactory {
    private final AutowireCapableBeanFactory beanFactory;

    ZLinkSpringHandlerFactory(AutowireCapableBeanFactory beanFactory) {
        this.beanFactory = beanFactory;
    }

    @Override
    public Object create(Class<?> handlerType) {
        try {
            return beanFactory.getBean(handlerType);
        } catch (NoSuchBeanDefinitionException ex) {
            return beanFactory.createBean(handlerType);
        }
    }
}
