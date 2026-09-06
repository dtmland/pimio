import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property var session
    property var libraryModel
    property string selectedLocation: ""

    objectName: "libraryManagerDialog"
    title: qsTr("Library Manager")
    modal: true
    closePolicy: Popup.CloseOnEscape

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.session && root.session.hasOpenLibrary
                  ? qsTr("Open: %1\nIdentity: %2\nLocation: %3")
                        .arg(root.session.currentLibraryName)
                        .arg(root.session.currentLibraryId)
                        .arg(root.session.currentLibraryLocation)
                  : qsTr("No Library is open.")
        }

        ListView {
            id: knownLibraries
            objectName: "knownLibraries"
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            clip: true
            model: root.libraryModel

            delegate: ItemDelegate {
                required property string name
                required property string libraryId
                required property string location
                required property bool available

                width: knownLibraries.width
                text: qsTr("%1 — %2\n%3%4").arg(name).arg(libraryId).arg(location)
                                             .arg(available ? "" : qsTr(" (missing)"))
                onClicked: {
                    root.selectedLocation = location
                    locationField.text = location
                }
            }
        }

        TextField {
            id: nameField
            objectName: "libraryNameField"
            Layout.fillWidth: true
            placeholderText: qsTr("Library name")
        }

        TextField {
            id: locationField
            objectName: "libraryLocationField"
            Layout.fillWidth: true
            placeholderText: qsTr("Library folder")
            onTextChanged: root.selectedLocation = text
        }

        RowLayout {
            Button {
                objectName: "createLibraryButton"
                text: qsTr("Create")
                enabled: root.session && nameField.text.trim().length > 0
                         && locationField.text.trim().length > 0
                onClicked: root.session.createLibrary(nameField.text, locationField.text)
            }
            Button {
                objectName: "openLibraryButton"
                text: qsTr("Open")
                enabled: root.session && locationField.text.trim().length > 0
                onClicked: root.session.openLibrary(locationField.text)
            }
            Button {
                objectName: "closeLibraryButton"
                text: qsTr("Close Library")
                enabled: root.session && root.session.hasOpenLibrary
                onClicked: root.session.closeLibrary()
            }
            Button {
                objectName: "renameLibraryButton"
                text: qsTr("Rename")
                enabled: root.session && root.session.hasOpenLibrary
                         && nameField.text.trim().length > 0
                onClicked: root.session.renameLibrary(nameField.text)
            }
        }

        Label {
            text: qsTr("Move the complete Library")
            font.bold: true
        }
        RowLayout {
            TextField {
                id: moveField
                objectName: "libraryMoveField"
                Layout.fillWidth: true
                placeholderText: qsTr("New Library folder")
            }
            Button {
                text: qsTr("Move")
                enabled: root.session && root.session.hasOpenLibrary
                         && moveField.text.trim().length > 0
                onClicked: root.session.moveLibrary(moveField.text)
            }
        }

        Label {
            text: qsTr("Backup and restore")
            font.bold: true
        }
        TextField {
            id: archiveField
            objectName: "libraryArchiveField"
            Layout.fillWidth: true
            placeholderText: qsTr("Backup archive (.pimio-backup)")
        }
        RowLayout {
            Button {
                text: qsTr("Back Up")
                enabled: root.session && root.session.hasOpenLibrary
                         && archiveField.text.trim().length > 0
                onClicked: root.session.backupLibrary(archiveField.text)
            }
            Button {
                text: qsTr("Restore")
                enabled: root.session && archiveField.text.trim().length > 0
                         && locationField.text.trim().length > 0
                onClicked: root.session.restoreLibrary(archiveField.text, locationField.text)
            }
        }

        Label {
            objectName: "libraryLifecycleStatus"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: text.length > 0
            text: root.session ? root.session.lifecycleStatus : ""
        }
    }

    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Close
        onRejected: root.close()
    }
}
