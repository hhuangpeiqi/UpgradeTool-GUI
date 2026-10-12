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
    // Location names come from C++ (translated); rebuild them when the UI
    // language changes, since retranslate() only refreshes qsTr bindings.
    property var placesModel: app.standardPlaces()

    signal accepted()

    Connections {
        target: app.language
        function onLanguageChanged() { root.placesModel = app.standardPlaces() }
    }

    readonly property int listHeight: 400
    readonly property int rowHeight: 34
    readonly property int maxCrumbWidth: 220

    width: 720

    onAboutToShow: reset()

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
        var p = dirModel.rootPath;
        var list = [];
        if (p === "") {
            crumbs = list;
            return;
        }
        if (p.charAt(0) === "/")
            list.push({ name: "/", path: "/" });
        var parts = p.split("/");
        var accum = "";
        for (var i = 0; i < parts.length; i++) {
            var seg = parts[i];
            if (seg === "")
                continue;
            if (accum === "")
                accum = (p.charAt(0) === "/" ? "/" + seg : seg);
            else
                accum = accum + "/" + seg;
            list.push({ name: seg, path: accum });
        }
        crumbs = list;
    }

    function navigateTo(path) {
        clearSearch();
        if (!path || path === "")
            return;
        dirModel.rootPath = path;
        selectedPath = "";
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
        Qt.callLater(function () { entryList.forceActiveFocus(); });
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
        var p = dirModel.rootPath;
        if (p === "/" || p === "")
            return;
        var idx = p.lastIndexOf("/");
        navigateTo(idx > 0 ? p.substring(0, idx) : "/");
    }

    function reset() {
        hintText = "";
        hintError = false;
        editingPath = false;
        creatingFolder = false;
        selectedPath = "";
        selectedIsDir = false;
        activeFilter = 0;
        hiddenBox.checked = false;
        filterCombo.currentIndex = 0;
        applyFilter();
        clearSearch();
        var start = "";
        if (persistKey !== "" && app.lastDir(persistKey) !== "")
            start = app.lastDir(persistKey);
        else if (defaultFolder.toString() !== "")
            start = app.toLocalPath(defaultFolder.toString());
        else
            start = app.homeDir();
        navigateTo(start);
        if (!folderMode)
            fileField.text = "";
    }

    function selectEntry(name, path, isDir) {
        selectedPath = path;
        selectedIsDir = isDir;
        hintText = "";
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
            if (!saveMode)
                confirmAccept();
        }
    }

    function createFolder() {
        var err = dirModel.makeDirectory(newFolderField.text);
        if (err === "") {
            creatingFolder = false;
            hintText = qsTr("Directory created");
            hintError = false;
        } else {
            hintText = err;
            hintError = true;
        }
    }

    function confirmAccept() {
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
                    hintText = qsTr("Enter a file name");
                    hintError = true;
                    fileField.forceActiveFocus();
                    return;
                }
            } else if (name.indexOf("/") >= 0) {
                hintText = qsTr("File name cannot contain \"/\"");
                hintError = true;
                return;
            } else {
                var folder = dirModel.rootPath;
                var full = (folder === "/" ? "" : folder) + "/" + name;
                if (saveMode && defaultSuffix !== "" && name.indexOf(".") < 0) {
                    full += (defaultSuffix.charAt(0) === "." ? defaultSuffix
                                                            : "." + defaultSuffix);
                }
                selectedFile = app.pathUrl(full);
            }
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
                    model: root.placesModel

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        background: Rectangle { color: "transparent" }
                        contentItem: Rectangle {
                            implicitWidth: 6
                            radius: 3
                            color: Theme.borderStrong
                            opacity: 0.5
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
                        onClicked: dirModel.refresh()
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
                            policy: ScrollBar.AsNeeded
                            background: Rectangle { color: "transparent" }
                            contentItem: Rectangle {
                                implicitWidth: 6
                                radius: 3
                                color: Theme.borderStrong
                                opacity: 0.5
                            }
                        }

                        delegate: Rectangle {
                            id: entryRow
                            width: entryList.width
                            height: 30
                            radius: 4
                            color: root.selectedPath === path
                                   ? Theme.selected
                                   : (entryMouse.containsMouse ? Theme.hover : "transparent")

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
                                onClicked: root.selectEntry(name, path, isDir)
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

    footerContent: RowLayout {
        width: parent.width
        spacing: 8

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
            visible: !root.folderMode
            Layout.fillWidth: true
            Layout.preferredHeight: root.rowHeight
            placeholderText: root.saveMode ? qsTr("Enter or choose a file to save") : qsTr("Select File")
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
            Layout.preferredHeight: root.rowHeight
            model: root.filterLabels()
            onActivated: (index) => {
                root.activeFilter = index;
                root.applyFilter();
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
        }

        SecondaryButton {
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("Cancel")
            onClicked: root.close()
        }

        PrimaryButton {
            Layout.alignment: Qt.AlignVCenter
            text: root.folderMode
                  ? (root.selectedIsDir ? qsTr("Select this directory") : qsTr("Select this directory"))
                  : (root.saveMode ? qsTr("Save") : qsTr("Select"))
            onClicked: root.confirmAccept()
        }
    }

    DirModel {
        id: dirModel
        showFiles: !root.folderMode
        showHidden: hiddenBox.checked
    }
}
