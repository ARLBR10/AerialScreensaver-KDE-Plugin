#pragma once

#include "catalogmodel.h"

#include <QByteArray>
#include <QUrl>

class ManifestParser
{
public:
    static QVector<AerialAsset> parse(const QByteArray &json, QString *error);
    static QUrl parseResourcesUrl(const QByteArray &plist, QString *error);
    static const AerialVariant *selectVariant(const AerialAsset &asset, const QString &policy);
};
