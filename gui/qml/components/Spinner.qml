import UpgradeTool
import QtQuick

Item {
    id: root

    property int size: 18
    property color color: Theme.primary
    property real thickness: Math.max(2, size / 9)
    property bool running: true
    property color trackColor: Qt.rgba(color.r, color.g, color.b, 0.2)

    implicitWidth: size
    implicitHeight: size

    onSizeChanged: canvas.requestPaint()
    onColorChanged: canvas.requestPaint()
    onTrackColorChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            var lw = root.thickness;
            var r = Math.min(width, height) / 2 - lw / 2;
            if (r <= 0)
                return;
            var cx = width / 2;
            var cy = height / 2;

            ctx.lineWidth = lw;
            ctx.lineCap = "round";

            ctx.beginPath();
            ctx.strokeStyle = root.trackColor;
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.stroke();

            ctx.beginPath();
            ctx.strokeStyle = root.color;
            ctx.arc(cx, cy, r, -Math.PI / 2, Math.PI * 1.15);
            ctx.stroke();
        }
    }

    NumberAnimation {
        target: canvas
        property: "rotation"
        from: 0
        to: 360
        duration: 820
        loops: Animation.Infinite
        running: root.running && root.visible
    }
}
