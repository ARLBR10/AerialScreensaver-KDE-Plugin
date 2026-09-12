#include <QQmlEngineExtensionPlugin>

class AerialPlugin final : public QQmlEngineExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
};

#include "plugin.moc"
