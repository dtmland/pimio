import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property var settings
    property bool scanning: false
    property int indexedCount: 0
    property int tileSize: 176
    property int minimumTileSize: 96
    property int maximumTileSize: 256
    property var sortOptions: []
    property int sortKey: 0
    property bool sortDescending: false
    property var settingsDialog: null

    height: 48
    color: "#2b2b2b"

    function indexOfSortKey(key) {
        const keys = root.sortOptions
        for (let i = 0; i < keys.length; ++i) {
            if (keys[i] === key)
                return i
        }
        return 0
    }

    Row {
        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
        spacing: 8

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: "#ffffff"
            font.pixelSize: 16
            text: qsTr("pimio %1").arg(Qt.application.version)
            objectName: "placeholderLabel"
        }

        BusyIndicator {
            objectName: "scanBusyIndicator"
            anchors.verticalCenter: parent.verticalCenter
            height: 24
            width: 24
            running: root.scanning
            visible: root.scanning
        }

        Text {
            objectName: "scanStatusLabel"
            anchors.verticalCenter: parent.verticalCenter
            color: "#bbbbbb"
            font.pixelSize: 12
            visible: root.scanning
            text: qsTr("Scanning… %1 found").arg(root.indexedCount)
        }
    }

    Row {
        anchors {
            right: parent.right
            rightMargin: 12
            verticalCenter: parent.verticalCenter
        }
        spacing: 8

        ComboBox {
            objectName: "sortComboBox"
            width: 150
            anchors.verticalCenter: parent.verticalCenter
            textRole: "label"
            valueRole: "key"
            model: root.sortOptions
            currentIndex: root.indexOfSortKey(root.sortKey)
            onActivated: if (root.settings)
                             root.settings.sortKey = valueAt(currentIndex)
        }

        ToolButton {
            objectName: "sortDirectionButton"
            anchors.verticalCenter: parent.verticalCenter
            text: root.sortDescending ? "↓" : "↑"
            Accessible.name: root.sortDescending
                             ? qsTr("Sort descending") : qsTr("Sort ascending")
            onClicked: if (root.settings)
                            root.settings.sortDescending = !root.settings.sortDescending
        }

        Slider {
            objectName: "tileSizeSlider"
            width: 120
            anchors.verticalCenter: parent.verticalCenter
            from: root.minimumTileSize
            to: root.maximumTileSize
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: root.tileSize
            onMoved: if (root.settings)
                         root.settings.tileSize = Math.round(value)
        }

        ToolButton {
            objectName: "settingsButton"
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Settings")
            onClicked: if (root.settingsDialog)
                            root.settingsDialog.open()
        }
    }
}
