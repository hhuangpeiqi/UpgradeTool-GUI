pragma Singleton

import QtQuick

QtObject {
    property bool dark: false

    property color primary: dark ? "#1F6FEB" : "#0D6BD6"
    property color primaryHover: dark ? "#3B87F5" : "#0B5FC0"
    property color primaryPressed: dark ? "#1A5FCC" : "#0A55AC"
    property color titleBar: dark ? "#232428" : "#FFFFFF"
    property color titleBarBorder: dark ? "#35373C" : "#E3E6EA"
    property color titleBarText: dark ? "#E3E5E8" : "#1F2329"
    property color titleBarGlyph: dark ? "#C2C7CD" : "#43474D"
    property color primarySoft: dark ? "#22355A" : "#E8F1FC"

    property color pageBg: dark ? "#1E1F22" : "#F4F5F7"
    property color card: dark ? "#2B2D31" : "#FFFFFF"
    property color tableHeader: dark ? "#32353A" : "#F7F8FA"
    property color hover: dark ? "#3A3D42" : "#F0F2F5"
    property color selected: dark ? "#2A3B52" : "#EAF2FD"
    property color filePickerHover: dark ? hover : "#E2ECF9"
    property color filePickerSelected: dark ? selected : "#C9DFFD"
    property color rowAlt: dark ? "#2F3237" : "#FBFCFD"
    property color statusBar: dark ? "#24262A" : "#FAFBFC"
    property color thumbBg: dark ? "#34373C" : "#F1F3F5"

    property color border: dark ? "#3A3D42" : "#E3E6EA"
    property color borderStrong: dark ? "#4A4E55" : "#D5D9DE"
    property color rowDivider: dark ? "#32353A" : "#F1F3F5"

    property color textPrimary: dark ? "#E3E5E8" : "#1F2329"
    property color textSecondary: dark ? "#9BA1A6" : "#6B7280"
    property color textDisabled: dark ? "#6B7075" : "#B4B8BE"
    property color textOnPrimary: "#FFFFFF"

    property color danger: dark ? "#FF6B6B" : "#C00000"
    property color success: dark ? "#57C271" : "#1E8E3E"
    property color warning: dark ? "#F5BE62" : "#915600"
    property color warningSoft: dark ? "#382F22" : "#FFF5E5"
    property color warningBorder: dark ? "#6D5430" : "#E8C58F"
    property color track: dark ? "#3A3D42" : "#E6E8EB"

    property color fieldDisabled: dark ? "#26282B" : "#F5F6F8"
    property color buttonFace: card
    property color buttonFaceHover: dark ? "#34373C" : "#F1F3F6"
    property color buttonFacePressed: dark ? "#3F434A" : "#E6E9ED"
    property color buttonFaceDisabled: dark ? "#26282B" : "#F5F6F8"
    property color primaryDisabledBg: dark ? "#3C4A5A" : "#AFC6E2"
    property color primaryDisabledText: dark ? "#8A9099" : "#EDF1F6"

    property color scrim: dark ? "#99000000" : "#66000000"
    property color closeHover: "#E81123"
    property color closeGlyphHover: "#FFFFFF"
    property color toastErrorBg: dark ? "#3A2527" : "#FDECEC"
    property color toastErrorBorder: dark ? "#6E3B3D" : "#F3B8B8"
    property color toastOkBg: dark ? "#22332A" : "#EAF6EE"
    property color toastOkBorder: dark ? "#33543F" : "#B9E0C6"

    // Shared label column width so every form row in a workspace lines up.
    // Chosen to fit the longest label ("Programmer input") on one line.
    readonly property int formLabelWidth: 132

    readonly property int radius: 8
    readonly property int controlRadius: 6
    readonly property int windowRadius: 10
    readonly property int titleBarHeight: 48
    readonly property int navBarHeight: 52
    readonly property int statusBarHeight: 84

    readonly property string fontFamily: Qt.application.font.family
    readonly property int fontSmall: 12
    readonly property int fontNormal: 13
    readonly property int fontMedium: 15
    readonly property int fontLarge: 17
    readonly property int fontTitle: 20
    readonly property int fontHuge: 26
}
