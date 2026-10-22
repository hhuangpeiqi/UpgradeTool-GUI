#include "application/BuildFeatures.h"
#if UPGRADE_TOOL_QUALCOMM
#include "gui/EdlRunner.h"
#include "gui/WindowsUsbDriver.h"
#include "backends/qualcomm/qdl_bridge.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include "platform/Process.h"
#include <chrono>
#include <thread>
#include <QRegularExpression>
#include "platform/SerialPort.h"
#include <QSerialPortInfo>
#include <QThread>
#include <QSet>
#include <vector>
#include <libusb.h>
#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#endif
#ifdef Q_OS_MACOS
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#endif
namespace gui {
namespace {
// Use EdlRunner::tr directly at each call site so lupdate resolves the class
// context ("gui::EdlRunner") rather than the enclosing namespace.
QString usbLocation(int bus, const QVector<int> &ports) {
    if (ports.isEmpty()) return {};
    QStringList parts; for (int p : ports) parts << QString::number(p);
    return QStringLiteral("usb:%1-%2").arg(bus).arg(parts.join('.'));
}
bool diagName(const QString &text) { return qtq_diag_name(text.toUtf8().constData()); }
QString hexId(const EdlEndpoint &e) { return QString("%1:%2").arg(e.vid,4,16,QLatin1Char('0')).arg(e.pid,4,16,QLatin1Char('0')); }
#ifdef Q_OS_LINUX
QString readText(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString(); }
void linuxIdentity(EdlEndpoint &e) {
    QString node = QFileInfo("/sys/class/tty/" + QFileInfo(e.path).fileName() + "/device").canonicalFilePath();
    while (!node.isEmpty() && node != "/") {
        if (QFileInfo::exists(node + "/bInterfaceNumber")) {
            e.interfaceNumber = readText(node + "/bInterfaceNumber").toInt(nullptr,16);
            e.suggested |= diagName(readText(node + "/interface"));
        }
        if (QFileInfo::exists(node + "/idVendor")) {
            e.vid = readText(node + "/idVendor").toUShort(nullptr,16);
            e.pid = readText(node + "/idProduct").toUShort(nullptr,16);
            e.serial = readText(node + "/serial");
            e.bus = readText(node + "/busnum").toInt();
            for (const auto &part : readText(node + "/devpath").split('.')) e.ports.append(part.toInt());
            e.location = usbLocation(e.bus,e.ports); break;
        }
        node = QFileInfo(node).absolutePath();
    }
}
#endif
#ifdef Q_OS_WIN
QString winProperty(HDEVINFO set, SP_DEVINFO_DATA &info, DWORD property, bool multi = false) {
    DWORD type = 0, size = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &info, property, &type, nullptr, 0, &size);
    if (!size || size > 65536) return {};
    std::vector<wchar_t> value(size / sizeof(wchar_t) + 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &info, property, &type, reinterpret_cast<PBYTE>(value.data()), size, nullptr)) return {};
    if (multi && type == REG_MULTI_SZ) {
        QStringList values;
        for (const wchar_t *p=value.data(); *p; p+=wcslen(p)+1) values.append(QString::fromWCharArray(p));
        return values.join('|');
    }
    return QString::fromWCharArray(value.data());
}
QVector<EdlEndpoint> nativeDevices() {
    QVector<EdlEndpoint> result;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return result;
    SP_DEVINFO_DATA info; info.cbSize = sizeof(info);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set,index,&info); ++index) {
        const QString hardware = winProperty(set,info,SPDRP_HARDWAREID);
        const QString compatible = winProperty(set,info,SPDRP_COMPATIBLEIDS,true);
        const QString service = winProperty(set,info,SPDRP_SERVICE);
        if (compatible.contains("USB\\COMPOSITE", Qt::CaseInsensitive) ||
            !service.compare("usbccgp", Qt::CaseInsensitive)) continue;
        const auto match = QRegularExpression("VID_([0-9A-F]{4})&PID_([0-9A-F]{4})", QRegularExpression::CaseInsensitiveOption).match(hardware);
        if (!match.hasMatch()) continue;
        EdlEndpoint e;
        wchar_t instance[MAX_DEVICE_ID_LEN] = {};
        if(CM_Get_Device_IDW(info.DevInst,instance,MAX_DEVICE_ID_LEN,0)==CR_SUCCESS) e.instance=QString::fromWCharArray(instance);
        e.vid = match.captured(1).toUShort(nullptr,16); e.pid = match.captured(2).toUShort(nullptr,16);
        const auto mi = QRegularExpression("&MI_([0-9A-F]{2})", QRegularExpression::CaseInsensitiveOption).match(hardware);
        e.interfaceNumber = mi.hasMatch() ? mi.captured(1).toInt(nullptr,16) : 0;
        e.label = winProperty(set,info,SPDRP_FRIENDLYNAME);
        if (e.label.isEmpty()) e.label = winProperty(set,info,SPDRP_DEVICEDESC);
        e.suggested = diagName(e.label) || qtq_known_diag(e.vid,e.pid,e.interfaceNumber);
        e.driverService = service;
        // Physical location paths survive a PID/driver/COM-number change.
        e.location = winProperty(set,info,SPDRP_LOCATION_PATHS).toUpper();
        e.location.remove(QRegularExpression("#USBMI\\([^)]*\\).*$"));
        auto portMatches = QRegularExpression("#USB\\(([0-9]+)\\)").globalMatch(e.location);
        while (portMatches.hasNext()) e.ports.append(portMatches.next().captured(1).toInt());
        DEVINST dev = info.DevInst;
        for (int depth = 0; depth < 8; ++depth) {
            wchar_t instance[MAX_DEVICE_ID_LEN] = {};
            if (CM_Get_Device_IDW(dev,instance,MAX_DEVICE_ID_LEN,0) != CR_SUCCESS) break;
            const QString id = QString::fromWCharArray(instance);
            if (id.startsWith("USB\\",Qt::CaseInsensitive) && !id.contains("&MI_",Qt::CaseInsensitive)) {
                e.serial = id.section('\\',-1);
                if (e.serial.contains('&')) e.serial.clear();
                break;
            }
            DEVINST parent; if (CM_Get_Parent(&parent,dev,0) != CR_SUCCESS) break; dev = parent;
        }
        HKEY key = SetupDiOpenDevRegKey(set,&info,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_READ);
        if (key != INVALID_HANDLE_VALUE) {
            wchar_t port[256] = {}; DWORD bytes = sizeof(port), type = 0;
            if (RegQueryValueExW(key,L"PortName",nullptr,&type,reinterpret_cast<BYTE *>(port),&bytes) == ERROR_SUCCESS && type == REG_SZ)
                e.path = QString::fromWCharArray(port);
            RegCloseKey(key);
        }
        for(const auto &id:compatible.split('|'))
            e.downloadInterface |= qtq_download_compatible_id(id.toLatin1().constData());
        if (compatible.contains("SUBCLASS_42&PROT_01", Qt::CaseInsensitive)) e.kind = EdlEndpoint::Adb;
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) != CR_SUCCESS) {
            e.accessError = LIBUSB_ERROR_OTHER;
            e.accessReason = EdlRunner::tr("Cannot check the interface driver. Refresh the device list.");
        } else if (problem == CM_PROB_FAILED_INSTALL || (service.isEmpty() && !problem)) {
            e.accessError = LIBUSB_ERROR_NOT_SUPPORTED;
            e.accessReason = EdlRunner::tr("The interface driver is missing. Open USB Drivers to install it.");
        } else if (problem || !(status & DN_STARTED)) {
            e.accessError = LIBUSB_ERROR_OTHER;
            e.accessReason = EdlRunner::tr("The interface driver is not ready (Windows error %1). Open USB Drivers for details.").arg(problem);
        } else if (e.path.isEmpty() && !WindowsUsbServiceCompatible(service)) {
            e.accessError = LIBUSB_ERROR_NOT_SUPPORTED;
            e.accessReason = EdlRunner::tr("The interface driver does not support this transport. Open USB Drivers for details.");
        }
        result.append(e);
    }
    SetupDiDestroyDeviceInfoList(set); return result;
}
#elif defined(Q_OS_MACOS)
QString cfString(CFTypeRef value) {
    if (!value || CFGetTypeID(value) != CFStringGetTypeID()) return {};
    char data[2048]; return CFStringGetCString(static_cast<CFStringRef>(value),data,sizeof(data),kCFStringEncodingUTF8) ? QString::fromUtf8(data) : QString();
}
int cfNumber(CFTypeRef value) {
    int number = 0;
    if (value && CFGetTypeID(value) == CFNumberGetTypeID()) CFNumberGetValue(static_cast<CFNumberRef>(value),kCFNumberIntType,&number);
    return number;
}
QVector<EdlEndpoint> nativeDevices() {
    QVector<EdlEndpoint> result;
    for (const char *service : {"IOSerialBSDClient", "IOUSBHostDevice"}) {
        io_iterator_t iterator = 0;
        if (IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching(service),&iterator) != KERN_SUCCESS) continue;
        io_registry_entry_t entry;
        while ((entry = IOIteratorNext(iterator))) {
            EdlEndpoint e;
            auto property = [&](CFStringRef key) { return IORegistryEntrySearchCFProperty(entry,kIOServicePlane,key,kCFAllocatorDefault,kIORegistryIterateRecursively | kIORegistryIterateParents); };
            auto stringValue = [&](CFStringRef key) { auto value=property(key); auto text=cfString(value); if(value) CFRelease(value); return text; };
            auto numberValue = [&](CFStringRef key) { auto value=property(key); auto number=cfNumber(value); if(value) CFRelease(value); return number; };
            e.path=stringValue(CFSTR("IOCalloutDevice")); e.serial=stringValue(CFSTR("USB Serial Number"));
            e.vid=numberValue(CFSTR("idVendor")); e.pid=numberValue(CFSTR("idProduct"));
            const int location=numberValue(CFSTR("locationID"));
            if(location) e.location=QString("iokit:%1").arg(quint32(location),8,16,QLatin1Char('0'));
            e.interfaceNumber=numberValue(CFSTR("bInterfaceNumber"));
            e.label=stringValue(CFSTR("USB Interface Name"));
            e.suggested=diagName(e.label);
            result.append(e); IOObjectRelease(entry);
        }
        IOObjectRelease(iterator);
    }
    return result;
}
#else
QVector<EdlEndpoint> nativeDevices() { return {}; }
#endif
QString usbSerial(libusb_device_handle *handle, const libusb_device_descriptor &descriptor) {
    char serial[128] = {};
    qtq_read_usb_serial(handle,descriptor.iSerialNumber,descriptor.iProduct,descriptor.idVendor,serial,sizeof(serial));
    return QString::fromUtf8(serial);
}
EdlEndpoint serialEndpoint(const QSerialPortInfo &port, const QVector<EdlEndpoint> &native) {
    EdlEndpoint e; e.path=port.systemLocation(); e.id="serial:"+e.path; e.serial=port.serialNumber();
    e.vid=port.vendorIdentifier(); e.pid=port.productIdentifier();
    e.label=port.portName()+" — "+port.description(); e.suggested=diagName(port.description());
#ifdef Q_OS_LINUX
    linuxIdentity(e);
#endif
    int bindings = 0;
    for (const auto &n : native) if (!n.path.isEmpty() && (n.path.compare(e.path,Qt::CaseInsensitive)==0 || n.path.compare(port.portName(),Qt::CaseInsensitive)==0)) {
        e.vid=n.vid; e.pid=n.pid; e.instance=n.instance; e.downloadInterface=n.downloadInterface; e.location=n.location; e.interfaceNumber=n.interfaceNumber; e.suggested |= n.suggested;
        ++bindings;
        e.accessError=n.accessError; e.accessReason=n.accessReason; e.driverService=n.driverService;
        if (e.serial.isEmpty()) e.serial=n.serial;
    }
#ifdef Q_OS_WIN
    if (bindings != 1) {
        e.accessError=LIBUSB_ERROR_NOT_FOUND;
        e.accessReason=EdlRunner::tr("Cannot uniquely identify the interface driver. Reconnect the device and refresh.");
    }
#else
    Q_UNUSED(bindings);
#endif
    e.suggested |= qtq_known_diag(e.vid,e.pid,e.interfaceNumber);
    return e;
}
QVector<EdlEndpoint> enumerate(const EdlCancel &cancel, QString &error) {
    QVector<EdlEndpoint> result;
    const auto native = nativeDevices();
    for (const auto &port : QSerialPortInfo::availablePorts()) {
        if (cancel && cancel->stop_requested()) return result;
        result.append(serialEndpoint(port, native));
    }
    qtq_usb_endpoint *rows = nullptr; size_t count = 0;
    if (qtq_discover_usb(&rows, &count)) {
        error = EdlRunner::tr("Cannot enumerate USB devices.");
    }
    for (size_t i = 0; i < count; ++i) {
        if (cancel && cancel->stop_requested()) break;
        const auto &row = rows[i]; const auto &d = row.device;
        EdlEndpoint e; e.vid=d.vid; e.pid=d.pid; e.kind=EdlEndpoint::Usb;
        e.bus=d.bus; e.address=d.address; e.accessError=d.access_error;
        for(unsigned int j=0;j<d.port_count;++j) e.ports.append(d.ports[j]);
        e.location=usbLocation(e.bus,e.ports);
#ifdef Q_OS_MACOS
        quint32 location = quint32(e.bus) << 24;
        bool representable = e.ports.size() <= 6;
        for (int port=0; port<e.ports.size() && port<6; ++port) {
            if (e.ports[port] > 15) representable = false;
            location |= quint32(e.ports[port]) << (20 - 4*port);
        }
        if (representable) e.location=QString("iokit:%1").arg(location,8,16,QLatin1Char('0'));
#endif
        e.serial=QString::fromUtf8(d.serial);
        QSet<QString> nativeLocations;
        for(const auto &n:native)
            if(!e.serial.isEmpty() && n.serial==e.serial && n.vid==e.vid && n.pid==e.pid && !n.location.isEmpty())
                nativeLocations.insert(n.location);
        if (nativeLocations.size()==1) e.location=*nativeLocations.begin();
        e.interfaceNumber=d.interface_number; e.alternate=d.alternate; e.in=d.in; e.out=d.out;
#ifdef Q_OS_WIN
        BindEdlNativeInterface(e, native);
#endif
        e.downloadInterface=qtq_download_interface(row.cls,row.subclass,row.protocol,e.in,e.out,row.in_size,row.out_size);
        e.suggested |= d.mode==QTQ_DIAG;
        if(d.mode==QTQ_ADB) {
            e.kind=EdlEndpoint::Adb; e.id="usb-adb:"+e.location; result.append(e); continue;
        }
        bool mapped=false;
        for(auto &existing:result) {
            if(existing.kind!=EdlEndpoint::Serial || existing.vid!=e.vid || existing.pid!=e.pid ||
                !EdlRunner::sameLocation(existing,e) || existing.interfaceNumber!=e.interfaceNumber) continue;
            // Only the default alternate belongs to an OS serial binding.
            if(e.alternate) continue;
            existing.downloadInterface=e.downloadInterface; existing.address=e.address;
            existing.bus=e.bus; existing.ports=e.ports;
            existing.suggested |= e.suggested; mapped=true; break;
        }
        if(mapped) continue;
        e.id=QString("usb:%1:%2:%3").arg(e.location).arg(e.interfaceNumber).arg(e.alternate);
        e.label=QString("USB %1 / %2 / IF %3 — %4").arg(hexId(e),e.location).arg(e.interfaceNumber).arg(QString::fromUtf8(row.name));
        if(e.in && e.out && row.in_size && row.out_size) result.append(e);
    }
    free(rows);
#ifdef Q_OS_WIN
    // Keep missing-driver candidates visible even when libusb cannot enumerate them.
    for (const auto &n : native) {
        if (!n.accessError || n.kind == EdlEndpoint::Adb ||
            !(n.suggested || qtq_download_identity(n.vid,n.pid))) continue;
        bool mapped = false;
        for (const auto &existing : result) if (existing.instance == n.instance) mapped = true;
        if (!mapped) { auto e=n; e.kind=EdlEndpoint::Usb; e.id="native:"+e.instance; result.append(e); }
    }
#endif
    return result;
}

bool process(const QString &exe,const QStringList &args,const EdlCancel &cancel,int timeout,QByteArray &output,QString &error,bool *started=nullptr) {
    if (cancel->stop_requested()) { error=EdlRunner::tr("ADB command cancelled or timed out."); return false; }
    auto child=platform::MakeAdbProcess();
    if(started) *started=false;
    std::vector<std::string> arguments; for(const auto &arg:args) arguments.push_back(arg.toStdString());
    try { child->Start(exe.toStdString(),arguments); if(started) *started=true; }
    catch(const std::exception &e) { error=EdlRunner::tr("ADB failed: %1").arg(QString::fromUtf8(e.what())); return false; }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    while(child->Running() && std::chrono::steady_clock::now()<deadline && !cancel->stop_requested()) {
        child->Poll(); if(child->Running()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(child->Running()) { child->Kill(); error=EdlRunner::tr("ADB command cancelled or timed out."); return false; }
    output=QByteArray::fromStdString(child->TakeOutput());
    if(!child->NormalExit() || child->ExitCode()!=0) { error=EdlRunner::tr("ADB failed: %1").arg(QString::fromLocal8Bit(output)); return false; }
    return true;
}
QVector<EdlEndpoint> discover(const QString &adb,const EdlCancel &cancel,QString &error) {
    auto endpoints=enumerate(cancel,error); QVector<EdlEndpoint> result;
    for(const auto &e:endpoints) if(e.kind!=EdlEndpoint::Adb) result.append(e);
    if(adb.isEmpty() || cancel->stop_requested()) return result;
    QByteArray output;
    if(!process(adb,{"devices","-l"},cancel,5000,output,error)) return result;
    for(const auto &line:QString::fromUtf8(output).split('\n')) {
        const auto fields=line.simplified().split(' ');
        if(fields.size()<2 || fields[1]!="device") continue;
        EdlEndpoint e; e.kind=EdlEndpoint::Adb; e.serial=fields[0]; e.id="adb:"+e.serial; e.label="ADB — "+e.serial;
        QVector<EdlEndpoint> matches;
        for(const auto &candidate:endpoints) if(candidate.kind==EdlEndpoint::Adb && candidate.serial==e.serial) matches.append(candidate);
        if(matches.size()==1) { e.location=matches[0].location; e.bus=matches[0].bus; e.ports=matches[0].ports; }
        // Native enumeration supplies serial/location even when the ADB driver cannot be opened by libusb.
        if(e.location.isEmpty()) {
            QSet<QString> locations;
            for(const auto &n:nativeDevices()) if(n.serial==e.serial && !n.location.isEmpty()) locations.insert(n.location);
            if(locations.size()==1) e.location=*locations.begin();
        }
#ifdef Q_OS_LINUX
        if(e.location.isEmpty()) for(const auto &field:fields) if(field.startsWith("usb:")) e.location=field;
#endif
        result.append(e);
    }
    return result;
}
class SerialDiagSession final : public DiagSession {
    std::unique_ptr<platform::IByteStream> port;
public:
    SerialDiagSession(const EdlEndpoint &e, QString &error) {
        // A recycled COM/tty name must not redirect a command to a different USB device.
        bool present=false;
        const auto native = nativeDevices();
        for(const auto &port : QSerialPortInfo::availablePorts()) {
            if (port.systemLocation() != e.path) continue;
            const auto current = serialEndpoint(port, native);
            if(current.vid==e.vid && current.pid==e.pid && (e.location.isEmpty() || current.location==e.location) && (e.serial.isEmpty() || current.serial==e.serial)) { present=true; break; }
        }
        if(!present) { error=EdlRunner::tr("The selected diagnostic port is no longer available."); return; }
        try { port=platform::OpenSerialPort(e.path.toStdString()); }
        catch(const std::exception &failure) { error=QString::fromUtf8(failure.what()); }

    }
    bool valid() const { return bool(port); }
    QByteArray exchange(const QByteArray &payload,const EdlCancel &cancel,QString &error,bool &sent) override {
        error.clear(); sent=false;
        if (cancel->stop_requested()) return {};
        const auto frame=EdlRunner::encode(payload);
        try {
            port->Clear();
            const auto bytes=std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(frame.constData()),static_cast<size_t>(frame.size()));
            const auto written=port->Write(bytes,std::chrono::milliseconds(1500),cancel->get_token());
            sent=written>0;
            if(written!=bytes.size() || cancel->stop_requested()) { error=EdlRunner::tr("Diagnostic write timed out or was cancelled."); return {}; }
            QByteArray buffer,reply;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);
            while(std::chrono::steady_clock::now()<deadline && !cancel->stop_requested()) {
                const auto part=port->Read(std::chrono::milliseconds(50),cancel->get_token());
                buffer.append(reinterpret_cast<const char *>(part.data()),static_cast<qsizetype>(part.size()));
                while(EdlRunner::decode(buffer,reply)) {
                    if(!reply.isEmpty() && (reply[0]==payload[0] || quint8(reply[0])==0x13 || quint8(reply[0])==0x14 || quint8(reply[0])==0x15 || quint8(reply[0])==0x18 || quint8(reply[0])==0x47)) return reply;
                }
            }
        } catch(const std::exception &failure) { error=QString::fromUtf8(failure.what()); return {}; }

        error=EdlRunner::tr("No valid DIAG response (timeout, disconnect or CRC error)."); return {};
    }
};

class UsbDiagSession final : public DiagSession {
    struct Usb {
        libusb_context *ctx=nullptr; libusb_device_handle *handle=nullptr; int iface=-1;
        ~Usb() { if(handle) { if(iface>=0) libusb_release_interface(handle,iface); libusb_close(handle); } if(ctx) libusb_exit(ctx); }
    } usb;

    EdlEndpoint endpoint;
    bool opened=false;
public:
    UsbDiagSession(const EdlEndpoint &e, QString &error) : endpoint(e) {
        if(libusb_init(&usb.ctx)) { error=EdlRunner::tr("Cannot initialize USB."); return; }
        libusb_device **list=nullptr; auto count=libusb_get_device_list(usb.ctx,&list); int code=LIBUSB_ERROR_NO_DEVICE;
        for(ssize_t i=0;i<count;++i) {
            libusb_device_descriptor d; if(libusb_get_device_descriptor(list[i],&d)) continue;
            if(d.idVendor!=e.vid || d.idProduct!=e.pid || (e.address && libusb_get_device_address(list[i])!=e.address) || libusb_get_bus_number(list[i])!=e.bus) continue;
            uint8_t ports[16]; int n=libusb_get_port_numbers(list[i],ports,sizeof(ports)); QVector<int> chain; for(int j=0;j<n;++j) chain.append(ports[j]);
            if(chain!=e.ports) continue;
            code=libusb_open(list[i],&usb.handle);
            if(!code && !e.serial.isEmpty() && usbSerial(usb.handle,d)!=e.serial) { libusb_close(usb.handle); usb.handle=nullptr; code=LIBUSB_ERROR_NO_DEVICE; }
            break;
        }
        if(list) libusb_free_device_list(list,1);
        if(!usb.handle) { error=QString::fromLatin1(libusb_error_name(code)); return; }
        // Recheck the chosen alternate and endpoints on the opened device before claiming.
        libusb_config_descriptor *config=nullptr; bool matches=false;
        if(!libusb_get_active_config_descriptor(libusb_get_device(usb.handle),&config)) {
            for(int i=0;i<config->bNumInterfaces;++i) for(int j=0;j<config->interface[i].num_altsetting;++j) {
                const auto &alt=config->interface[i].altsetting[j];
                if(alt.bInterfaceClass!=0xff || alt.bInterfaceNumber!=e.interfaceNumber || alt.bAlternateSetting!=e.alternate) continue;
                int in=0,out=0;
                for(int k=0;k<alt.bNumEndpoints;++k) {
                    const auto &ep=alt.endpoint[k];
                    if((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)!=LIBUSB_TRANSFER_TYPE_BULK || !ep.wMaxPacketSize) continue;
                    if(ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) in=ep.bEndpointAddress; else out=ep.bEndpointAddress;
                }
                matches=in==e.in && out==e.out;
            }
            libusb_free_config_descriptor(config);
        }
        if(!matches) { error=EdlRunner::tr("The selected diagnostic port is no longer available."); return; }
        // Detach only the explicitly selected interface, restored by libusb on release.
        libusb_set_auto_detach_kernel_driver(usb.handle,1);
        code=libusb_claim_interface(usb.handle,e.interfaceNumber);
        if(code) { error=QString::fromLatin1(libusb_error_name(code)); return; }
        usb.iface=e.interfaceNumber;
        if(e.alternate) { code=libusb_set_interface_alt_setting(usb.handle,e.interfaceNumber,e.alternate); if(code) { error=QString::fromLatin1(libusb_error_name(code)); return; } }

        opened=true;
    }
    bool valid() const { return opened; }
    QByteArray exchange(const QByteArray &payload,const EdlCancel &cancel,QString &error,bool &sent) override {
        const auto &e=endpoint; int code=0;
        error.clear(); sent=false;
        if(cancel->stop_requested()) return {};
        const auto frame=EdlRunner::encode(payload); int written=0;
        code=libusb_bulk_transfer(usb.handle,e.out,reinterpret_cast<unsigned char *>(const_cast<char *>(frame.constData())),frame.size(),&written,1000);
        sent = written > 0;
        if(code || written!=frame.size()) { error=EdlRunner::tr("Diagnostic USB write failed: %1").arg(QString::fromLatin1(libusb_error_name(code))); return {}; }
        QElapsedTimer timer; timer.start(); QByteArray buffer,reply;
        while(timer.elapsed()<1500 && !cancel->stop_requested()) {
            unsigned char data[4096]; int size=0; code=libusb_bulk_transfer(usb.handle,e.in,data,sizeof(data),&size,100);
            if(size>0) buffer.append(reinterpret_cast<char *>(data),size);
            while(EdlRunner::decode(buffer,reply)) if(!reply.isEmpty() && (reply[0]==payload[0] || quint8(reply[0])==0x13 || quint8(reply[0])==0x14 || quint8(reply[0])==0x15 || quint8(reply[0])==0x18 || quint8(reply[0])==0x47)) return reply;
            if(code && code!=LIBUSB_ERROR_TIMEOUT) break;
        }
        error=EdlRunner::tr("No valid DIAG response (timeout, disconnect or CRC error)."); return {};
    }
};

}
EdlHooks DefaultEdlHooks() {
    EdlHooks hooks;
    hooks.discover=discover;
    hooks.openDiag=[](const EdlEndpoint &e,const EdlCancel &cancel,QString &error) -> std::unique_ptr<DiagSession> {
        error.clear(); if(cancel->stop_requested()) return {};
        if(e.kind==EdlEndpoint::Serial) {
            std::unique_ptr<SerialDiagSession> session(new SerialDiagSession(e,error));
            if(!session->valid()) return {};
            return std::unique_ptr<DiagSession>(session.release());
        }
        std::unique_ptr<UsbDiagSession> session(new UsbDiagSession(e,error));
        if(!session->valid()) return {};
        return std::unique_ptr<DiagSession>(session.release());
    };
    hooks.reboot=[](const QString &adb,const QString &serial,const EdlCancel &cancel,QString &error) {
        error.clear(); QByteArray output;
        if(!process(adb,{"devices"},cancel,5000,output,error)) return false;
        bool present=false;
        for(const auto &line:QString::fromUtf8(output).split('\n')) { const auto fields=line.simplified().split(' '); if(fields.size()>=2 && fields[0]==serial && fields[1]=="device") present=true; }
        if(!present) { error=EdlRunner::tr("The selected ADB device is disconnected or unauthorized."); return false; }
        bool started=false;
        process(adb,{"-s",serial,"reboot","edl"},cancel,10000,output,error,&started);
        return started;
    };
    hooks.scan=[] {
        QString error; auto result = enumerate({},error);
#ifdef Q_OS_WIN
        const auto drivers = ScanWindowsUsbDrivers().devices;
        for (const auto &native : nativeDevices()) {
            if (native.mode() != QTQ_EDL) continue;
            bool missing = false;
            for (const auto &d : drivers)
                if (d.instance == native.instance && d.needsAttention()) missing = true;
            if (!missing) continue;
            bool mapped = false;
            for (const auto &existing : result) if (existing.instance == native.instance) mapped = true;
            if (!mapped) { auto e = native; e.kind = EdlEndpoint::Usb; e.id = "native:" + e.instance; result.append(e); }
        }
#endif
        return result;
    };
    return hooks;
}
}

#else
#include "gui/EdlRunner.h"
namespace gui {
EdlHooks DefaultEdlHooks() {
    EdlHooks hooks;
    hooks.discover=[](const QString &,const EdlCancel &,QString &error) { error=EdlRunner::tr("Qualcomm backend is not available"); return QVector<EdlEndpoint>{}; };
    hooks.openDiag=[](const EdlEndpoint &,const EdlCancel &,QString &error)->std::unique_ptr<DiagSession> { error=EdlRunner::tr("Qualcomm backend is not available"); return {}; };
    hooks.reboot=[](const QString &,const QString &,const EdlCancel &,QString &error) { error=EdlRunner::tr("Qualcomm backend is not available"); return false; };
    hooks.scan=[] { return QVector<EdlEndpoint>{}; };
    return hooks;
}
}
#endif
