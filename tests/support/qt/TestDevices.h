#ifndef TEST_DEVICES_H
#define TEST_DEVICES_H
#include "backends/DeviceIdentity.h"
#include "gui/EngineWorker.h"
#include <QVariantMap>
// Test-only enumeration fixture. Production preserves backend-created snapshots.
inline QVariantList SnapshotDevices(QVariantList rows) {
    for(auto &row:rows) {
        auto map=row.toMap();
        if(!map.contains("reference")) {
            core::DeviceIdentity identity;
            identity.vendor=static_cast<core::Vendor>(map.value("vendor",0).toInt());
            identity.vid=map.value("vid").toUInt(); identity.pid=map.value("pid").toUInt();
            identity.location=map.value("locationId").toUInt(); identity.serial=map.value("serial").toString().toStdString();
            identity.devicePath=map.value("devicePath").toString().toStdString(); identity.deviceInstance=map.value("deviceInstance").toString().toStdString();
            identity.physicalLocation=map.value("physicalLocation").toString().toStdString();
            identity.transport=static_cast<core::QualcommTransport>(map.value("transport",-1).toInt());
            identity.bus=map.value("bus").toUInt(); identity.usbAddress=map.value("usbAddress").toUInt();
            identity.interfaceNumber=map.value("interfaceNumber",-1).toInt(); identity.alternate=map.value("alternate").toInt();
            identity.inEndpoint=map.value("inEndpoint").toInt(); identity.outEndpoint=map.value("outEndpoint").toInt();
            for(const auto &port:map.value("ports").toList()) identity.ports.push_back(port.toUInt());
            map["reference"]=QVariant::fromValue(core::DeviceRef(identity));
        }
        row=map;
    }
    return rows;
}
#endif
