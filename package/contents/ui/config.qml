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

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        ListView {
            model: AerialBackend.catalogModel
            reuseItems: true
            delegate: CheckDelegate {
                required property string assetId
                required property string name
                width: ListView.view.width
                text: name
                checked: root.cfg_SelectedAssetIds.indexOf(assetId) !== -1
                onToggled: {
                    const values = root.cfg_SelectedAssetIds.slice()
                    const index = values.indexOf(assetId)
                    if (checked && index === -1) values.push(assetId)
                    if (!checked && index !== -1) values.splice(index, 1)
                    root.cfg_SelectedAssetIds = values
                }
            }
        }
    }

    Component.onCompleted: {
        if (AerialBackend.catalogModel.rowCount() === 0) {
            AerialBackend.refreshCatalog()
        }
    }
}
