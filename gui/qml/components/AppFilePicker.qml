import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: root

    enum Mode {
        OpenFile,
        SaveFile,
        OpenFolder
    }

    property int mode: AppFilePicker.OpenFile
    property string pending: ""
    property string persistKey: ""
    property url defaultFolder: ""
    // Explicit per-request location takes precedence over navigation history.
    property url initialFolder: ""
    readonly property string currentFolder: dirModel.rootPath
    property bool multiple: false
    property var selectedFiles: []
    property var selectedPaths: []
    // Anchor row for Shift-click range selection in multiple mode.
    property int selectionAnchor: -1
    property string skipText: ""
    property var validateSelection: null
    property string selectionError: ""
    signal skipped()
    property var nameFilters: []
    property string defaultSuffix: ""

    readonly property bool folderMode: mode === AppFilePicker.OpenFolder
    readonly property bool saveMode: mode === AppFilePicker.SaveFile

    property url selectedFile: ""
    property url selectedFolder: ""

    property string selectedPath: ""
    property bool selectedIsDir: false
    property bool editingPath: false
    property bool creatingFolder: false
    property bool searchVisible: false
    property int activeFilter: 0
    property var crumbs: []

    signal accepted()

    // Hint text is a binding rather than an imperative assignment so it
    // retranslates like any other qsTr text; the C++ error text has its own
    // NOTIFY signal. C++ location names come through app.standardPlaces.
    enum Hint { None, Created, EnterName, SlashInName, ModelError, StorageRemoved }
    property int hint: AppFilePicker.Hint.None
    readonly property string errorMessage: {
        if (selectionError !== "") return selectionError;
        switch (hint) {
        case AppFilePicker.Hint.EnterName:   return qsTr("Enter a file name");
        case AppFilePicker.Hint.SlashInName: return qsTr("File name cannot contain \"/\"");
        case AppFilePicker.Hint.ModelError:  return dirModel.errorText;
        case AppFilePicker.Hint.StorageRemoved: return qsTr("The original path is no longer available. Returned to Home.");
        default:                             return "";
        }
    }
    hintText: hint === AppFilePicker.Hint.Created ? qsTr("Directory created") : ""
    property bool errorPending: false
    property var errorFocusItem: null

    readonly property int listHeight: 400
    readonly property int rowHeight: 34
    readonly property int maxCrumbWidth: 220

    width: Math.min(720, Overlay.overlay ? Overlay.overlay.width - 32 : 720)

    onAboutToShow: reset()
    onOpened: openPendingError()
    onAboutToHide: {
        errorPending = false;
        errorDialog.close();
    }

    function showError(kind) {
        hint = kind;
        if (!errorDialog.visible)
            errorFocusItem = root.contentItem.Window.window ? root.contentItem.Window.window.activeFocusItem : null;
        errorPending = true;
        Qt.callLater(openPendingError);
    }

    function openPendingError() {
        if (!root.opened || !errorPending)
            return;
        errorPending = false;
        if (errorMessage !== "")
            errorDialog.open();
    }

    AppDialog {
        id: errorDialog
        objectName: "filePickerErrorDialog"
        parent: Overlay.overlay
        z: root.z + 1
        titleText: qsTr("Error")
        onOpened: errorOk.forceActiveFocus()

        onClosed: Qt.callLater(function () {
            if (root.visible && !errorDialog.visible) {
                if (root.errorFocusItem && root.errorFocusItem.visible && root.errorFocusItem.enabled)
                    root.errorFocusItem.forceActiveFocus();
                else
                    root.focusList();
            }
            root.errorFocusItem = null;
        })

        Text {
            objectName: "filePickerErrorMessage"
            width: parent.width
            text: root.errorMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
        }

        footerContent: PrimaryButton {
            id: errorOk
            objectName: "filePickerErrorOk"
            focusPolicy: Qt.StrongFocus
            text: qsTr("OK")
            onClicked: errorDialog.close()
        }
    }

    function filterLabels() {
        var labels = [];
        if (!nameFilters)
            return labels;
        for (var i = 0; i < nameFilters.length; i++)
            labels.push(String(nameFilters[i]));
        return labels;
    }

    function globsFor(str) {
        if (str === undefined || str === null)
            return [];
        var text = String(str);
        var open = text.indexOf("(");
        var close = text.lastIndexOf(")");
        if (open < 0 || close <= open)
            return [];
        var inside = text.substring(open + 1, close).trim();
        if (inside === "" || inside === "*")
            return [];
        var parts = inside.split(/\s+/);
        var out = [];
        for (var i = 0; i < parts.length; i++) {
            var glob = parts[i].trim();
            if (glob !== "" && glob !== "*")
                out.push(glob);
        }
        return out;
    }

    function applyFilter() {
        var labels = filterLabels();
        var globs = [];
        if (labels.length > 0) {
            var idx = Math.max(0, Math.min(activeFilter, labels.length - 1));
            globs = globsFor(labels[idx]);
        }
        dirModel.nameFilters = globs;
    }

    function buildCrumbs() {
        crumbs = dirModel.breadcrumbs();
    }

    function recoverUnavailableFolder() {
        if (app.directoryAvailable(dirModel.rootPath))
            return false;
        navigateTo(app.homeDir());
        selectedFiles = [];
        selectedFile = "";
        selectedFolder = "";
        fileField.text = "";
        creatingFolder = false;
        newFolderField.text = "";
        showError(AppFilePicker.Hint.StorageRemoved);
        return true;
    }

    function refreshLocations() {
        var previousFolder = dirModel.rootPath;
        app.refreshStandardPlaces();
        var recovered = recoverUnavailableFolder();
        return recovered || dirModel.rootPath !== previousFolder;
    }

    Timer {
        objectName: "storageRefreshTimer"
        interval: 1000
        repeat: true
        running: root.visible
        onTriggered: root.refreshLocations()
    }

    Connections {
        target: app
        function onStorageVolumesChanged() {
            if (root.visible)
                root.recoverUnavailableFolder();
        }
    }

    function navigateTo(path) {
        clearSearch();
        if (!path || path === "")
            return;
        dirModel.rootPath = path;
        selectedPath = "";
        selectedPaths = [];
        selectionAnchor = -1;
        selectionError = "";
        selectedIsDir = false;
        editingPath = false;
        pathField.text = dirModel.rootPath;
        if (!folderMode)
            fileField.text = "";
        buildCrumbs();
        if (persistKey !== "")
            app.rememberPath(persistKey, dirModel.rootPath);
        scrollCrumbsToEnd();
        focusList();
    }

    function openSearch() {
        searchVisible = true;
        Qt.callLater(function () {
            searchField.forceActiveFocus();
            searchField.cursorPosition = searchField.text.length;
        });
    }

    function focusList() {
        Qt.callLater(function () {
            if (root.visible && !errorDialog.visible)
                entryList.forceActiveFocus();
        });
    }

    function clearSearch() {
        if (searchField.text !== "")
            searchField.text = "";
        dirModel.searchText = "";
        searchVisible = false;
        focusList();
    }

    function scrollCrumbsToEnd() {
        Qt.callLater(function () {
            crumbFlick.contentX = Math.max(0, crumbFlick.contentWidth - crumbFlick.width);
        });
    }

    function goUp() {
        var parent = dirModel.parentPath();
        if (parent !== dirModel.rootPath)
            navigateTo(parent);
    }

    function reset() {
        errorPending = false;
        errorDialog.close();
        selectedFiles = [];
        hint = AppFilePicker.Hint.None;
        selectedFile = "";
        selectedFolder = "";
        editingPath = false;
        creatingFolder = false;
        selectedPath = "";
        selectedPaths = [];
        selectionAnchor = -1;
        selectionError = "";
        selectedIsDir = false;
        activeFilter = 0;
        hiddenBox.checked = false;
        filterCombo.currentIndex = 0;
        applyFilter();
        clearSearch();
        var start = "";
        if (initialFolder.toString() !== "")
            start = app.toLocalPath(initialFolder.toString());
        else if (persistKey !== "" && app.lastDir(persistKey) !== "")
            start = app.lastDir(persistKey);
        else if (defaultFolder.toString() !== "")
            start = app.toLocalPath(defaultFolder.toString());
        else
            start = app.homeDir();
        // Enumerate before opening the remembered directory, including when
        // the device was removed while the picker was closed.
        app.refreshStandardPlaces();
        hint = AppFilePicker.Hint.None;
        var unavailable = !app.directoryAvailable(start);
        navigateTo(unavailable ? app.homeDir() : start);
        if (unavailable)
            showError(AppFilePicker.Hint.StorageRemoved);
        if (!folderMode)
            fileField.text = "";
        // setRootPath() early-returns when the target directory is unchanged,
        // so the persistent DirModel would otherwise keep showing the stale
        // list cached from the previous open. Force a rescan every time the
        // picker is shown so newly added/removed files appear immediately.
        dirModel.refresh();
    }

    function selectEntry(name, path, isDir, modifiers) {
        selectionError = "";
        // Resolve the row from the path so the handler does not depend on the
        // delegate's implicit index context property.
        var rowIndex = -1;
        for (var r = 0; r < dirModel.count; ++r)
            if (dirModel.pathAt(r) === path) { rowIndex = r; break; }
        if (multiple && !folderMode && !saveMode && !isDir) {
            // Native-dialog selection: a plain click picks one file, Ctrl
            // toggles a single row and Shift extends from the anchor. A plain
            // click on an already selected row keeps the set so that a
            // following double-click can confirm the whole selection.
            var paths = selectedPaths.slice();
            if ((modifiers & Qt.ShiftModifier) && selectionAnchor >= 0 && rowIndex >= 0) {
                var first = Math.min(selectionAnchor, rowIndex);
                var last = Math.max(selectionAnchor, rowIndex);
                paths = [];
                for (var row = first; row <= last; ++row)
                    if (!dirModel.isDirAt(row)) paths.push(dirModel.pathAt(row));
            } else if (modifiers & Qt.ControlModifier) {
                var index = paths.indexOf(path);
                if (index < 0) paths.push(path); else paths.splice(index, 1);
                selectionAnchor = rowIndex;
            } else if (paths.indexOf(path) < 0) {
                paths = [path];
                selectionAnchor = rowIndex;
            }
            selectedPaths = paths;
            selectedPath = path;
            selectedIsDir = false;
            fileField.text = "";
            return;
        }
        selectionAnchor = rowIndex;
        selectedPath = path;
        selectedIsDir = isDir;
        hint = AppFilePicker.Hint.None;
        if (!folderMode && !isDir)
            fileField.text = name;
        if (!searchVisible)
            focusList();
    }

    function activateEntry(name, path, isDir) {
        if (isDir) {
            navigateTo(path);
        } else {
            selectedPath = path;
            selectedIsDir = false;
            fileField.text = name;
            if (multiple) {
                // A double-click confirms the whole current selection so the
                // rawprogram and patch pickers behave like QFIL's.
                if (selectedPaths.indexOf(path) < 0) selectedPaths = selectedPaths.concat([path]);
                fileField.text = "";
                confirmAccept();
            } else if (!saveMode)
                confirmAccept();
        }
    }

    function createFolder() {
        if (dirModel.makeDirectory(newFolderField.text)) {
            creatingFolder = false;
            hint = AppFilePicker.Hint.Created;
        } else {
            showError(AppFilePicker.Hint.ModelError);
        }
    }

    function confirmAccept() {
        // Close the interval between the last poll and a confirmation click.
        if (refreshLocations())
            return;
        selectionError = "";
        if (multiple && !folderMode && !saveMode && selectedPaths.length > 0 && fileField.text.trim() === "") {
            if (validateSelection) selectionError = validateSelection(selectedPaths);
            if (selectionError !== "") {
                showError(AppFilePicker.Hint.None);
                return;
            }
            selectedFiles = selectedPaths.map(function(path) { return app.pathUrl(path); });
            selectedFile = selectedFiles[0];
            accepted();
            close();
            return;
        }
        if (folderMode) {
            var dir = (selectedIsDir && selectedPath !== "")
                    ? selectedPath : dirModel.rootPath;
            if (persistKey !== "")
                app.rememberPath(persistKey, dir);
            selectedFolder = app.pathUrl(dir);
        } else {
            var name = fileField.text.trim();
            if (name === "") {
                if (!selectedIsDir && selectedPath !== "") {
                    selectedFile = app.pathUrl(selectedPath);
                } else {
                    fileField.forceActiveFocus();
                    showError(AppFilePicker.Hint.EnterName);
                    return;
                }
            } else if (name.indexOf("/") >= 0 || (Qt.platform.os === "windows" && name.indexOf("\\") >= 0)) {
                showError(AppFilePicker.Hint.SlashInName);
                return;
            } else {
                var full = dirModel.filePath(name);
                if (saveMode && defaultSuffix !== "" && name.indexOf(".") < 0) {
                    full += (defaultSuffix.charAt(0) === "." ? defaultSuffix
                                                            : "." + defaultSuffix);
                }
                selectedFile = app.pathUrl(full);
            }
        }
        if (!folderMode) {
            var paths = [app.toLocalPath(selectedFile)];
            if (validateSelection) selectionError = validateSelection(paths);
            if (selectionError !== "") {
                showError(AppFilePicker.Hint.None);
                return;
            }
            selectedFiles = [selectedFile];
        }
        accepted();
        close();
    }

    Column {
        width: parent.width
        spacing: 10

        Row {
            width: parent.width
            spacing: 12

            Column {
                id: placesColumn
                width: 150
                spacing: 6

                Text {
                    text: qsTr("Location")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                }

                ListView {
                    id: placesList
                    width: parent.width
                    height: root.listHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: app.standardPlaces

                    ScrollBar.vertical: ScrollBar {
                        id: placesBar
                        policy: ScrollBar.AsNeeded
                        background: Rectangle { color: "transparent" }
                        contentItem: Rectangle {
                            implicitWidth: 6
                            radius: 3
                            color: Theme.borderStrong
                            opacity: placesBar.size < 1.0 ? (placesBar.active || placesBar.pressed ? 1.0 : 0.5) : 0.0
                        }
                    }

                    delegate: Rectangle {
                        id: placeRow
                        width: placesList.width
                        height: 30
                        radius: Theme.controlRadius
                        color: dirModel.rootPath === modelData.path
                               ? Theme.selected
                               : (placeMouse.containsMouse ? Theme.hover : "transparent")

                        Image {
                            id: placeIcon
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            source: Icons.folderGray
                            sourceSize.width: 16
                            sourceSize.height: 16
                        }

                        Text {
                            anchors.left: placeIcon.right
                            anchors.leftMargin: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontNormal
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            id: placeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.navigateTo(modelData.path)
                        }
                    }
                }
            }

            Column {
                id: mainColumn
                width: parent.width - placesColumn.width - 12
                spacing: 8

                RowLayout {
                    width: parent.width
                    spacing: 6

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.rowHeight

                        Flickable {
                            id: crumbFlick
                            width: parent.width
                            height: parent.height
                            visible: !root.editingPath
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            flickableDirection: Flickable.HorizontalFlick
                            contentWidth: crumbRow.implicitWidth
                            contentHeight: height

                            ScrollBar.horizontal: ScrollBar {
                                id: crumbBar
                                policy: ScrollBar.AsNeeded
                                background: Rectangle { color: "transparent" }
                                contentItem: Rectangle {
                                    implicitHeight: 3
                                    radius: 1.5
                                    color: Theme.borderStrong
                                    opacity: crumbBar.size < 1.0 ? (crumbBar.active || crumbBar.pressed ? 1.0 : 0.5) : 0.0
                                }
                            }

                            WheelHandler {
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                onWheel: function (event) {
                                    var dx = event.angleDelta.x;
                                    var dy = event.angleDelta.y;
                                    if (event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0) {
                                        dx = event.pixelDelta.x;
                                        dy = event.pixelDelta.y;
                                    }
                                    var step = dx !== 0 ? dx : dy;
                                    var maxX = Math.max(0, crumbFlick.contentWidth - crumbFlick.width);
                                    crumbFlick.contentX = Math.max(0, Math.min(maxX, crumbFlick.contentX - step));
                                }
                            }

                            Row {
                                id: crumbRow
                                height: crumbFlick.height
                                spacing: 2

                                Repeater {
                                    model: root.crumbs

                                    delegate: Rectangle {
                                        height: 26
                                        y: (crumbRow.height - height) / 2
                                        width: Math.min(crumbLabel.implicitWidth, root.maxCrumbWidth) + 14
                                        radius: Theme.controlRadius
                                        color: crumbMouse.containsMouse ? Theme.hover : "transparent"

                                        Text {
                                            id: crumbLabel
                                            anchors.centerIn: parent
                                            width: Math.min(implicitWidth, root.maxCrumbWidth)
                                            text: modelData.name
                                            color: Theme.textPrimary
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontNormal
                                            elide: Text.ElideRight
                                        }

                                        MouseArea {
                                            id: crumbMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.navigateTo(modelData.path)
                                        }
                                    }
                                }
                            }
                        }

                        AppTextField {
                            id: pathField
                            width: parent.width
                            height: root.rowHeight
                            visible: root.editingPath
                            placeholderText: qsTr("/absolute/path")
                            onAccepted: root.navigateTo(text)
                            Keys.onEscapePressed: {
                                root.editingPath = false;
                                pathField.text = dirModel.rootPath;
                            }
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        text: root.editingPath ? "\u2713" : "\u270E"
                        color: Theme.textSecondary
                        font.pixelSize: 15
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.editingPath) {
                                    root.navigateTo(pathField.text);
                                } else {
                                    pathField.text = dirModel.rootPath;
                                    root.editingPath = true;
                                    pathField.forceActiveFocus();
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    width: parent.width
                    spacing: 8

                    SecondaryButton {
                        text: qsTr("Up")
                        Layout.preferredHeight: root.rowHeight
                        onClicked: root.goUp()
                    }

                    SecondaryButton {
                        text: qsTr("Refresh")
                        Layout.preferredHeight: root.rowHeight
                        objectName: "filePickerRefresh"
                        onClicked: {
                            root.refreshLocations();
                            dirModel.refresh();
                        }
                    }

                    AppCheckBox {
                        id: hiddenBox
                        text: qsTr("Show hidden")
                        Layout.preferredHeight: root.rowHeight
                    }

                    SecondaryButton {
                        text: qsTr("New Folder")
                        Layout.preferredHeight: root.rowHeight
                        onClicked: {
                            root.creatingFolder = true;
                            newFolderField.text = "";
                            Qt.callLater(function () { newFolderField.forceActiveFocus(); });
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                    }

                    SecondaryButton {
                        implicitWidth: 38
                        iconSource: Icons.searchGray
                        iconSize: 18
                        hPadding: 0
                        Layout.preferredHeight: root.rowHeight
                        onClicked: root.openSearch()
                    }
                }

                RowLayout {
                    width: parent.width
                    spacing: 8
                    visible: root.creatingFolder

                    AppTextField {
                        id: newFolderField
                        objectName: "filePickerNewFolderName"
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.rowHeight
                        placeholderText: qsTr("New folder name")
                        onAccepted: root.createFolder()
                        Keys.onEscapePressed: root.creatingFolder = false
                    }

                    SecondaryButton {
                        text: qsTr("Cancel")
                        Layout.preferredHeight: root.rowHeight
                        onClicked: root.creatingFolder = false
                    }

                    PrimaryButton {
                        text: qsTr("Create")
                        Layout.preferredHeight: root.rowHeight
                        onClicked: root.createFolder()
                    }
                }

                RowLayout {
                    width: parent.width
                    spacing: 8
                    visible: root.searchVisible

                    AppTextField {
                        id: searchField
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.rowHeight
                        placeholderText: qsTr("Search current directory…")
                        onTextChanged: dirModel.searchText = text
                        Keys.onEscapePressed: root.clearSearch()
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 22
                        horizontalAlignment: Text.AlignHCenter
                        text: "\u2715"
                        color: Theme.textSecondary
                        font.pixelSize: 15
                        visible: searchField.text !== ""
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.clearSearch()
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: root.listHeight
                    radius: Theme.controlRadius
                    color: Theme.thumbBg
                    border.width: 1
                    border.color: Theme.border

                    ListView {
                        id: entryList
                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: dirModel
                        focus: true

                        Keys.onPressed: function (event) {
                            if (event.key === Qt.Key_Backspace) {
                                if (dirModel.searchText !== "" || searchField.text !== "") {
                                    if (!root.searchVisible)
                                        root.openSearch();
                                    searchField.text = searchField.text.substring(0, searchField.text.length - 1);
                                    event.accepted = true;
                                }
                                return;
                            }
                            if (event.key === Qt.Key_Escape) {
                                if (root.searchVisible || dirModel.searchText !== "") {
                                    root.clearSearch();
                                    event.accepted = true;
                                }
                                return;
                            }
                            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                                return;
                            var ch = event.text;
                            if (ch.length === 1 && ch.charCodeAt(0) >= 0x20 && ch.charCodeAt(0) !== 0x7f) {
                                root.openSearch();
                                searchField.text = searchField.text + ch;
                                event.accepted = true;
                            }
                        }

                        ScrollBar.vertical: ScrollBar {
                            id: entryBar
                            policy: ScrollBar.AsNeeded
                            background: Rectangle { color: "transparent" }
                            contentItem: Rectangle {
                                implicitWidth: 6
                                radius: 3
                                color: Theme.borderStrong
                                opacity: entryBar.size < 1.0 ? (entryBar.active || entryBar.pressed ? 1.0 : 0.5) : 0.0
                            }
                        }

                        delegate: Rectangle {
                            id: entryRow
                            readonly property bool rowSelected: root.multiple && !isDir
                                                               ? root.selectedPaths.indexOf(path) >= 0
                                                               : root.selectedPath === path
                            width: entryList.width
                            height: 30
                            radius: 4
                            color: rowSelected ? Theme.filePickerSelected
                                               : (entryMouse.containsMouse ? Theme.filePickerHover : "transparent")

                            Image {
                                id: entryIcon
                                anchors.left: parent.left
                                anchors.leftMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                source: isDir ? Icons.folderGray : Icons.fileGray
                                sourceSize.width: 16
                                sourceSize.height: 16
                            }

                            Text {
                                anchors.left: entryIcon.right
                                anchors.leftMargin: 8
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: name
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontNormal
                                elide: Text.ElideRight
                            }

                            MouseArea {
                                id: entryMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: function(mouse) { root.selectEntry(name, path, isDir, mouse.modifiers); }
                                onDoubleClicked: root.activateEntry(name, path, isDir)
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: dirModel.count === 0
                            text: dirModel.errorText !== "" ? dirModel.errorText
                                  : (searchField.text !== "" ? qsTr("No matches found")
                                                             : qsTr("(empty directory)"))
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontNormal
                        }
                    }
                }

            }
        }
    }

    footerFormContent: GridLayout {
        visible: !root.folderMode
        width: parent.width
        columns: width < 600 ? 2 : 4
        columnSpacing: Theme.dialogControlSpacing
        rowSpacing: Theme.dialogControlSpacing
        Text {
            visible: !root.folderMode
            Layout.preferredWidth: 56
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("File name")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
        }

        AppTextField {
            id: fileField
            objectName: "filePickerFileName"
            visible: !root.folderMode
            Layout.fillWidth: true
            Layout.preferredHeight: root.rowHeight
            placeholderText: root.multiple && root.selectedPaths.length > 0 ? qsTr("%1 file(s) selected").arg(root.selectedPaths.length) : root.saveMode ? qsTr("Enter or choose a file to save") : qsTr("Select File")
            onAccepted: root.confirmAccept()
        }

        Text {
            visible: !root.folderMode && root.filterLabels().length > 1
            Layout.preferredWidth: 56
            Layout.alignment: Qt.AlignVCenter
            text: root.saveMode ? qsTr("Save as type") : qsTr("File type")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
        }

        AppComboBox {
            id: filterCombo
            visible: !root.folderMode && root.filterLabels().length > 1
            Layout.preferredWidth: 180
            Layout.fillWidth: true
            Layout.preferredHeight: root.rowHeight
            model: root.filterLabels()
            onActivated: index => {
                root.activeFilter = index;
                root.applyFilter();
            }
        }
    }

    footerContent: [
        SecondaryButton {
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("Cancel")
            onClicked: root.close()
        },
        SecondaryButton {
            visible: root.skipText !== ""
            text: root.skipText
            onClicked: {
                root.skipped();
                root.close();
            }
        },
        PrimaryButton {
            Layout.alignment: Qt.AlignVCenter
            text: root.folderMode ? (root.selectedIsDir ? qsTr("Select this directory") : qsTr("Select this directory")) : (root.saveMode ? qsTr("Save") : qsTr("Select"))
            onClicked: root.confirmAccept()
        }
    ]

    DirModel {
        id: dirModel
        objectName: "filePickerDirModel"
        showFiles: !root.folderMode
        showHidden: hiddenBox.checked
        Component.onCompleted: app.language.registerRetranslatable(dirModel)
    }
}
