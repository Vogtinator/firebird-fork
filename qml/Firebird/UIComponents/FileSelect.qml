import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Dialogs 1.0
import QtQuick.Layouts 1.0
import Firebird.Emu 1.0

RowLayout {
    property string filePath: ""
    property bool selectExisting: true
    property bool showCreateButton: false
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

    // Button for either custom creation functionality (onCreate) or
    // if the open file dialog doesn't allow creation, to open a file creation dialog.
    ToolButton {
        text: qsTr("New")
        visible: showCreateButton || (!selectExisting && !Emu.saveDialogSupported())
        iconSource: "qrc:/icons/resources/icons/document-new.png"
        onClicked: {
            if(showCreateButton)
                parent.create()
            else {
                createDialogLoader.active = true;
                createDialogLoader.item.visible = true;
            }
        }

        Loader {
            id: createDialogLoader
            active: false
            sourceComponent: FileDialog {
                folder: filePath ? Emu.dir(filePath) : Global.lastFileDialogDir
                selectExisting: false
                onAccepted: {
                    filePath = Emu.toLocalFile(fileUrl);
                    Global.lastFileDialogDir = Emu.dir(filePath);
                }
            }
        }
    }

    ToolButton {
        text: qsTr("Select")
        iconSource: "qrc:/icons/resources/icons/document-edit.png"
        onClicked: {
            dialogLoader.active = true;
            dialogLoader.item.visible = true;
        }
    }
}
