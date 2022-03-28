import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Dialogs 1.0
import QtQuick.Layouts 1.0
import Firebird.Emu 1.0

RowLayout {
    id: root
    property string filePath: ""
    property bool selectExisting: true
    signal create()

    Loader {
        id: dialogLoader
        active: false
        sourceComponent: FileDialog {
            folder: filePath ? Emu.dir(filePath) : Global.lastFileDialogDir
            // If save dialogs are not supported, force an open dialog
            selectExisting: root.selectExisting || !Emu.saveDialogSupported()
            onAccepted: {
                filePath = Emu.toLocalFile(fileUrl);
                Global.lastFileDialogDir = Emu.dir(filePath);
            }
        }
    }

    SystemPalette {
        id: paletteActive
    }

    FBLabel {
        id: filenameLabel
        elide: "ElideRight"

        Layout.fillWidth: true

        font.italic: filePath === ""
        text: filePath === "" ? qsTr("(none)") : Emu.basename(filePath)
        color: (!selectExisting || filePath === "" || Emu.fileExists(filePath)) ? paletteActive.text : "red"
    }

    ToolButton {
        iconSource: "qrc:/icons/resources/icons/edit-entry.svg"
        onClicked: menu.popup()
        Menu {
            id: menu
            iconSource: "qrc:/icons/resources/icons/edit-entry.svg"

            MenuItem {
                text: "Select existing"
                onTriggered: root.create()
            }

            MenuItem {
                text: "Create new"
            }
       }
    }
}
