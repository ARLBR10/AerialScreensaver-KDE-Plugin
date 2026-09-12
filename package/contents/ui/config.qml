import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.aerial.private 1.0

ColumnLayout {
    id: root

    property var cfg_SelectedAssetIds: []
    property alias cfg_QualityPolicy: quality.currentValue
    property alias cfg_PlaybackOrder: order.currentValue
    property int cfg_FillMode: 1
    property alias cfg_CacheLimitMiB: cacheLimit.value
    property string cfg_LastAssetId: ""

    spacing: Kirigami.Units.smallSpacing

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: true
        type: Kirigami.MessageType.Information
        text: "Unofficial plugin. Videos are downloaded directly from Apple and played muted from the local cache."
    }

    RowLayout {
        Layout.fillWidth: true
        Label { text: "Quality" }
        ComboBox {
            id: quality
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [
                { text: "1080p H.264 SDR (recommended)", value: "compatibility" },
                { text: "1080p SDR", value: "1080-sdr" },
                { text: "4K SDR", value: "4k-sdr" },
                { text: "HDR (experimental)", value: "hdr" }
            ]
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Label { text: "Playback" }
        ComboBox {
            id: order
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [{ text: "Shuffle", value: "shuffle" }, { text: "Sequential", value: "sequential" }]
        }
        Label { text: "Fit" }
        ComboBox {
            model: ["Fit", "Fill"]
            currentIndex: root.cfg_FillMode
            onActivated: root.cfg_FillMode = currentIndex
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Label { text: "Cache limit" }
        SpinBox {
            id: cacheLimit
            from: 256
            to: 32768
            stepSize: 256
            editable: true
        }
        Label { text: "MiB" }
        Item { Layout.fillWidth: true }
        Button { text: "Clear cache"; onClicked: AerialBackend.clearCache() }
        Button { text: "Refresh catalog"; enabled: AerialBackend.state !== "refreshing"; onClicked: AerialBackend.refreshCatalog() }
    }

    Label {
        Layout.fillWidth: true
        text: AerialBackend.errorMessage
        color: Kirigami.Theme.negativeTextColor
        wrapMode: Text.WordWrap
        visible: text.length > 0
    }

    Label {
        text: "Aerials (leave all unchecked to rotate through the full catalog)"
        font.bold: true
    }

    GridView {
        id: catalogGrid
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 240
        clip: true
        model: AerialBackend.catalogModel
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 220)))
        cellHeight: 178
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        delegate: Item {
            required property string assetId
            required property string name
            required property url previewUrl
            required property bool previewAvailable
            required property string sourceLabel
            readonly property bool selected: root.cfg_SelectedAssetIds.indexOf(assetId) !== -1
            property bool previewTimedOut: false
            width: catalogGrid.cellWidth
            height: catalogGrid.cellHeight

            Rectangle {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                radius: Kirigami.Units.smallSpacing
                color: selected ? Kirigami.Theme.highlightColor : Kirigami.Theme.alternateBackgroundColor
                border.width: selected ? 3 : 1
                border.color: selected ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor

                Image {
                    id: preview
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 112
                    source: previewUrl
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 360
                    sourceSize.height: 202
                    clip: true
                }

                BusyIndicator {
                    anchors.centerIn: preview
                    running: previewAvailable && previewUrl.toString() === "" && !previewTimedOut
                    visible: running
                }

                Label {
                    anchors.centerIn: preview
                    text: "Preview unavailable"
                    color: Kirigami.Theme.disabledTextColor
                    visible: !previewAvailable || preview.status === Image.Error || previewTimedOut
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    width: 26
                    height: 26
                    radius: 13
                    color: selected ? Kirigami.Theme.highlightColor : "#99000000"

                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        source: selected ? "checkmark" : "list-add"
                        color: "white"
                    }
                }

                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: preview.bottom
                    anchors.margins: 8
                    text: name + (sourceLabel.length > 0 ? " (" + sourceLabel + ")" : "")
                    color: selected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                TapHandler {
                    cursorShape: Qt.PointingHandCursor
                    onTapped: {
                        const values = root.cfg_SelectedAssetIds.slice()
                        const index = values.indexOf(assetId)
                        if (index === -1) values.push(assetId)
                        else values.splice(index, 1)
                        root.cfg_SelectedAssetIds = values
                    }
                }
            }

            Timer {
                interval: 16000
                running: previewAvailable && previewUrl.toString() === ""
                onTriggered: previewTimedOut = true
            }

            function loadPreview() {
                previewTimedOut = false
                AerialBackend.requestPreview(assetId)
            }

            Component.onCompleted: loadPreview()
            onAssetIdChanged: loadPreview()
        }
    }

    Component.onCompleted: {
        if (AerialBackend.catalogModel.rowCount() === 0) {
            AerialBackend.refreshCatalog()
        }
    }
}
