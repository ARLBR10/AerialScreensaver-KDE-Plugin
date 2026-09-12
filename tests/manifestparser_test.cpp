#include "manifestparser.h"

#include <QFile>
#include <QTest>

class ManifestParserTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesAndFiltersAssets();
    void rejectsMalformedInput();
    void selectsCompatibilityVariant();
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

QTEST_MAIN(ManifestParserTest)
#include "manifestparser_test.moc"
