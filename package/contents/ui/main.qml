import QtQuick
import QtMultimedia
import org.kde.plasma.plasmoid
import org.kde.plasma.aerial.private 1.0

WallpaperItem {
    id: root

    readonly property var configuredIds: configuration.SelectedAssetIds || []
    property var queue: []
    property int queueIndex: -1
    property int consecutiveFailures: 0
    property url activeUrl: ""
    property string activeName: ""
    property string preparedAssetId: ""
    property string preparedName: ""
    property url preparedUrl: ""

    function rebuildQueue() {
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
        preparedAssetId = ""
        preparedName = ""
        preparedUrl = ""
    }

    function activate(assetId, name, localUrl) {
        AerialBackend.markPlaying(activeUrl, false)
        activeUrl = localUrl
        activeName = name
        consecutiveFailures = 0
        player.source = localUrl
        AerialBackend.markPlaying(localUrl, true)
        player.play()
        prefetchTimer.restart()
    }

    function prefetchNext() {
        if (queue.length < 2 || queueIndex < 0) return
        const nextIndex = (queueIndex + 1) % queue.length
        if (queue[nextIndex] !== preparedAssetId) {
            AerialBackend.ensureDownloaded(queue[nextIndex], configuration.QualityPolicy, configuration.CacheLimitMiB)
        }
    }

    function next() {
        if (queue.length === 0) {
            rebuildQueue()
        }
        if (queue.length === 0 || consecutiveFailures >= Math.min(3, queue.length)) {
            return
        }
        queueIndex = (queueIndex + 1) % queue.length
        if (preparedAssetId === queue[queueIndex] && preparedUrl.toString() !== "") {
            const assetId = preparedAssetId
            const name = preparedName
            const localUrl = preparedUrl
            preparedAssetId = ""
            preparedName = ""
            preparedUrl = ""
            activate(assetId, name, localUrl)
            return
        }
        AerialBackend.ensureDownloaded(queue[queueIndex], configuration.QualityPolicy, configuration.CacheLimitMiB)
    }

    Rectangle {
        anchors.fill: parent
        color: "#101418"

        VideoOutput {
            id: output
            anchors.fill: parent
            fillMode: configuration.FillMode === 0 ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop
            visible: root.activeUrl.toString() !== "" && player.error === MediaPlayer.NoError
        }

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.7, 520)
            spacing: 10
            visible: !output.visible

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
                text: AerialBackend.errorMessage || "Downloading a video may take a few minutes. Only completed files are played."
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
        id: silentAudio
        muted: true
        volume: 0
    }

    MediaPlayer {
        id: player
        audioOutput: silentAudio
        videoOutput: output

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia) {
                AerialBackend.markPlaying(root.activeUrl, false)
                root.next()
            }
        }
        onErrorOccurred: function(error, errorString) {
            console.warn("Aerial playback error:", errorString)
            AerialBackend.markPlaying(root.activeUrl, false)
            root.activeUrl = ""
            root.consecutiveFailures++
            source = ""
            retryTimer.restart()
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
            if (player.source.toString() === "") {
                root.next()
            }
        }
        function onPlayableReady(assetId, name, localUrl) {
            if (root.queue.length === 0) {
                return
            }
            if (root.queue[root.queueIndex] === assetId) {
                root.activate(assetId, name, localUrl)
                return
            }
            const nextIndex = (root.queueIndex + 1) % root.queue.length
            if (root.queue[nextIndex] === assetId) {
                root.preparedAssetId = assetId
                root.preparedName = name
                root.preparedUrl = localUrl
            }
        }
        function onOperationFailed(assetId, message) {
            if (root.queue.length > 0 && root.queue[root.queueIndex] === assetId) {
                console.warn("Aerial download error:", message)
                root.consecutiveFailures++
                retryTimer.restart()
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
    Component.onDestruction: AerialBackend.markPlaying(activeUrl, false)
}
