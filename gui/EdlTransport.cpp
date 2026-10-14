#include "gui/EdlRunner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QSerialPort>
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
bool diagName(const QString &text) {
    return QRegularExpression(QStringLiteral("\\b(diag(?:nostic|nostics)?|qcdm)\\b"), QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
}
bool knownDiag(quint16 vid, quint16 pid, int iface) {
    // Interface-level hints only. A valid DIAG_VERNO_F reply is still mandatory.
    static const struct { quint16 vid, pid; int iface; } devices[] = {
        {0x05c6,0x9091,0}, {0x05c6,0x9092,0}, {0x05c6,0x901d,0}, {0x05c6,0x676c,0},
        {0x2c7c,0x0125,0}, {0x2c7c,0x0700,0}, {0x1199,0x9071,0}, {0x1199,0x9091,0},
        {0x0846,0x68e2,2}, {0x19d2,0x1404,2}, {0x413c,0x81d7,5},
        {0x1bc7,0x1040,0}, {0x1bc7,0x1041,0}, {0x1bc7,0x1201,0}
    };
    for (const auto &d : devices) if (d.vid == vid && d.pid == pid && d.iface == iface) return true;
    return false;
}
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
QString winProperty(HDEVINFO set, SP_DEVINFO_DATA &info, DWORD property) {
    DWORD type = 0, size = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &info, property, &type, nullptr, 0, &size);
    if (!size || size > 65536) return {};
    std::vector<wchar_t> value(size / sizeof(wchar_t) + 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &info, property, &type, reinterpret_cast<PBYTE>(value.data()), size, nullptr)) return {};
    return QString::fromWCharArray(value.data());
}
QVector<EdlEndpoint> nativeDevices() {
    QVector<EdlEndpoint> result;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return result;
    SP_DEVINFO_DATA info; info.cbSize = sizeof(info);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set,index,&info); ++index) {
        const QString hardware = winProperty(set,info,SPDRP_HARDWAREID);
        const auto match = QRegularExpression("VID_([0-9A-F]{4})&PID_([0-9A-F]{4})", QRegularExpression::CaseInsensitiveOption).match(hardware);
        if (!match.hasMatch()) continue;
        EdlEndpoint e; e.vid = match.captured(1).toUShort(nullptr,16); e.pid = match.captured(2).toUShort(nullptr,16);
        const auto mi = QRegularExpression("&MI_([0-9A-F]{2})", QRegularExpression::CaseInsensitiveOption).match(hardware);
        if (mi.hasMatch()) e.interfaceNumber = mi.captured(1).toInt(nullptr,16);
        e.label = winProperty(set,info,SPDRP_FRIENDLYNAME);
        if (e.label.isEmpty()) e.label = winProperty(set,info,SPDRP_DEVICEDESC);
        e.suggested = diagName(e.label);
        // Physical location paths survive a PID/driver/COM-number change.
        e.location = winProperty(set,info,SPDRP_LOCATION_PATHS).toUpper();
        e.location.remove(QRegularExpression("#USBMI\\([^)]*\\).*$"));
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
        e.edl = e.pid == 0x9008;
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
            e.suggested=diagName(e.label); e.edl=e.pid==0x9008;
            result.append(e); IOObjectRelease(entry);
        }
        IOObjectRelease(iterator);
    }
    return result;
}
#else
QVector<EdlEndpoint> nativeDevices() { return {}; }
#endif
QString usbString(libusb_device_handle *handle, int index) {
    if (!handle || !index) return {};
    unsigned char data[256]; const int size=libusb_get_string_descriptor_ascii(handle,index,data,sizeof(data));
    return size > 0 ? QString::fromUtf8(reinterpret_cast<char *>(data),size) : QString();
}
EdlEndpoint serialEndpoint(const QSerialPortInfo &port, const QVector<EdlEndpoint> &native) {
    EdlEndpoint e; e.path=port.systemLocation(); e.id="serial:"+e.path; e.serial=port.serialNumber();
    e.vid=port.vendorIdentifier(); e.pid=port.productIdentifier();
    e.label=port.portName()+" — "+port.description(); e.suggested=diagName(port.description());
#ifdef Q_OS_LINUX
    linuxIdentity(e);
#endif
    for (const auto &n : native) if (!n.path.isEmpty() && (n.path.compare(e.path,Qt::CaseInsensitive)==0 || n.path.compare(port.portName(),Qt::CaseInsensitive)==0)) {
        e.vid=n.vid; e.pid=n.pid; e.location=n.location; e.interfaceNumber=n.interfaceNumber; e.suggested |= n.suggested;
        if (e.serial.isEmpty()) e.serial=n.serial;
    }
    e.edl=e.pid==0x9008; e.suggested |= knownDiag(e.vid,e.pid,e.interfaceNumber);
    return e;
}
QVector<EdlEndpoint> enumerate(bool includeUnknown, const EdlCancel &cancel = {}) {
    QVector<EdlEndpoint> result;
    const auto native = nativeDevices();
    for (const auto &port : QSerialPortInfo::availablePorts()) {
        if (cancel && cancel->load()) return result;
        const auto e = serialEndpoint(port, native);
        if (includeUnknown || e.edl) result.append(e);
    }
    libusb_context *ctx=nullptr;
    if(libusb_init(&ctx)) return result;
    libusb_device **list=nullptr; const auto count=libusb_get_device_list(ctx,&list);
    for(ssize_t i=0;i<count;++i) {
        if (cancel && cancel->load()) break;
        libusb_device_descriptor d;
        if(libusb_get_device_descriptor(list[i],&d)) continue;
        if(!includeUnknown && d.idProduct!=0x9008) continue;
        libusb_config_descriptor *config=nullptr;
        if(libusb_get_active_config_descriptor(list[i],&config)) continue;
        bool vendorInterface = false;
        for (int j=0;j<config->bNumInterfaces;++j)
            for (int k=0;k<config->interface[j].num_altsetting;++k)
                vendorInterface |= config->interface[j].altsetting[k].bInterfaceClass == 0xff;
        if (!vendorInterface) { libusb_free_config_descriptor(config); continue; }
        EdlEndpoint base; base.vid=d.idVendor; base.pid=d.idProduct; base.kind=EdlEndpoint::Usb;
        base.bus=libusb_get_bus_number(list[i]); uint8_t ports[16]; const int n=libusb_get_port_numbers(list[i],ports,sizeof(ports));
        for(int j=0;j<n;++j) base.ports.append(ports[j]);
        base.location=usbLocation(base.bus,base.ports);
#ifdef Q_OS_MACOS
        // libusb's Darwin bus number is locationID >> 24; each port occupies a nibble.
        quint32 location = quint32(base.bus) << 24;
        bool representable = base.ports.size() <= 6;
        for (int port=0; port<base.ports.size() && port<6; ++port) {
            if (base.ports[port] > 15) representable = false;
            location |= quint32(base.ports[port]) << (20 - 4*port);
        }
        if (representable) base.location=QString("iokit:%1").arg(location,8,16,QLatin1Char('0'));
#endif
        libusb_device_handle *handle=nullptr;
        // Read descriptors only; no interface is claimed during discovery.
        if (includeUnknown) libusb_open(list[i],&handle);
        base.serial=usbString(handle,d.iSerialNumber);
        QSet<QString> nativeLocations;
        for(const auto &nativeDevice:native) {
            if(!base.serial.isEmpty() && nativeDevice.serial==base.serial && nativeDevice.vid==base.vid && nativeDevice.pid==base.pid && !nativeDevice.location.isEmpty())
                nativeLocations.insert(nativeDevice.location);
        }
        if (nativeLocations.size() == 1) base.location = *nativeLocations.begin();
        for(int j=0;j<config->bNumInterfaces;++j) for(int k=0;k<config->interface[j].num_altsetting;++k) {
            const auto &alt=config->interface[j].altsetting[k];
            if(alt.bInterfaceClass!=0xff) continue;
            EdlEndpoint e=base; e.interfaceNumber=alt.bInterfaceNumber; e.alternate=alt.bAlternateSetting;
            for(int ep=0;ep<alt.bNumEndpoints;++ep) {
                const auto &endpoint=alt.endpoint[ep];
                if((endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)!=LIBUSB_TRANSFER_TYPE_BULK) continue;
                if(endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) e.in=endpoint.bEndpointAddress; else e.out=endpoint.bEndpointAddress;
            }
            if(!e.in || !e.out) continue;
            e.edl=d.idProduct==0x9008 && alt.bInterfaceSubClass==0xff && (alt.bInterfaceProtocol==0xff || alt.bInterfaceProtocol==0x10 || alt.bInterfaceProtocol==0x11 || alt.bInterfaceProtocol==0x13);
            if(alt.bInterfaceSubClass==0x42 && alt.bInterfaceProtocol==1) {
                // Retain ADB topology for association, but never offer it as a DIAG endpoint.
                e.kind=EdlEndpoint::Adb; e.id="usb-adb:"+base.location; result.append(e); continue;
            }
            bool mapped=false;
            for(const auto &existing:result) if(existing.kind==EdlEndpoint::Serial && EdlRunner::sameLocation(existing,e) && existing.interfaceNumber==e.interfaceNumber) { mapped=true; break; }
            if(mapped) continue;
            e.id=QString("usb:%1:%2:%3").arg(base.location).arg(e.interfaceNumber).arg(e.alternate);
            const QString name=usbString(handle,alt.iInterface);
            e.label=QString("USB %1 / %2 / IF %3 — %4").arg(hexId(e),base.location).arg(e.interfaceNumber).arg(name);
            e.suggested=diagName(name) || knownDiag(e.vid,e.pid,e.interfaceNumber);
            if(includeUnknown || e.edl) result.append(e);
        }
        if(handle) libusb_close(handle);
        libusb_free_config_descriptor(config);
    }
    if(list) libusb_free_device_list(list,1);
    libusb_exit(ctx); return result;
}
bool process(const QString &exe,const QStringList &args,const EdlCancel &cancel,int timeout,QByteArray &output,QString &error,bool *started=nullptr) {
    if (cancel->load()) { error=EdlRunner::tr("ADB command cancelled or timed out."); return false; }
    QProcess child;
    if(started) { *started=false; QObject::connect(&child,&QProcess::started,&child,[started] { *started=true; }); }
    child.setProcessChannelMode(QProcess::MergedChannels); child.start(exe,args);
    QElapsedTimer timer; timer.start();
    while(child.state()!=QProcess::NotRunning && timer.elapsed()<timeout && !cancel->load()) child.waitForFinished(50);
    if(child.state()!=QProcess::NotRunning) {
        child.kill(); child.waitForFinished(1000); error=EdlRunner::tr("ADB command cancelled or timed out."); return false;
    }
    output=child.readAll();
    if(child.error()==QProcess::FailedToStart || child.exitStatus()!=QProcess::NormalExit || child.exitCode()!=0) {
        error=EdlRunner::tr("ADB failed: %1").arg(output.isEmpty()?child.errorString():QString::fromLocal8Bit(output)); return false;
    }
    return true;
}
QVector<EdlEndpoint> discover(const QString &adb,const EdlCancel &cancel,QString &error) {
    auto endpoints=enumerate(true,cancel); QVector<EdlEndpoint> result;
    for(const auto &e:endpoints) if(e.kind!=EdlEndpoint::Adb) result.append(e);
    if(adb.isEmpty() || cancel->load()) return result;
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
QByteArray serialExchange(const EdlEndpoint &e,const QByteArray &payload,const EdlCancel &cancel,QString &error,bool &sent) {
    // A recycled COM/tty name must not redirect a command to a different USB device.
    bool present=false;
    const auto native = nativeDevices();
    for(const auto &port : QSerialPortInfo::availablePorts()) {
        if (port.systemLocation() != e.path) continue;
        const auto current = serialEndpoint(port, native);
        if(current.vid==e.vid && current.pid==e.pid && (e.location.isEmpty() || current.location==e.location) && (e.serial.isEmpty() || current.serial==e.serial)) { present=true; break; }
    }
    if(!present) { error=EdlRunner::tr("The selected diagnostic port is no longer available."); return {}; }
    QSerialPort port; port.setPortName(e.path); port.setBaudRate(115200);
    port.setDataBits(QSerialPort::Data8); port.setParity(QSerialPort::NoParity); port.setStopBits(QSerialPort::OneStop); port.setFlowControl(QSerialPort::NoFlowControl);
    if(!port.open(QIODevice::ReadWrite)) { error=port.errorString(); return {}; }
    if (cancel->load()) return {};
    port.clear(); const auto frame=EdlRunner::encode(payload);
    if(port.write(frame)!=frame.size()) { error=port.errorString(); return {}; }
    QElapsedTimer timer; timer.start();
    while(port.bytesToWrite()>0 && timer.elapsed()<1500 && !cancel->load()) port.waitForBytesWritten(50);
    sent = port.bytesToWrite() < frame.size();
    if(port.bytesToWrite()>0 || cancel->load()) { error=EdlRunner::tr("Diagnostic write timed out or was cancelled."); return {}; }
    QByteArray buffer,reply; timer.restart();
    while(timer.elapsed()<1500 && !cancel->load()) {
        port.waitForReadyRead(50); buffer+=port.readAll();
        while(EdlRunner::decode(buffer,reply)) {
            if(!reply.isEmpty() && (reply[0]==payload[0] || quint8(reply[0])==0x13 || quint8(reply[0])==0x14 || quint8(reply[0])==0x15 || quint8(reply[0])==0x18 || quint8(reply[0])==0x47)) return reply;
        }
        if(port.error()==QSerialPort::ResourceError) break;
    }
    error=EdlRunner::tr("No valid DIAG response (timeout, disconnect or CRC error)."); return {};
}
QByteArray usbExchange(const EdlEndpoint &e,const QByteArray &payload,const EdlCancel &cancel,QString &error,bool &sent) {
    struct Usb {
        libusb_context *ctx=nullptr; libusb_device_handle *handle=nullptr; int iface=-1;
        ~Usb() { if(handle) { if(iface>=0) libusb_release_interface(handle,iface); libusb_close(handle); } if(ctx) libusb_exit(ctx); }
    } usb;
    if(libusb_init(&usb.ctx)) { error=EdlRunner::tr("Cannot initialize USB."); return {}; }
    libusb_device **list=nullptr; auto count=libusb_get_device_list(usb.ctx,&list); int code=LIBUSB_ERROR_NO_DEVICE;
    for(ssize_t i=0;i<count;++i) {
        libusb_device_descriptor d; if(libusb_get_device_descriptor(list[i],&d)) continue;
        if(d.idVendor!=e.vid || d.idProduct!=e.pid || libusb_get_bus_number(list[i])!=e.bus) continue;
        uint8_t ports[16]; int n=libusb_get_port_numbers(list[i],ports,sizeof(ports)); QVector<int> chain; for(int j=0;j<n;++j) chain.append(ports[j]);
        if(chain!=e.ports) continue;
        code=libusb_open(list[i],&usb.handle);
        if(!code && !e.serial.isEmpty() && usbString(usb.handle,d.iSerialNumber)!=e.serial) { libusb_close(usb.handle); usb.handle=nullptr; code=LIBUSB_ERROR_NO_DEVICE; }
        break;
    }
    if(list) libusb_free_device_list(list,1);
    if(!usb.handle) { error=QString::fromLatin1(libusb_error_name(code)); return {}; }
    // Detach only the explicitly selected interface, restored by libusb on release.
    libusb_set_auto_detach_kernel_driver(usb.handle,1);
    code=libusb_claim_interface(usb.handle,e.interfaceNumber);
    if(code) { error=QString::fromLatin1(libusb_error_name(code)); return {}; }
    usb.iface=e.interfaceNumber;
    if(e.alternate) { code=libusb_set_interface_alt_setting(usb.handle,e.interfaceNumber,e.alternate); if(code) { error=QString::fromLatin1(libusb_error_name(code)); return {}; } }
    if(cancel->load()) return {};
    const auto frame=EdlRunner::encode(payload); int written=0;
    code=libusb_bulk_transfer(usb.handle,e.out,reinterpret_cast<unsigned char *>(const_cast<char *>(frame.constData())),frame.size(),&written,1000);
    sent = written > 0;
    if(code || written!=frame.size()) { error=EdlRunner::tr("Diagnostic USB write failed: %1").arg(QString::fromLatin1(libusb_error_name(code))); return {}; }
    QElapsedTimer timer; timer.start(); QByteArray buffer,reply;
    while(timer.elapsed()<1500 && !cancel->load()) {
        unsigned char data[4096]; int size=0; code=libusb_bulk_transfer(usb.handle,e.in,data,sizeof(data),&size,100);
        if(size>0) buffer.append(reinterpret_cast<char *>(data),size);
        while(EdlRunner::decode(buffer,reply)) if(!reply.isEmpty() && (reply[0]==payload[0] || quint8(reply[0])==0x13 || quint8(reply[0])==0x14 || quint8(reply[0])==0x15 || quint8(reply[0])==0x18 || quint8(reply[0])==0x47)) return reply;
        if(code && code!=LIBUSB_ERROR_TIMEOUT) break;
    }
    error=EdlRunner::tr("No valid DIAG response (timeout, disconnect or CRC error)."); return {};
}
}
EdlHooks DefaultEdlHooks() {
    EdlHooks hooks;
    hooks.discover=discover;
    hooks.exchange=[](const EdlEndpoint &e,const QByteArray &command,const EdlCancel &cancel,QString &error,bool &sent) {
        error.clear(); sent=false; return e.kind==EdlEndpoint::Serial ? serialExchange(e,command,cancel,error,sent) : usbExchange(e,command,cancel,error,sent);
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
    hooks.scan=[] { return enumerate(false); };
    return hooks;
}
}
