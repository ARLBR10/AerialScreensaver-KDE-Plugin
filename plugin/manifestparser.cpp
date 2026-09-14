#include "manifestparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QXmlStreamReader>

#include <algorithm>

namespace
{
struct VariantDescription {
    const char *key;
    const char *codec;
    const char *range;
    int height;
};

constexpr VariantDescription variantDescriptions[] = {
    {"url-1080-H264", "h264", "sdr", 1080},
    {"url-1080-SDR", "hevc", "sdr", 1080},
    {"url-4K-SDR", "hevc", "sdr", 2160},
    {"url-1080-HDR", "hevc", "hdr", 1080},
    {"url-4K-HDR", "hevc", "hdr", 2160},
    {"url-4K-SDR-120FPS", "hevc", "sdr", 2160},
    {"url-4K-SDR-240FPS", "hevc", "sdr", 2160},
};

bool validAppleUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && url.host().compare(QStringLiteral("sylvan.apple.com"), Qt::CaseInsensitive) == 0;
}
}

QVector<AerialAsset> ManifestParser::parse(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid catalog JSON: %1").arg(parseError.errorString());
        }
        return {};
    }

    QVector<AerialAsset> result;
    QHash<QString, qsizetype> assetIndexes;
    const auto values = document.object().value(QStringLiteral("assets"));
    if (!values.isArray()) {
        if (error) {
            *error = QStringLiteral("Catalog has no assets array");
        }
        return {};
    }

    for (const auto &value : values.toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const auto object = value.toObject();
        AerialAsset asset;
        asset.id = object.value(QStringLiteral("id")).toString().trimmed();
        asset.shotId = object.value(QStringLiteral("shotID")).toString().trimmed();
        const QString sourceLabel = object.value(QStringLiteral("_aerialSourceLabel")).toString().trimmed();
        if (!sourceLabel.isEmpty()) {
            asset.sourceLabels.push_back(sourceLabel);
        }
        asset.name = object.value(QStringLiteral("accessibilityLabel")).toString().trimmed();
        if (asset.name.isEmpty()) {
            asset.name = asset.shotId.isEmpty() ? asset.id : asset.shotId;
        }
        const QUrl preview(object.value(QStringLiteral("previewImage")).toString());
        if (validAppleUrl(preview)) {
            asset.previewUrl = preview;
        }
        for (const auto &description : variantDescriptions) {
            const QUrl url(object.value(QLatin1String(description.key)).toString().trimmed());
            if (validAppleUrl(url)) {
                asset.variants.push_back({QLatin1String(description.key), url, QLatin1String(description.codec), QLatin1String(description.range), description.height});
            }
        }
        if (asset.id.isEmpty() || asset.variants.isEmpty()) {
            continue;
        }
        const auto existingIndex = assetIndexes.constFind(asset.id);
        if (existingIndex == assetIndexes.cend()) {
            assetIndexes.insert(asset.id, result.size());
            result.push_back(std::move(asset));
            continue;
        }

        auto &existing = result[*existingIndex];
        if (existing.previewUrl.isEmpty()) {
            existing.previewUrl = asset.previewUrl;
        }
        if (existing.shotId.isEmpty()) {
            existing.shotId = asset.shotId;
        }
        if (existing.name == existing.id && asset.name != asset.id) {
            existing.name = asset.name;
        }
        for (const auto &sourceLabel : std::as_const(asset.sourceLabels)) {
            if (!existing.sourceLabels.contains(sourceLabel)) {
                existing.sourceLabels.push_back(sourceLabel);
            }
        }
        for (auto &variant : asset.variants) {
            const auto duplicate = std::find_if(existing.variants.cbegin(), existing.variants.cend(), [&variant](const AerialVariant &candidate) {
                return candidate.key == variant.key;
            });
            if (duplicate == existing.variants.cend()) {
                existing.variants.push_back(std::move(variant));
            }
        }
    }

    if (result.isEmpty() && error) {
        *error = QStringLiteral("Catalog contains no playable assets");
    }
    return result;
}

QUrl ManifestParser::parseResourcesUrl(const QByteArray &plist, QString *error)
{
    QXmlStreamReader xml(plist);
    QString currentKey;
    QUrl resourcesUrl;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        if (xml.name() == QLatin1String("key")) {
            currentKey = xml.readElementText().trimmed();
        } else if (xml.name() == QLatin1String("string")) {
            const QString value = xml.readElementText().trimmed();
            if (currentKey == QStringLiteral("resources-url") && !value.isEmpty()) {
                resourcesUrl = QUrl(value);
            }
            currentKey.clear();
        }
    }
    if (!xml.hasError() && !resourcesUrl.isEmpty()) {
        return resourcesUrl;
    }
    if (error) {
        *error = xml.hasError() ? QStringLiteral("Invalid resources plist: %1").arg(xml.errorString())
                               : QStringLiteral("Resources plist has no resources-url");
    }
    return {};
}

const AerialVariant *ManifestParser::selectVariant(const AerialAsset &asset, const QString &policy)
{
    QStringList preferred;
    if (policy == QStringLiteral("hdr")) {
        preferred = {QStringLiteral("url-4K-HDR"), QStringLiteral("url-1080-HDR")};
    } else if (policy == QStringLiteral("4k-sdr")) {
        preferred = {
            QStringLiteral("url-4K-SDR"),
            QStringLiteral("url-4K-SDR-120FPS"),
            QStringLiteral("url-4K-SDR-240FPS"),
        };
    } else if (policy == QStringLiteral("1080-sdr")) {
        preferred = {QStringLiteral("url-1080-SDR"), QStringLiteral("url-1080-H264")};
    } else {
        preferred = {QStringLiteral("url-1080-H264")};
    }
    preferred << QStringLiteral("url-1080-H264") << QStringLiteral("url-1080-SDR") << QStringLiteral("url-4K-SDR");
    for (const auto &key : std::as_const(preferred)) {
        for (const auto &variant : asset.variants) {
            if (variant.key == key) {
                return &variant;
            }
        }
    }
    return asset.variants.isEmpty() ? nullptr : &asset.variants.constFirst();
}
