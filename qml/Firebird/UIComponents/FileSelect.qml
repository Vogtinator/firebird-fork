import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Dialogs 1.0
import QtQuick.Layouts 1.0
import Firebird.Emu 1.0

RowLayout {
    id: root
    property string filePath: ""
    property bool selectExisting: true
    property bool showCreateButton: true
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

    function openFileDialog() {
        dialogLoader.active = true;
        dialogLoader.item.visible = true;
    }

    Menu {
        id: menu
        iconSource: "qrc:/icons/resources/icons/edit-entry.svg"

        MenuItem {
            text: qsTr("Select existing")
            onTriggered: openFileDialog()
        }

        MenuItem {
            text: qsTr("Create new")
            onTriggered: root.create()
        }
   }

    ToolButton {
        visible: showEditButton
        iconSource: "qrc:/icons/resources/icons/edit-entry.svg"
        onClicked: {
            if(showCreateButton)
                menu.popup();
            else
                openFileDialog();
        }
    }
}
