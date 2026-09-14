import QtQuick
import QtMultimedia
import org.kde.plasma.plasmoid
import org.kde.plasma.aerial.private 1.0

WallpaperItem {
    id: root

    readonly property var configuredIds: configuration.SelectedAssetIds || []
    readonly property int crossfadeDuration: Math.min(5000, Math.max(250, configuration.CrossfadeDurationMs || 1000))
    readonly property var activePlayer: useFirstPlayer ? playerOne : playerTwo
    readonly property var standbyPlayer: useFirstPlayer ? playerTwo : playerOne
    readonly property var activeOutput: useFirstPlayer ? outputOne : outputTwo
    readonly property var standbyOutput: useFirstPlayer ? outputTwo : outputOne
    readonly property bool hasVisibleVideo:
        (outputOne.opacity > 0 && outputOne.frameReady)
        || (outputTwo.opacity > 0 && outputTwo.frameReady)
    property var failedAssetIds: []
    property string activeAssetId: ""
    property string playbackError: ""
    property var queue: []
    property int queueIndex: -1
    property int consecutiveFailures: 0
    property bool useFirstPlayer: true
    property int transitionState: 0 // 0: idle, 1: awaiting first frame, 2: fading
    property url activeUrl: ""
    property string activeName: ""
    property string preparedAssetId: ""
    property string preparedName: ""
    property url preparedUrl: ""
    property int preparedQueueIndex: -1

    function rebuildQueue() {
        cancelCrossfade()
        retryTimer.stop()
        consecutiveFailures = 0
        if (activePlayer.mediaStatus === MediaPlayer.EndOfMedia || activePlayer.error !== MediaPlayer.NoError) {
            releaseActive()
        }
        const available = AerialBackend.assetIds()
        const selected = configuredIds.length > 0
            ? available.filter(id => configuredIds.indexOf(id) !== -1)
            : available
        queue = selected
        if (configuration.PlaybackOrder === "shuffle") {
            for (let i = queue.length - 1; i > 0; --i) {
                const j = Math.floor(Math.random() * (i + 1))
                const temporary = queue[i]
                queue[i] = queue[j]
                queue[j] = temporary
            }
        }
        queueIndex = -1
        clearPrepared()
    }

    function clearPrepared() {
        AerialBackend.markPlaying(preparedUrl, false)
        preparedAssetId = ""
        preparedName = ""
        preparedUrl = ""
        preparedQueueIndex = -1
        if (transitionState === 0) {
            standbyOutput.frameReady = false
            standbyPlayer.stop()
            standbyPlayer.source = ""
            standbyOutput.opacity = 0
        }
    }

    function activate(assetId, name, localUrl) {
        if (failedAssetIds.indexOf(assetId) !== -1) return
        retryTimer.stop()
        AerialBackend.markPlaying(activeUrl, false)
        activeUrl = localUrl
        activeName = name
        activeAssetId = assetId
        playbackError = ""
        activeOutput.frameReady = false
        activeOutput.opacity = 1
        activeFrameTimeout.restart()
        activePlayer.source = localUrl
        AerialBackend.markPlaying(localUrl, true)
        activePlayer.play()
        prefetchTimer.restart()
    }

    function prefetchNext() {
        if (queue.length === 0 || queueIndex < 0 || transitionState !== 0) return
        if (consecutiveFailures >= Math.min(3, queue.length)) return
        const nextIndex = nextPlayableIndex()
        if (nextIndex < 0) return
        if (queue[nextIndex] !== preparedAssetId) {
            AerialBackend.ensureDownloaded(queue[nextIndex], configuration.QualityPolicy, configuration.CacheLimitMiB)
        }
    }

    function nextPlayableIndex() {
        for (let offset = 1; offset <= queue.length; ++offset) {
            const index = (queueIndex + offset) % queue.length
            if (failedAssetIds.indexOf(queue[index]) === -1) return index
        }
        return -1
    }

    function rejectAsset(assetId) {
        if (assetId !== "" && failedAssetIds.indexOf(assetId) === -1)
            failedAssetIds = failedAssetIds.concat([assetId])
        consecutiveFailures++
        playbackError = "Playback failed. Unusable videos are skipped for this wallpaper session."
    }

    function next() {
        if (transitionState !== 0) return
        if (queue.length === 0) {
            rebuildQueue()
        }
        if (queue.length === 0 || consecutiveFailures >= Math.min(3, queue.length)) {
            return
        }
        const nextIndex = nextPlayableIndex()
        if (nextIndex < 0) return
        if (preparedQueueIndex === nextIndex && preparedAssetId === queue[nextIndex] && preparedUrl.toString() !== "") {
            beginCrossfade()
            return
        }
        queueIndex = nextIndex
        AerialBackend.ensureDownloaded(queue[queueIndex], configuration.QualityPolicy, configuration.CacheLimitMiB)
    }

    function maybeBeginCrossfade(mediaPlayer) {
        if (mediaPlayer !== activePlayer || transitionState !== 0 || preparedUrl.toString() === "" || mediaPlayer.duration <= 0) return
        const leadTime = crossfadeDuration + 300
        if (mediaPlayer.position >= mediaPlayer.duration - leadTime) {
            beginCrossfade()
        }
    }

    function beginCrossfade() {
        if (transitionState !== 0 || preparedUrl.toString() === "") return
        transitionState = 1
        standbyOutput.frameReady = false
        standbyOutput.opacity = 0
        if (standbyPlayer.source.toString() !== preparedUrl.toString()) {
            standbyPlayer.source = preparedUrl
        }
        standbyPlayer.play()
        firstFrameTimeout.restart()
    }

    function handleVideoFrame(mediaPlayer, videoOutput) {
        if (mediaPlayer.source.toString() === "" || videoOutput.videoSink.videoSize.width <= 0) return
        const firstFrame = !videoOutput.frameReady
        videoOutput.frameReady = true
        if (mediaPlayer === activePlayer) {
            if (firstFrame) consecutiveFailures = 0
            activeFrameTimeout.restart()
            return
        }
        if (transitionState !== 1 || mediaPlayer !== standbyPlayer) return
        if (mediaPlayer.source.toString() !== preparedUrl.toString() || videoOutput.videoSink.videoSize.width <= 0) return
        firstFrameTimeout.stop()
        transitionState = 2
        crossfadeAnimation.start()
    }

    function finishCrossfade() {
        retryTimer.stop()
        const previousPlayer = activePlayer
        const previousOutput = activeOutput
        const previousUrl = activeUrl

        previousOutput.opacity = 0
        standbyOutput.opacity = 1
        activeUrl = preparedUrl
        activeName = preparedName
        activeAssetId = preparedAssetId
        queueIndex = preparedQueueIndex
        useFirstPlayer = !useFirstPlayer
        transitionState = 0
        consecutiveFailures = 0
        playbackError = ""
        activeFrameTimeout.restart()
        preparedAssetId = ""
        preparedName = ""
        preparedUrl = ""
        preparedQueueIndex = -1

        previousPlayer.stop()
        previousPlayer.source = ""
        previousOutput.frameReady = false
        AerialBackend.markPlaying(previousUrl, false)
        prefetchTimer.restart()
    }

    function cancelCrossfade() {
        if (transitionState !== 0) {
            crossfadeAnimation.stop()
            firstFrameTimeout.stop()
            standbyPlayer.stop()
            standbyPlayer.source = ""
            standbyOutput.opacity = 0
            activeOutput.opacity = 1
            transitionState = 0
        }
        clearPrepared()
    }

    function releaseActive() {
        activeFrameTimeout.stop()
        prefetchTimer.stop()
        activeOutput.frameReady = false
        const previousUrl = activeUrl
        activePlayer.stop()
        activePlayer.source = ""
        activeUrl = ""
        activeAssetId = ""
        AerialBackend.markPlaying(previousUrl, false)
    }

    function failCrossfade() {
        const failedIndex = preparedQueueIndex
        rejectAsset(preparedAssetId)
        cancelCrossfade()
        if (failedIndex >= 0) {
            queueIndex = failedIndex
        }
        const activeHealthy = activePlayer.source.toString() !== ""
            && activePlayer.error === MediaPlayer.NoError
            && activePlayer.mediaStatus !== MediaPlayer.EndOfMedia
        if (activeHealthy) {
            prefetchTimer.restart()
        } else {
            releaseActive()
            retryTimer.restart()
        }
    }

    function handleMediaStatus(mediaPlayer) {
        if (mediaPlayer.mediaStatus !== MediaPlayer.EndOfMedia) return
        if (mediaPlayer === standbyPlayer && transitionState !== 0) {
            failCrossfade()
            return
        }
        if (mediaPlayer !== activePlayer) return
        activeFrameTimeout.stop()
        if (transitionState === 0) {
            if (preparedUrl.toString() !== "") {
                beginCrossfade()
            } else {
                releaseActive()
                next()
            }
        }
    }

    function handlePlaybackError(mediaPlayer, errorString) {
        if (mediaPlayer.source.toString() === "") return
        console.warn("Aerial playback error:", errorString)
        if (mediaPlayer === standbyPlayer && transitionState !== 0) {
            failCrossfade()
            return
        }
        if (mediaPlayer !== activePlayer) return
        rejectAsset(activeAssetId)
        if (transitionState === 2) {
            crossfadeAnimation.stop()
            finishCrossfade()
            return
        }
        if (transitionState === 1) {
            cancelCrossfade()
        }
        releaseActive()
        retryTimer.restart()
    }

    Rectangle {
        anchors.fill: parent
        color: "#101418"

        VideoOutput {
            id: outputOne
            property bool frameReady: false
            anchors.fill: parent
            z: root.useFirstPlayer ? 1 : 2
            opacity: 1
            fillMode: configuration.FillMode === 0 ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop
            visible: frameReady && playerOne.source.toString() !== "" && playerOne.error === MediaPlayer.NoError
        }

        VideoOutput {
            id: outputTwo
            property bool frameReady: false
            anchors.fill: parent
            z: root.useFirstPlayer ? 2 : 1
            opacity: 0
            fillMode: configuration.FillMode === 0 ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop
            visible: frameReady && playerTwo.source.toString() !== "" && playerTwo.error === MediaPlayer.NoError
        }

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.7, 520)
            spacing: 10
            visible: !root.hasVisibleVideo

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: "white"
                font.pixelSize: 22
                text: AerialBackend.state === "downloading" ? "Preparing an Aerial video"
                    : AerialBackend.state === "processing" ? "Reading the Aerial catalog" : "Aerial"
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: "#b8c2cc"
                text: root.playbackError || AerialBackend.errorMessage || "Downloading a video may take a few minutes. Only completed files are played."
            }
            Rectangle {
                width: parent.width
                height: 3
                color: "#33404a"
                visible: AerialBackend.state === "downloading"
                Rectangle {
                    height: parent.height
                    width: parent.width * AerialBackend.downloadProgress
                    color: "#75bfff"
                }
            }
        }
    }

    AudioOutput {
        id: silentAudioOne
        muted: true
        volume: 0
    }

    AudioOutput {
        id: silentAudioTwo
        muted: true
        volume: 0
    }

    MediaPlayer {
        id: playerOne
        audioOutput: silentAudioOne
        videoOutput: outputOne
        onPositionChanged: root.maybeBeginCrossfade(playerOne)
        onMediaStatusChanged: root.handleMediaStatus(playerOne)
        onErrorOccurred: function(error, errorString) { root.handlePlaybackError(playerOne, errorString) }
    }

    MediaPlayer {
        id: playerTwo
        audioOutput: silentAudioTwo
        videoOutput: outputTwo
        onPositionChanged: root.maybeBeginCrossfade(playerTwo)
        onMediaStatusChanged: root.handleMediaStatus(playerTwo)
        onErrorOccurred: function(error, errorString) { root.handlePlaybackError(playerTwo, errorString) }
    }

    Connections {
        target: outputOne.videoSink
        function onVideoFrameChanged() { root.handleVideoFrame(playerOne, outputOne) }
    }

    Connections {
        target: outputTwo.videoSink
        function onVideoFrameChanged() { root.handleVideoFrame(playerTwo, outputTwo) }
    }

    NumberAnimation {
        id: crossfadeAnimation
        target: root.standbyOutput
        property: "opacity"
        to: 1
        duration: root.crossfadeDuration
        easing.type: Easing.Linear
        onFinished: root.finishCrossfade()
    }

    Timer {
        id: activeFrameTimeout
        interval: 15000
        repeat: false
        onTriggered: root.handlePlaybackError(root.activePlayer, "Video did not deliver frames for 15 seconds")
    }

    Timer {
        id: firstFrameTimeout
        interval: 3000
        repeat: false
        onTriggered: {
            console.warn("Aerial playback error: next video did not produce a frame")
            root.failCrossfade()
        }
    }

    Timer {
        id: retryTimer
        interval: 1500
        repeat: false
        onTriggered: root.next()
    }

    Timer {
        id: prefetchTimer
        interval: 1000
        repeat: false
        onTriggered: root.prefetchNext()
    }

    Connections {
        target: AerialBackend

        function onCatalogChanged() {
            root.rebuildQueue()
            if (root.activePlayer.source.toString() === "") {
                root.next()
            }
        }
        function onPlayableReady(assetId, name, localUrl) {
            if (root.queue.length === 0) return
            if (root.failedAssetIds.indexOf(assetId) !== -1) return
            if (root.transitionState === 0 && root.queue[root.queueIndex] === assetId && root.activePlayer.source.toString() === "") {
                root.activate(assetId, name, localUrl)
                return
            }
            const nextIndex = root.nextPlayableIndex()
            if (root.queue[nextIndex] === assetId && root.transitionState === 0) {
                root.clearPrepared()
                root.preparedAssetId = assetId
                root.preparedName = name
                root.preparedUrl = localUrl
                root.preparedQueueIndex = nextIndex
                AerialBackend.markPlaying(localUrl, true)
            }
        }
        function onOperationFailed(assetId, message) {
            if (root.queue.length > 0 && (root.queue[root.queueIndex] === assetId || root.queue[root.nextPlayableIndex()] === assetId)) {
                console.warn("Aerial download error:", message)
                root.rejectAsset(assetId)
                if (root.activePlayer.source.toString() === "") retryTimer.restart()
                else prefetchTimer.restart()
            }
        }
    }

    Component.onCompleted: {
        rebuildQueue()
        if (queue.length > 0) {
            next()
        } else {
            AerialBackend.refreshCatalog()
        }
    }
    Component.onDestruction: {
        AerialBackend.markPlaying(activeUrl, false)
        AerialBackend.markPlaying(preparedUrl, false)
    }
}
