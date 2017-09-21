import QtQuick 2.4
import Ubuntu.Components 1.3
import Ubuntu.Components.Popups 1.3

import "templates"

ConfirmDialogWithInput {
    id: rootItem

    property var folderModel

    // IMPROVE: this does not seem good: the backend expects row and new name.
    // But what if new files are added/deleted in the background while user is
    // entering the new name? The indices change and wrong file is renamed.
    // Perhaps the backend should take as parameters the "old name" and "new name"?
    // This is not currently a problem since the backend does not poll changes in
    // the filesystem, but may be a problem in the future.
    property int modelRow

    title: i18n.tr("Rename")
    text: i18n.tr("Enter a new name")

    onAccepted: {
        console.log("Rename accepted", inputText)
        if (inputText !== '') {
            console.log("Rename commensed, modelRow/inputText", modelRow, inputText.trim())
            if (folderModel.rename(modelRow, inputText.trim()) === false) {
                var props = {
                    title: i18n.tr("Could not rename"),
                    text: i18n.tr("Insufficient permissions or name already exists?")
                }
                PopupUtils.open(Qt.resolvedUrl("NotifyDialog.qml"), mainView, props)

            }
        } else {
            console.log("Empty new name given, ignored")
        }
    }
}
