#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QObject>
#include <QTest>

namespace
{
class MockTimer final : public QObject
{
    Q_OBJECT

public:
    explicit MockTimer(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    int restartCount() const { return m_restartCount; }
    int stopCount() const { return m_stopCount; }
    bool running() const { return m_running; }

public Q_SLOTS:
    void restart()
    {
        ++m_restartCount;
        m_running = true;
    }

    void stop()
    {
        ++m_stopCount;
        m_running = false;
    }

    void start() { restart(); }

private:
    int m_restartCount = 0;
    int m_stopCount = 0;
    bool m_running = false;
};

class MockPlayer final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource)
    Q_PROPERTY(int error READ error WRITE setError)
    Q_PROPERTY(int mediaStatus READ mediaStatus WRITE setMediaStatus)
    Q_PROPERTY(qreal duration READ duration WRITE setDuration)
    Q_PROPERTY(qreal position READ position WRITE setPosition)

public:
    explicit MockPlayer(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    QString source() const { return m_source; }
    int error() const { return m_error; }
    int mediaStatus() const { return m_mediaStatus; }
    qreal duration() const { return m_duration; }
    qreal position() const { return m_position; }
    int playCount() const { return m_playCount; }
    int stopCount() const { return m_stopCount; }

    void setSource(const QString &source) { m_source = source; }
    void setError(int error) { m_error = error; }
    void setMediaStatus(int mediaStatus) { m_mediaStatus = mediaStatus; }
    void setDuration(qreal duration) { m_duration = duration; }
    void setPosition(qreal position) { m_position = position; }

public Q_SLOTS:
    void play() { ++m_playCount; }
    void stop() { ++m_stopCount; }

private:
    QString m_source;
    int m_error = 0;
    int m_mediaStatus = 0;
    qreal m_duration = 0;
    qreal m_position = 0;
    int m_playCount = 0;
    int m_stopCount = 0;
};

class MockVideoSize final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int width READ width WRITE setWidth)

public:
    explicit MockVideoSize(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    int width() const { return m_width; }
    void setWidth(int width) { m_width = width; }

private:
    int m_width = 0;
};

class MockVideoSink final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *videoSize READ videoSize CONSTANT)

public:
    explicit MockVideoSink(QObject *parent = nullptr)
        : QObject(parent)
        , m_videoSize(new MockVideoSize(this))
    {
    }

    QObject *videoSize() const { return m_videoSize; }
    MockVideoSize *size() const { return m_videoSize; }

private:
    MockVideoSize *m_videoSize;
};

class MockOutput final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool frameReady READ frameReady WRITE setFrameReady)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
    Q_PROPERTY(QObject *videoSink READ videoSink CONSTANT)

public:
    explicit MockOutput(QObject *parent = nullptr)
        : QObject(parent)
        , m_videoSink(new MockVideoSink(this))
    {
    }

    bool frameReady() const { return m_frameReady; }
    qreal opacity() const { return m_opacity; }
    QObject *videoSink() const { return m_videoSink; }
    MockVideoSink *sink() const { return m_videoSink; }

    void setFrameReady(bool frameReady) { m_frameReady = frameReady; }
    void setOpacity(qreal opacity) { m_opacity = opacity; }

private:
    MockVideoSink *m_videoSink;
    bool m_frameReady = false;
    qreal m_opacity = 0;
};

class MockBackend final : public QObject
{
    Q_OBJECT

public:
    explicit MockBackend(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    QStringList assetIds() const { return m_assetIds; }
    QStringList ensureIds;
    QStringList markedPlayingUrls;

    void setAssetIds(const QStringList &assetIds) { m_assetIds = assetIds; }

public Q_SLOTS:
    void ensureDownloaded(const QString &assetId, const QString &, int)
    {
        ensureIds.append(assetId);
    }

    void markPlaying(const QString &localUrl, bool playing)
    {
        if (playing) {
            markedPlayingUrls.append(localUrl);
        }
    }

    void refreshCatalog() { }

private:
    QStringList m_assetIds;
};

struct ExtractedFunction
{
    QString source;
    int line = 1;
};

bool isEscaped(const QString &source, int position)
{
    int backslashes = 0;
    for (int index = position - 1; index >= 0 && source.at(index) == QLatin1Char('\\'); --index) {
        ++backslashes;
    }
    return (backslashes % 2) != 0;
}

bool extractFunction(const QString &qml, const QString &name, ExtractedFunction *function, QString *error)
{
    const QString marker = QStringLiteral("function ") + name + QLatin1Char('(');
    const int start = qml.indexOf(marker);
    if (start < 0) {
        if (error) {
            *error = QStringLiteral("Could not find function %1").arg(name);
        }
        return false;
    }

    const int closeParenthesis = qml.indexOf(QLatin1Char(')'), start + marker.size());
    const int openBrace = closeParenthesis < 0 ? -1 : qml.indexOf(QLatin1Char('{'), closeParenthesis);
    if (openBrace < 0) {
        if (error) {
            *error = QStringLiteral("Could not find body for function %1").arg(name);
        }
        return false;
    }

    enum class State { Normal, SingleQuote, DoubleQuote, LineComment, BlockComment };
    State state = State::Normal;
    int depth = 1;
    for (int index = openBrace + 1; index < qml.size(); ++index) {
        const QChar character = qml.at(index);
        const QChar next = index + 1 < qml.size() ? qml.at(index + 1) : QChar();
        switch (state) {
        case State::Normal:
            if (character == QLatin1Char('/') && next == QLatin1Char('/')) {
                state = State::LineComment;
                ++index;
            } else if (character == QLatin1Char('/') && next == QLatin1Char('*')) {
                state = State::BlockComment;
                ++index;
            } else if (character == QLatin1Char('\'')) {
                state = State::SingleQuote;
            } else if (character == QLatin1Char('"')) {
                state = State::DoubleQuote;
            } else if (character == QLatin1Char('{')) {
                ++depth;
            } else if (character == QLatin1Char('}') && --depth == 0) {
                function->source = qml.mid(start, index - start + 1);
                function->line = qml.left(start).count(QLatin1Char('\n')) + 1;
                return true;
            }
            break;
        case State::SingleQuote:
            if (character == QLatin1Char('\'') && !isEscaped(qml, index)) {
                state = State::Normal;
            }
            break;
        case State::DoubleQuote:
            if (character == QLatin1Char('"') && !isEscaped(qml, index)) {
                state = State::Normal;
            }
            break;
        case State::LineComment:
            if (character == QLatin1Char('\n')) {
                state = State::Normal;
            }
            break;
        case State::BlockComment:
            if (character == QLatin1Char('*') && next == QLatin1Char('/')) {
                state = State::Normal;
                ++index;
            }
            break;
        }
    }

    if (error) {
        *error = QStringLiteral("Unbalanced body for function %1").arg(name);
    }
    return false;
}

QString extractBinding(const QString &qml, const QString &propertyMarker, const QString &nextMarker, QString *error)
{
    const int startMarker = qml.indexOf(propertyMarker);
    const int expressionStart = startMarker < 0 ? -1 : startMarker + propertyMarker.size();
    const int expressionEnd = expressionStart < 0 ? -1 : qml.indexOf(nextMarker, expressionStart);
    if (expressionStart < 0 || expressionEnd < 0) {
        if (error) {
            *error = QStringLiteral("Could not find binding %1").arg(propertyMarker);
        }
        return {};
    }
    return qml.mid(expressionStart, expressionEnd - expressionStart).trimmed();
}

QString extractLineExpression(const QString &qml, const QString &scopeMarker, const QString &expressionMarker, QString *error)
{
    const int scopeStart = qml.indexOf(scopeMarker);
    const int expressionStart = scopeStart < 0 ? -1 : qml.indexOf(expressionMarker, scopeStart);
    const int expressionEnd = expressionStart < 0 ? -1 : qml.indexOf(QLatin1Char('\n'), expressionStart);
    if (expressionStart < 0 || expressionEnd < 0) {
        if (error) {
            *error = QStringLiteral("Could not find handler %1 in %2").arg(expressionMarker, scopeMarker);
        }
        return {};
    }
    return qml.mid(expressionStart + expressionMarker.size(), expressionEnd - expressionStart - expressionMarker.size()).trimmed();
}

class MainQmlHarness final
{
    QObject objects;

public:
    MainQmlHarness()
        : playerOne(&objects)
        , playerTwo(&objects)
        , outputOne(&objects)
        , outputTwo(&objects)
        , backend(&objects)
        , activeFrameTimeout(&objects)
        , firstFrameTimeout(&objects)
        , retryTimer(&objects)
        , prefetchTimer(&objects)
        , crossfadeAnimation(&objects)
        , engine()
    {
    }

    bool load(QString *error)
    {
        QFile file(QStringLiteral(MAIN_QML_PATH));
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = file.errorString();
            }
            return false;
        }
        const QString qml = QString::fromUtf8(file.readAll());
        installObjects();

        const QStringList functionNames = {
            QStringLiteral("rebuildQueue"),
            QStringLiteral("clearPrepared"),
            QStringLiteral("activate"),
            QStringLiteral("prefetchNext"),
            QStringLiteral("nextPlayableIndex"),
            QStringLiteral("rejectAsset"),
            QStringLiteral("next"),
            QStringLiteral("maybeBeginCrossfade"),
            QStringLiteral("beginCrossfade"),
            QStringLiteral("handleVideoFrame"),
            QStringLiteral("finishCrossfade"),
            QStringLiteral("cancelCrossfade"),
            QStringLiteral("releaseActive"),
            QStringLiteral("failCrossfade"),
            QStringLiteral("handleMediaStatus"),
            QStringLiteral("handlePlaybackError"),
            QStringLiteral("onPlayableReady"),
            QStringLiteral("onOperationFailed"),
        };
        for (const QString &name : functionNames) {
            ExtractedFunction function;
            QString extractionError;
            if (!extractFunction(qml, name, &function, &extractionError)) {
                if (error) {
                    *error = extractionError;
                }
                return false;
            }
            const QJSValue result = engine.evaluate(function.source, QStringLiteral(MAIN_QML_PATH), function.line);
            if (result.isError()) {
                if (error) {
                    *error = QStringLiteral("Could not evaluate %1: %2").arg(name, result.toString());
                }
                return false;
            }
        }

        QString bindingError;
        const QString binding = extractBinding(qml,
                                               QStringLiteral("readonly property bool hasVisibleVideo:"),
                                               QStringLiteral("property var failedAssetIds"),
                                               &bindingError);
        if (binding.isEmpty()) {
            if (error) {
                *error = bindingError;
            }
            return false;
        }
        const QJSValue result = engine.evaluate(QStringLiteral("function __aerialHasVisibleVideo() { return (%1); }").arg(binding),
                                                QStringLiteral(MAIN_QML_PATH));
        if (result.isError()) {
            if (error) {
                *error = QStringLiteral("Could not evaluate hasVisibleVideo: %1").arg(result.toString());
            }
            return false;
        }

        QString timerError;
        const QString activeFrameHandler = extractLineExpression(qml,
                                                                 QStringLiteral("id: activeFrameTimeout"),
                                                                 QStringLiteral("onTriggered:"),
                                                                 &timerError);
        if (activeFrameHandler.isEmpty()) {
            if (error) {
                *error = timerError;
            }
            return false;
        }
        const QJSValue timerResult = engine.evaluate(QStringLiteral("function __aerialActiveFrameTimeout() { %1; }").arg(activeFrameHandler),
                                                     QStringLiteral(MAIN_QML_PATH));
        if (timerResult.isError()) {
            if (error) {
                *error = QStringLiteral("Could not evaluate activeFrameTimeout: %1").arg(timerResult.toString());
            }
            return false;
        }

        syncRoot();
        return true;
    }

    QJSValue call(const QString &name, const QJSValueList &arguments = {})
    {
        const QJSValue function = engine.globalObject().property(name);
        if (!function.isCallable()) {
            return QJSValue(QStringLiteral("Function is not callable: ") + name);
        }
        return function.call(arguments);
    }

    QJSValue callConnection(const QString &name, const QJSValueList &arguments)
    {
        syncRoot();
        return call(name, arguments);
    }

    QJSValue callVisibleBinding() { return call(QStringLiteral("__aerialHasVisibleVideo")); }

    QJSValue callActiveFrameTimeout()
    {
        syncRoot();
        return call(QStringLiteral("__aerialActiveFrameTimeout"));
    }

    QJSValue global(const QString &name) const { return engine.globalObject().property(name); }

    void setQueue(const QStringList &ids)
    {
        QJSValue queue = engine.newArray(ids.size());
        for (int index = 0; index < ids.size(); ++index) {
            queue.setProperty(static_cast<quint32>(index), QJSValue(ids.at(index)));
        }
        engine.globalObject().setProperty(QStringLiteral("queue"), queue);
        syncRoot();
    }

    void setGlobalString(const QString &name, const QString &value)
    {
        engine.globalObject().setProperty(name, QJSValue(value));
        syncRoot();
    }

    void setGlobalInt(const QString &name, int value)
    {
        engine.globalObject().setProperty(name, QJSValue(value));
        syncRoot();
    }

    QString globalString(const QString &name) const { return global(name).toString(); }
    int globalInt(const QString &name) const { return global(name).toInt(); }

    QStringList failedAssetIds() const
    {
        const QJSValue ids = global(QStringLiteral("failedAssetIds"));
        const int length = ids.property(QStringLiteral("length")).toInt();
        QStringList result;
        for (int index = 0; index < length; ++index) {
            result.append(ids.property(static_cast<quint32>(index)).toString());
        }
        return result;
    }

    MockPlayer playerOne;
    MockPlayer playerTwo;
    MockOutput outputOne;
    MockOutput outputTwo;
    MockBackend backend;
    MockTimer activeFrameTimeout;
    MockTimer firstFrameTimeout;
    MockTimer retryTimer;
    MockTimer prefetchTimer;
    MockTimer crossfadeAnimation;

private:
    void installObjects()
    {
        auto expose = [this](const QString &name, QObject *object) {
            engine.globalObject().setProperty(name, engine.newQObject(object));
        };
        expose(QStringLiteral("playerOne"), &playerOne);
        expose(QStringLiteral("playerTwo"), &playerTwo);
        expose(QStringLiteral("outputOne"), &outputOne);
        expose(QStringLiteral("outputTwo"), &outputTwo);
        expose(QStringLiteral("AerialBackend"), &backend);
        expose(QStringLiteral("activeFrameTimeout"), &activeFrameTimeout);
        expose(QStringLiteral("firstFrameTimeout"), &firstFrameTimeout);
        expose(QStringLiteral("retryTimer"), &retryTimer);
        expose(QStringLiteral("prefetchTimer"), &prefetchTimer);
        expose(QStringLiteral("crossfadeAnimation"), &crossfadeAnimation);

        QJSValue mediaPlayer = engine.newObject();
        mediaPlayer.setProperty(QStringLiteral("NoError"), QJSValue(0));
        mediaPlayer.setProperty(QStringLiteral("EndOfMedia"), QJSValue(1));
        engine.globalObject().setProperty(QStringLiteral("MediaPlayer"), mediaPlayer);

        QJSValue console = engine.newObject();
        console.setProperty(QStringLiteral("warn"), engine.evaluate(QStringLiteral("(function() {})")));
        engine.globalObject().setProperty(QStringLiteral("console"), console);

        QJSValue configuration = engine.newObject();
        configuration.setProperty(QStringLiteral("SelectedAssetIds"), engine.newArray());
        configuration.setProperty(QStringLiteral("QualityPolicy"), QJSValue(QStringLiteral("compatibility")));
        configuration.setProperty(QStringLiteral("PlaybackOrder"), QJSValue(QStringLiteral("sequential")));
        configuration.setProperty(QStringLiteral("FillMode"), QJSValue(1));
        configuration.setProperty(QStringLiteral("CrossfadeDurationMs"), QJSValue(1000));
        configuration.setProperty(QStringLiteral("CacheLimitMiB"), QJSValue(256));
        engine.globalObject().setProperty(QStringLiteral("configuration"), configuration);
        engine.globalObject().setProperty(QStringLiteral("configuredIds"), configuration.property(QStringLiteral("SelectedAssetIds")));

        engine.globalObject().setProperty(QStringLiteral("queue"), engine.newArray());
        engine.globalObject().setProperty(QStringLiteral("failedAssetIds"), engine.newArray());
        engine.globalObject().setProperty(QStringLiteral("queueIndex"), QJSValue(-1));
        engine.globalObject().setProperty(QStringLiteral("consecutiveFailures"), QJSValue(0));
        engine.globalObject().setProperty(QStringLiteral("useFirstPlayer"), QJSValue(true));
        engine.globalObject().setProperty(QStringLiteral("transitionState"), QJSValue(0));
        engine.globalObject().setProperty(QStringLiteral("activeUrl"), QJSValue(QString()));
        engine.globalObject().setProperty(QStringLiteral("activeName"), QJSValue(QString()));
        engine.globalObject().setProperty(QStringLiteral("activeAssetId"), QJSValue(QString()));
        engine.globalObject().setProperty(QStringLiteral("playbackError"), QJSValue(QString()));
        engine.globalObject().setProperty(QStringLiteral("preparedAssetId"), QJSValue(QString()));
        engine.globalObject().setProperty(QStringLiteral("preparedName"), QJSValue(QString()));
        engine.globalObject().setProperty(QStringLiteral("preparedUrl"), QJSValue(QString()));
        engine.globalObject().setProperty(QStringLiteral("preparedQueueIndex"), QJSValue(-1));

        const QJSValue aliases = engine.evaluate(QStringLiteral(R"(
(function () {
    Object.defineProperty(this, "activePlayer", {
        configurable: true,
        get: function() { return useFirstPlayer ? playerOne : playerTwo; }
    });
    Object.defineProperty(this, "standbyPlayer", {
        configurable: true,
        get: function() { return useFirstPlayer ? playerTwo : playerOne; }
    });
    Object.defineProperty(this, "activeOutput", {
        configurable: true,
        get: function() { return useFirstPlayer ? outputOne : outputTwo; }
    });
    Object.defineProperty(this, "standbyOutput", {
        configurable: true,
        get: function() { return useFirstPlayer ? outputTwo : outputOne; }
    });
})()
)"));
        Q_UNUSED(aliases)

        m_root = engine.newObject();
        engine.globalObject().setProperty(QStringLiteral("root"), m_root);
    }

    void syncRoot()
    {
        if (!m_root.isObject()) {
            return;
        }
        m_root.setProperty(QStringLiteral("queue"), global(QStringLiteral("queue")));
        m_root.setProperty(QStringLiteral("failedAssetIds"), global(QStringLiteral("failedAssetIds")));
        m_root.setProperty(QStringLiteral("queueIndex"), global(QStringLiteral("queueIndex")));
        m_root.setProperty(QStringLiteral("transitionState"), global(QStringLiteral("transitionState")));
        m_root.setProperty(QStringLiteral("activePlayer"), global(QStringLiteral("activePlayer")));
        m_root.setProperty(QStringLiteral("preparedAssetId"), global(QStringLiteral("preparedAssetId")));
        m_root.setProperty(QStringLiteral("preparedName"), global(QStringLiteral("preparedName")));
        m_root.setProperty(QStringLiteral("preparedUrl"), global(QStringLiteral("preparedUrl")));
        m_root.setProperty(QStringLiteral("preparedQueueIndex"), global(QStringLiteral("preparedQueueIndex")));
        m_root.setProperty(QStringLiteral("nextPlayableIndex"), global(QStringLiteral("nextPlayableIndex")));
        m_root.setProperty(QStringLiteral("rejectAsset"), global(QStringLiteral("rejectAsset")));
        m_root.setProperty(QStringLiteral("activate"), global(QStringLiteral("activate")));
        m_root.setProperty(QStringLiteral("clearPrepared"), global(QStringLiteral("clearPrepared")));
        m_root.setProperty(QStringLiteral("handlePlaybackError"), global(QStringLiteral("handlePlaybackError")));
    }

    QJSEngine engine;
    QJSValue m_root;
};
}

class MainQmlTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void finiteAttemptsAcrossBrokenCachedSources();
    void activeStartupWatchdogIsBounded();
    void firstFrameTimeoutReleasesStandbySource();
    void rejectedIdsAreSkippedAfterPrefetchFailure();
    void firstFrameResetPreservesPrefetchFailure();
    void fallbackWaitsForFrameReady();
};

void MainQmlTest::finiteAttemptsAcrossBrokenCachedSources()
{
    MainQmlHarness harness;
    QString error;
    QVERIFY2(harness.load(&error), qPrintable(error));

    const QStringList ids = {
        QStringLiteral("broken-one"),
        QStringLiteral("broken-two"),
        QStringLiteral("broken-three"),
        QStringLiteral("healthy-four"),
    };
    harness.setQueue(ids);

    QJSValue result = harness.call(QStringLiteral("next"));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.backend.ensureIds, QStringList({QStringLiteral("broken-one")}));

    for (int index = 0; index < 3; ++index) {
        const QString assetId = ids.at(index);
        result = harness.call(QStringLiteral("activate"),
                              {QJSValue(assetId), QJSValue(assetId), QJSValue(QStringLiteral("file:///cache/") + assetId + QStringLiteral(".mov"))});
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(harness.playerOne.source(), QStringLiteral("file:///cache/") + assetId + QStringLiteral(".mov"));

        result = harness.call(QStringLiteral("handlePlaybackError"),
                              {harness.global(QStringLiteral("playerOne")), QJSValue(QStringLiteral("cached source failed"))});
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(harness.playerOne.source(), QString());

        result = harness.call(QStringLiteral("next"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
    }

    QCOMPARE(harness.backend.ensureIds,
             QStringList({QStringLiteral("broken-one"), QStringLiteral("broken-two"), QStringLiteral("broken-three")}));
    QCOMPARE(harness.failedAssetIds(), QStringList({QStringLiteral("broken-one"), QStringLiteral("broken-two"), QStringLiteral("broken-three")}));
    QCOMPARE(harness.globalInt(QStringLiteral("consecutiveFailures")), 3);
    QCOMPARE(harness.globalString(QStringLiteral("activeUrl")), QString());
    QVERIFY(!harness.backend.ensureIds.contains(QStringLiteral("healthy-four")));

    for (int attempt = 0; attempt < 5; ++attempt) {
        result = harness.call(QStringLiteral("next"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
    }
    QCOMPARE(harness.backend.ensureIds.size(), 3);
}

void MainQmlTest::activeStartupWatchdogIsBounded()
{
    MainQmlHarness harness;
    QString error;
    QVERIFY2(harness.load(&error), qPrintable(error));

    harness.setQueue({QStringLiteral("startup-broken")});
    QJSValue result = harness.call(QStringLiteral("next"));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.backend.ensureIds, QStringList({QStringLiteral("startup-broken")}));

    result = harness.call(QStringLiteral("activate"),
                          {QJSValue(QStringLiteral("startup-broken")), QJSValue(QStringLiteral("Startup")), QJSValue(QStringLiteral("file:///cache/startup-broken.mov"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(harness.activeFrameTimeout.running());
    QCOMPARE(harness.playerOne.source(), QStringLiteral("file:///cache/startup-broken.mov"));

    result = harness.callActiveFrameTimeout();
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.playerOne.source(), QString());
    QVERIFY(harness.playerOne.stopCount() > 0);
    QVERIFY(harness.activeFrameTimeout.stopCount() > 0);
    QVERIFY(!harness.activeFrameTimeout.running());
    QCOMPARE(harness.failedAssetIds(), QStringList({QStringLiteral("startup-broken")}));
    QCOMPARE(harness.globalInt(QStringLiteral("consecutiveFailures")), 1);
    QVERIFY(!harness.callVisibleBinding().toBool());
    QCOMPARE(harness.retryTimer.restartCount(), 1);

    // A stopped watchdog and the retry timer must not spin on the same bad source.
    result = harness.callActiveFrameTimeout();
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.retryTimer.restartCount(), 1);
    for (int attempt = 0; attempt < 5; ++attempt) {
        result = harness.call(QStringLiteral("next"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
    }
    QCOMPARE(harness.backend.ensureIds.size(), 1);
}

void MainQmlTest::firstFrameTimeoutReleasesStandbySource()
{
    MainQmlHarness harness;
    QString error;
    QVERIFY2(harness.load(&error), qPrintable(error));

    harness.setQueue({QStringLiteral("active"), QStringLiteral("prepared")});
    harness.setGlobalInt(QStringLiteral("queueIndex"), 0);
    harness.outputOne.setFrameReady(true);

    QJSValue result = harness.call(QStringLiteral("activate"),
                                   {QJSValue(QStringLiteral("active")), QJSValue(QStringLiteral("Active")), QJSValue(QStringLiteral("file:///cache/active.mov"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    harness.playerOne.setError(0);
    harness.playerOne.setMediaStatus(0);

    harness.setGlobalString(QStringLiteral("preparedAssetId"), QStringLiteral("prepared"));
    harness.setGlobalString(QStringLiteral("preparedName"), QStringLiteral("Prepared"));
    harness.setGlobalString(QStringLiteral("preparedUrl"), QStringLiteral("file:///cache/prepared.mov"));
    harness.setGlobalInt(QStringLiteral("preparedQueueIndex"), 1);

    result = harness.call(QStringLiteral("beginCrossfade"));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.globalInt(QStringLiteral("transitionState")), 1);
    QCOMPARE(harness.playerTwo.source(), QStringLiteral("file:///cache/prepared.mov"));
    QVERIFY(harness.firstFrameTimeout.running());

    result = harness.call(QStringLiteral("failCrossfade"));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.globalInt(QStringLiteral("transitionState")), 0);
    QCOMPARE(harness.playerTwo.source(), QString());
    QVERIFY(harness.playerTwo.stopCount() > 0);
    QVERIFY(harness.firstFrameTimeout.stopCount() > 0);
    QVERIFY(!harness.firstFrameTimeout.running());
    QCOMPARE(harness.globalString(QStringLiteral("preparedUrl")), QString());
    QCOMPARE(harness.playerOne.source(), QStringLiteral("file:///cache/active.mov"));
    QCOMPARE(harness.globalString(QStringLiteral("activeAssetId")), QStringLiteral("active"));
}

void MainQmlTest::rejectedIdsAreSkippedAfterPrefetchFailure()
{
    MainQmlHarness harness;
    QString error;
    QVERIFY2(harness.load(&error), qPrintable(error));

    harness.setQueue({QStringLiteral("active"), QStringLiteral("prefetch-broken"), QStringLiteral("later" )});
    harness.setGlobalInt(QStringLiteral("queueIndex"), 0);
    QJSValue result = harness.call(QStringLiteral("activate"),
                                   {QJSValue(QStringLiteral("active")), QJSValue(QStringLiteral("Active")), QJSValue(QStringLiteral("file:///cache/active.mov"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    harness.playerOne.setError(0);
    harness.playerOne.setMediaStatus(0);
    const int prefetchRestartsBeforeFailure = harness.prefetchTimer.restartCount();
    const int retryRestartsBeforeFailure = harness.retryTimer.restartCount();

    result = harness.callConnection(QStringLiteral("onOperationFailed"),
                                    {QJSValue(QStringLiteral("prefetch-broken")), QJSValue(QStringLiteral("broken cached prefetch"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.failedAssetIds(), QStringList({QStringLiteral("prefetch-broken")}));
    QCOMPARE(harness.globalInt(QStringLiteral("consecutiveFailures")), 1);
    QCOMPARE(harness.prefetchTimer.restartCount(), prefetchRestartsBeforeFailure + 1);
    QCOMPARE(harness.retryTimer.restartCount(), retryRestartsBeforeFailure);

    result = harness.call(QStringLiteral("prefetchNext"));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.backend.ensureIds, QStringList({QStringLiteral("later")}));

    const int marksBeforeRejectedReady = harness.backend.markedPlayingUrls.size();
    result = harness.callConnection(QStringLiteral("onPlayableReady"),
                                    {QJSValue(QStringLiteral("prefetch-broken")), QJSValue(QStringLiteral("Rejected")), QJSValue(QStringLiteral("file:///cache/rejected.mov"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.globalString(QStringLiteral("preparedAssetId")), QString());
    QCOMPARE(harness.backend.markedPlayingUrls.size(), marksBeforeRejectedReady);
    QCOMPARE(harness.playerOne.source(), QStringLiteral("file:///cache/active.mov"));
}

void MainQmlTest::firstFrameResetPreservesPrefetchFailure()
{
    MainQmlHarness harness;
    QString error;
    QVERIFY2(harness.load(&error), qPrintable(error));

    harness.setQueue({QStringLiteral("active"), QStringLiteral("prefetch-broken"), QStringLiteral("later")});
    harness.setGlobalInt(QStringLiteral("queueIndex"), 0);
    QJSValue result = harness.call(QStringLiteral("activate"),
                                   {QJSValue(QStringLiteral("active")), QJSValue(QStringLiteral("Active")), QJSValue(QStringLiteral("file:///cache/active.mov"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    result = harness.call(QStringLiteral("rejectAsset"), {QJSValue(QStringLiteral("previous-one"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    result = harness.call(QStringLiteral("rejectAsset"), {QJSValue(QStringLiteral("previous-two"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.globalInt(QStringLiteral("consecutiveFailures")), 2);

    harness.playerOne.setError(0);
    harness.playerOne.setMediaStatus(0);
    harness.outputOne.sink()->size()->setWidth(1920);
    result = harness.call(QStringLiteral("handleVideoFrame"),
                          {harness.global(QStringLiteral("playerOne")), harness.global(QStringLiteral("outputOne"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(harness.outputOne.frameReady());
    QCOMPARE(harness.globalInt(QStringLiteral("consecutiveFailures")), 0);

    result = harness.callConnection(QStringLiteral("onOperationFailed"),
                                    {QJSValue(QStringLiteral("prefetch-broken")), QJSValue(QStringLiteral("broken cached prefetch"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.failedAssetIds(),
             QStringList({QStringLiteral("previous-one"), QStringLiteral("previous-two"), QStringLiteral("prefetch-broken")}));
    QCOMPARE(harness.globalInt(QStringLiteral("consecutiveFailures")), 1);

    // Once the active output has delivered its first frame, later frames are not recovery events.
    result = harness.call(QStringLiteral("handleVideoFrame"),
                          {harness.global(QStringLiteral("playerOne")), harness.global(QStringLiteral("outputOne"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(harness.globalInt(QStringLiteral("consecutiveFailures")), 1);
    QVERIFY(harness.failedAssetIds().contains(QStringLiteral("prefetch-broken")));
}

void MainQmlTest::fallbackWaitsForFrameReady()
{
    MainQmlHarness harness;
    QString error;
    QVERIFY2(harness.load(&error), qPrintable(error));

    harness.playerOne.setSource(QStringLiteral("file:///cache/not-yet-decoded.mov"));
    harness.playerOne.setError(0);
    harness.outputOne.setOpacity(1);
    harness.outputTwo.setOpacity(0);
    harness.outputOne.setFrameReady(false);
    harness.outputTwo.setFrameReady(false);

    QJSValue result = harness.callVisibleBinding();
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(!result.toBool());

    harness.outputOne.sink()->size()->setWidth(1920);
    result = harness.call(QStringLiteral("handleVideoFrame"),
                          {harness.global(QStringLiteral("playerOne")), harness.global(QStringLiteral("outputOne"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(harness.outputOne.frameReady());
    QVERIFY(harness.activeFrameTimeout.restartCount() > 0);

    result = harness.callVisibleBinding();
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(result.toBool());

    harness.outputOne.setOpacity(0);
    harness.outputTwo.setOpacity(1);
    result = harness.callVisibleBinding();
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(!result.toBool());

    harness.outputTwo.setFrameReady(true);
    result = harness.callVisibleBinding();
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QVERIFY(result.toBool());
}

QTEST_MAIN(MainQmlTest)
#include "mainqml_test.moc"
