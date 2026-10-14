import UpgradeTool
import QtQuick
import QtQuick.Layouts

QualcommWorkspace {
    objectName: "QualcommToolsPage"
    title: qsTr("Maintenance & Offline Tools")
    FormSection {
        title: qsTr("Quick actions")
        GridLayout {
            Layout.fillWidth: true
            columns: width >= 960 ? 2 : 1
            columnSpacing: 18
            rowSpacing: 8
            QualcommToolPanel { toolIndex: 0; operation: Operations.QualcommChipInfo; context: "chipinfo"; Layout.alignment: Qt.AlignTop }
            QualcommToolPanel { toolIndex: 1; operation: Operations.QualcommReset; context: "reset"; Layout.alignment: Qt.AlignTop }
        }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    FormSection {
        title: qsTr("Device maintenance")
        QualcommToolPanel { toolIndex: 2; operation: Operations.QualcommRamdump; context: "ramdump" }
        QualcommToolPanel { toolIndex: 3; operation: Operations.QualcommKickstart; context: "kickstart" }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    FormSection {
        title: qsTr("Offline tools")
        QualcommToolPanel { toolIndex: 4; operation: Operations.QualcommCreateZip; context: "zip" }
        QualcommToolPanel { toolIndex: 5; operation: Operations.QualcommCreateArchive; context: "archive" }
        QualcommToolPanel { toolIndex: 6; operation: Operations.QualcommCreateDigests; context: "digests" }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    QualcommToolPanel { toolIndex: 7; operation: Operations.QualcommProvision; context: "provision" }
}
