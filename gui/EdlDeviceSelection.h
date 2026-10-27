#pragma once
#include "gui/EdlRunner.h"
#include "platform/UsbLocation.h"
#include <QVariantMap>

namespace gui {
inline QString EdlDevicePathKey(QString path) {
    path = path.trimmed();
    if (path.startsWith(QStringLiteral("\\\\.\\"))) path.remove(0, 4);
    return path.toUpper();
}

// Match the handoff identity, not a remembered list position. Physical location
// may bridge a native/USB candidate to its eventual QDLoader binding; execution
// still receives the fresh backend-created DeviceRef.
inline bool MatchesEdlDevice(const EdlEndpoint &expected, const QVariantMap &row) {
    if (row.value("vendor").toInt() != 2 || row.value("qualcommMode").toInt() != QTQ_EDL ||
        row.value("accessError").toInt() || !row.value("reference").value<core::DeviceRef>() ||
        row.value("vid").toUInt() != expected.vid || row.value("pid").toUInt() != expected.pid) return false;
    const auto location = row.value("physicalLocation").toString();
    if (!expected.location.isEmpty() && !location.isEmpty()) {
        const auto a = platform::NormalizeUsbLocation(expected.location.toStdString());
        const auto b = platform::NormalizeUsbLocation(location.toStdString());
        if (a == b) return true;
        // Contradictory native Windows locations cannot be rescued by a COM
        // number, serial or coincidentally equal USB port chain.
        if (a.find('#') != std::string::npos && b.find('#') != std::string::npos) return false;
    }
    if (row.value("transport", -1).toInt() == int(core::QualcommTransport::Qud)) {
        const auto instance = row.value("deviceInstance").toString();
        if (!expected.instance.isEmpty() && !instance.isEmpty())
            return instance.compare(expected.instance, Qt::CaseInsensitive) == 0;
        return !expected.path.isEmpty() && !expected.serial.isEmpty() &&
            EdlDevicePathKey(row.value("devicePath").toString()) == EdlDevicePathKey(expected.path) &&
            row.value("serial").toString().compare(expected.serial, Qt::CaseInsensitive) == 0;
    }
    QVector<int> ports;
    for (const auto &port : row.value("ports").toList()) ports.append(port.toInt());
    return !expected.ports.isEmpty() && row.value("bus").toInt() == expected.bus && ports == expected.ports &&
        row.value("usbAddress").toUInt() == expected.address &&
        row.value("interfaceNumber", -1).toInt() == expected.interfaceNumber &&
        row.value("alternate").toInt() == expected.alternate &&
        (expected.serial.isEmpty() || row.value("serial").toString().compare(expected.serial, Qt::CaseInsensitive) == 0);
}

// Windows prefers the official transport only among matches for this target.
// Multiple candidates of the same priority remain ambiguous.
inline int SelectEdlDevice(const EdlEndpoint &expected, const QVariantList &rows) {
    int selected = -1, priority = -1, matches = 0;
    for (int i = 0; i < rows.size(); ++i) {
        const auto row = rows[i].toMap();
        if (!MatchesEdlDevice(expected, row)) continue;
        int candidatePriority = 0;
#ifdef Q_OS_WIN
        candidatePriority = row.value("transport", -1).toInt() == int(core::QualcommTransport::Qud) ? 1 : 0;
#endif
        if (candidatePriority > priority) { priority = candidatePriority; selected = i; matches = 1; }
        else if (candidatePriority == priority) ++matches;
    }
    return matches == 1 ? selected : -1;
}
}
