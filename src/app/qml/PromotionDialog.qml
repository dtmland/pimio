import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property var session
    readonly property bool canPromote: session ? session.canPromote : false

    objectName: "promotionDialog"
    title: qsTr("Promote Library")
    modal: true
    closePolicy: Popup.CloseOnEscape

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Publish this local library to a LORE server. The server must permit "
                       + "access to the repository URL.")
        }

        TextField {
            id: serverUrl
            objectName: "promotionServerUrl"
            Layout.fillWidth: true
            placeholderText: qsTr("lore://server.example/library")
            inputMethodHints: Qt.ImhUrlCharactersOnly
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#b36b00"
            text: qsTr("Alpha limitation: if the first push is interrupted, LORE 0.9 may "
                       + "require server-side repair before promotion can be retried.")
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Promotion transfers the Library identity, canonical records, history, "
                       + "and current managed originals. Older historical file payloads are "
                       + "loaded lazily by LORE when requested.")
        }

        Label {
            objectName: "promotionStatusLabel"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: text.length > 0
            text: root.session ? root.session.promotionStatus : ""
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "promoteLibraryButton"
            text: qsTr("Promote")
            enabled: root.canPromote && serverUrl.text.trim().length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: root.session.promoteToServer(serverUrl.text)
        }
        Button {
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        onRejected: root.close()
    }
}
