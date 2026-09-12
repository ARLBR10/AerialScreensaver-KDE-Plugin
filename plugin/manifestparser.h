#pragma once

#include "catalogmodel.h"

#include <QByteArray>

class ManifestParser
{
public:
    static QVector<AerialAsset> parse(const QByteArray &json, QString *error);
    static const AerialVariant *selectVariant(const AerialAsset &asset, const QString &policy);
};
