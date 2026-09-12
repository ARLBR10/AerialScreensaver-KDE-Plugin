#include "manifestparser.h"

#include <QFile>
#include <QTest>

class ManifestParserTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesAndFiltersAssets();
    void rejectsMalformedInput();
    void parsesMacosDiscovery();
    void mergesDuplicateAssetVariants();
    void selectsCompatibilityVariant();
    void filtersCatalogBySourceAndText();
};

void ManifestParserTest::parsesAndFiltersAssets()
{
    QFile file(QStringLiteral(FIXTURE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString error;
    const auto assets = ManifestParser::parse(file.readAll(), &error);
    QCOMPARE(error, QString());
    QCOMPARE(assets.size(), 2);
    QCOMPARE(assets.at(0).name, QStringLiteral("Korea and Japan at night"));
    QCOMPARE(assets.at(1).name, QStringLiteral("SECOND_SHOT"));
    QCOMPARE(assets.at(0).variants.size(), 2);
}

void ManifestParserTest::parsesMacosDiscovery()
{
    const QByteArray plist = QByteArrayLiteral("<?xml version=\"1.0\"?><plist><dict><key>resources-url</key><string>https://sylvan.apple.com/resources-macos.tar</string></dict></plist>");
    QString error;
    QCOMPARE(ManifestParser::parseResourcesUrl(plist, &error), QUrl(QStringLiteral("https://sylvan.apple.com/resources-macos.tar")));
    QCOMPARE(error, QString());

    QVERIFY(ManifestParser::parseResourcesUrl("<plist><dict></dict></plist>", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(ManifestParser::parseResourcesUrl("<plist><dict><key>resources-url</key><string>https://sylvan.apple.com/a.tar</string>", &error).isEmpty());
}

void ManifestParserTest::mergesDuplicateAssetVariants()
{
    const QByteArray json = QByteArrayLiteral(R"({"assets":[
        {"id":"shared","_aerialSourceLabel":"tvOS 26","url-1080-H264":"https://sylvan.apple.com/shared-1080.mov"},
        {"id":"shared","_aerialSourceLabel":"macOS 26","accessibilityLabel":"macOS landscape","previewImage":"https://sylvan.apple.com/shared.jpg","url-4K-SDR-240FPS":"https://sylvan.apple.com/shared-4k.mov"},
        {"id":"mac-only","localizedNameKey":"unused","accessibilityLabel":"Tahoe","url-4K-SDR-240FPS":"https://sylvan.apple.com/tahoe.mov"}
    ]})");
    QString error;
    const auto assets = ManifestParser::parse(json, &error);
    QCOMPARE(error, QString());
    QCOMPARE(assets.size(), 2);
    QCOMPARE(assets.at(0).name, QStringLiteral("macOS landscape"));
    QCOMPARE(assets.at(0).variants.size(), 2);
    QCOMPARE(assets.at(0).sourceLabels, QStringList({QStringLiteral("tvOS 26"), QStringLiteral("macOS 26")}));
    QCOMPARE(assets.at(0).previewUrl, QUrl(QStringLiteral("https://sylvan.apple.com/shared.jpg")));
    QCOMPARE(assets.at(1).name, QStringLiteral("Tahoe"));
}

void ManifestParserTest::rejectsMalformedInput()
{
    QString error;
    QVERIFY(ManifestParser::parse("{", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    const QByteArray insecure = QByteArrayLiteral("{\"assets\":[{\"id\":\"bad\",\"url-1080-H264\":\"http://example.com/a.mov\"}]}");
    QVERIFY(ManifestParser::parse(insecure, &error).isEmpty());
}

void ManifestParserTest::selectsCompatibilityVariant()
{
    QFile file(QStringLiteral(FIXTURE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString error;
    const auto assets = ManifestParser::parse(file.readAll(), &error);
    const auto *variant = ManifestParser::selectVariant(assets.constFirst(), QStringLiteral("compatibility"));
    QVERIFY(variant);
    QCOMPARE(variant->codec, QStringLiteral("h264"));
}

void ManifestParserTest::filtersCatalogBySourceAndText()
{
    CatalogModel model;
    QVector<AerialAsset> assets;
    auto addAsset = [&assets](const QString &id, const QString &name, const QStringList &sourceLabels) {
        AerialAsset asset;
        asset.id = id;
        asset.name = name;
        asset.sourceLabels = sourceLabels;
        assets.push_back(std::move(asset));
    };
    addAsset(QStringLiteral("mac-mountains"), QStringLiteral("Mountain Lake"), {QStringLiteral("macOS 26")});
    addAsset(QStringLiteral("tvos-city"), QStringLiteral("City at Night"), {QStringLiteral("tvOS 26")});
    addAsset(QStringLiteral("shared-coast"), QStringLiteral("Coastal Cliffs"), {QStringLiteral("macOS 26"), QStringLiteral("tvOS 26")});
    model.setAssets(std::move(assets));

    QCOMPARE(model.rowCount(), 3);

    model.setTvosEnabled(false);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), CatalogModel::AssetIdRole).toString(), QStringLiteral("mac-mountains"));
    QCOMPARE(model.data(model.index(1), CatalogModel::AssetIdRole).toString(), QStringLiteral("shared-coast"));

    model.setSearchText(QStringLiteral("CLIFF"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), CatalogModel::AssetIdRole).toString(), QStringLiteral("shared-coast"));

    model.setMacosEnabled(false);
    QCOMPARE(model.rowCount(), 0);
    model.setTvosEnabled(true);
    QCOMPARE(model.rowCount(), 1);

    model.setSearchText(QStringLiteral("tvos-city"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), CatalogModel::NameRole).toString(), QStringLiteral("City at Night"));
}

QTEST_MAIN(ManifestParserTest)
#include "manifestparser_test.moc"
