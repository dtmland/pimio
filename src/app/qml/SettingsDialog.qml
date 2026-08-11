import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Everything the user can change about how browsing behaves, in one place.
//
// The dialog is only one way to reach these settings: each control binds to
// the appSettings object, which is also driven from C++ (and, later, from a
// command line), so nothing here owns the value it shows.
Dialog {
    id: dialog

    property var settings: null

    objectName: "settingsDialog"
    title: qsTr("View and input settings")
    modal: true
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        enabled: dialog.settings !== null

        GroupBox {
            title: qsTr("Stored — remembered for next time")
            Layout.fillWidth: true

            GridLayout {
                anchors.fill: parent
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Label { text: qsTr("Sort by") }

                ComboBox {
                    id: sortCombo
                    objectName: "settingsSortCombo"
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "key"
                    model: dialog.sortModel()
                    currentIndex: dialog.settings
                                  ? dialog.indexOfSortKey(dialog.settings.sortKey) : 0
                    onActivated: if (dialog.settings)
                                     dialog.settings.sortKey = valueAt(currentIndex)
                }

                Label { text: qsTr("Order") }

                Switch {
                    objectName: "settingsSortDescendingSwitch"
                    text: checked ? qsTr("Descending") : qsTr("Ascending")
                    checked: dialog.settings ? dialog.settings.sortDescending : false
                    onToggled: if (dialog.settings)
                                   dialog.settings.sortDescending = checked
                }

                Label { text: qsTr("Tile size") }

                RowLayout {
                    Layout.fillWidth: true
                    Slider {
                        objectName: "settingsTileSizeSlider"
                        Layout.fillWidth: true
                        from: dialog.settings ? dialog.settings.minimumTileSize : 96
                        to: dialog.settings ? dialog.settings.maximumTileSize : 256
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        value: dialog.settings ? dialog.settings.tileSize : 176
                        // Live, so the grid resizes while the handle moves,
                        // the way Picasa's thumbnail slider did.
                        onMoved: if (dialog.settings)
                                     dialog.settings.tileSize = Math.round(value)
                    }
                    Label {
                        text: dialog.settings ? qsTr("%1 px").arg(dialog.settings.tileSize) : ""
                        Layout.preferredWidth: 60
                    }
                }

                Label { text: qsTr("Scroll speed") }

                RowLayout {
                    Layout.fillWidth: true
                    Slider {
                        objectName: "settingsScrollSpeedSlider"
                        Layout.fillWidth: true
                        from: dialog.settings ? dialog.settings.minimumScrollSpeed : 0.25
                        to: dialog.settings ? dialog.settings.maximumScrollSpeed : 8
                        stepSize: 0.25
                        snapMode: Slider.SnapAlways
                        value: dialog.settings ? dialog.settings.scrollSpeed : 2
                        onMoved: if (dialog.settings)
                                     dialog.settings.scrollSpeed = value
                    }
                    Label {
                        text: dialog.settings
                              ? qsTr("×%1").arg(dialog.settings.scrollSpeed.toFixed(2)) : ""
                        Layout.preferredWidth: 60
                    }
                }

                Label { text: qsTr("Scroll acceleration") }

                CheckBox {
                    objectName: "settingsScrollAccelerationCheck"
                    text: qsTr("Keep scrolling to go faster")
                    checked: dialog.settings ? dialog.settings.scrollAcceleration : true
                    onToggled: if (dialog.settings)
                                   dialog.settings.scrollAcceleration = checked
                }

                Label { text: qsTr("Key repeat acceleration") }

                CheckBox {
                    objectName: "settingsKeyAccelerationCheck"
                    text: qsTr("Hold a key to jump further")
                    checked: dialog.settings ? dialog.settings.keyRepeatAcceleration : true
                    onToggled: if (dialog.settings)
                                   dialog.settings.keyRepeatAcceleration = checked
                }
            }
        }

        GroupBox {
            title: qsTr("This session only — back to normal next launch")
            Layout.fillWidth: true

            CheckBox {
                objectName: "settingsTileDiagnosticsCheck"
                text: qsTr("Show tile diagnostics")
                checked: dialog.settings ? dialog.settings.showTileDiagnostics : false
                onToggled: if (dialog.settings)
                               dialog.settings.showTileDiagnostics = checked
            }
        }

        Button {
            objectName: "settingsResetButton"
            text: qsTr("Reset to defaults")
            onClicked: if (dialog.settings) dialog.settings.resetToDefaults()
        }
    }

    function sortModel() {
        if (!settings)
            return []
        const keys = settings.sortKeyValues()
        let entries = []
        for (let i = 0; i < keys.length; ++i)
            entries.push({ key: keys[i], label: settings.sortKeyLabelFor(keys[i]) })
        return entries
    }

    function indexOfSortKey(key) {
        if (!settings)
            return 0
        const keys = settings.sortKeyValues()
        for (let i = 0; i < keys.length; ++i) {
            if (keys[i] === key)
                return i
        }
        return 0
    }
}
