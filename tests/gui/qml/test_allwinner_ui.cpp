#include "tests/support/qt/Await.h"
#include "tests/support/qt/TestDevices.h"
#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QImage>
#include <QKeyEvent>
#include <libusb.h>
#include <cstdio>
#include <functional>

static int failures=0;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL line %d: %s\n",__LINE__,#c); ++failures; } } while(0)
int main(int argc, char **argv) {
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME",temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME",temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QGuiApplication application(argc,argv);
    QQuickStyle::setStyle("Basic");
    qmlRegisterUncreatableMetaObject(gui::Operations::staticMetaObject,"UpgradeTool",1,0,"Operations","Operation IDs");
    qmlRegisterType<gui::DirModel>("UpgradeTool",1,0,"DirModel");
    if(argc<3 || libusb_init(nullptr)<0) return 2;
    {
        gui::AppController controller(nullptr);
        // Device state below is supplied by SnapshotDevices. A board connected
        // to the test host must not replace those deterministic snapshots.
        QObject::disconnect(&controller, &gui::AppController::requestScan, nullptr, nullptr);
        controller.setVendor(1); controller.ignorePermission();
        for(int context=0;context<3;++context) {
            controller.allwinner()->setContextFirmware(context,QString::fromLocal8Bit(argv[1]));
            CHECK(Await([&] { return controller.allwinner()->states()[context].toMap()["valid"].toBool(); }));
        }
        QQmlApplicationEngine engine;
        QObject::connect(&engine,&QQmlEngine::warnings,&application,[&](const QList<QQmlError> &errors) {
            for(const auto &error:errors) { std::printf("QML: %s\n",error.toString().toUtf8().constData()); ++failures; }
        });
        engine.rootContext()->setContextProperty("app",&controller);
        engine.rootContext()->setContextProperty("startTab",1);
        engine.load(QUrl("qrc:/qt/qml/UpgradeTool/Main.qml"));
        CHECK(!engine.rootObjects().isEmpty());
        if(engine.rootObjects().isEmpty()) return 1;
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        CHECK(window);
        if(!window) return 1;
        std::function<QQuickItem *(QQuickItem *,const QString &)> visualFind;
        visualFind=[&](QQuickItem *parent,const QString &name) -> QQuickItem * {
            if(parent->objectName()==name) return parent;
            for(auto *child:parent->childItems()) if(auto *found=visualFind(child,name)) return found;
            return nullptr;
        };
        auto find=[&](const char *name) {
            auto *item=window->findChild<QQuickItem *>(name);
            if(!item) item=visualFind(window->contentItem(),QString::fromLatin1(name));
            if(!item) qFatal("Missing UI control: %s",name);
            return item;
        };
        auto *flash=find("awFlashStart"), *raw=find("awRawStart"), *convert=find("awConvertStart");
        auto *reboot=find("awReboot"), *deviceInfo=find("awDeviceInfo");
        CHECK(!flash->isEnabled() && !raw->isEnabled() && !convert->isEnabled());
        CHECK(!reboot->isEnabled() && !deviceInfo->isEnabled()); // No device yet.
        auto *tools=find("AllwinnerToolsPage");
        CHECK(visualFind(tools, "awDeviceInfo") == deviceInfo);
        CHECK(tools->findChild<QObject *>("AllwinnerDeviceDialog"));
        auto *gpt=find("rkGenerateGpt");
        controller.setVendor(0);
        window->setProperty("currentPage", 2);
        CHECK(!controller.hasDevice() && gpt->isEnabled());
        auto *firmwareInfo = find("rkAdvancedFirmwareInfo");
        CHECK(!firmwareInfo->isEnabled());
        find("rkAdvancedFirmwarePath")->setProperty("text", QString::fromLocal8Bit(argv[1]));
        CHECK(firmwareInfo->isEnabled()); // Firmware tools work without a device.

        window->resize(1024, 640);
        CHECK(Await([&] {
            return gpt->width()>0 && gpt->x()>=0
                && gpt->x()+gpt->width()<=gpt->parentItem()->width();
        }));
        window->resize(1300, 780);
        CHECK(QMetaObject::invokeMethod(gpt, "clicked"));
        auto *gptDialog=window->findChild<QObject *>("rkGptDialog");
        CHECK(gptDialog && gptDialog->property("visible").toBool());
        if (gptDialog) CHECK(QMetaObject::invokeMethod(gptDialog, "close"));
        controller.tasks()->setBusy(true);
        CHECK(!gpt->isEnabled() && !firmwareInfo->isEnabled());
        controller.tasks()->setBusy(false);
        controller.setVendor(1);
        window->setProperty("currentPage", 1);
        find("awConvertOutput")->setProperty("text",temporary.filePath("programmer.img"));
        CHECK(convert->isEnabled()); // Offline conversion does not need USB.
        find("awRawImage")->setProperty("text","disk.img");
        CHECK(!raw->isEnabled());
        QVariantMap device{{"vendor",1},{"index",1},{"key","allwinner:250-254"},{"bus",250},{"ports",QVariantList{254}}};
        CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList, SnapshotDevices(QVariantList{device}))));
        CHECK(flash->isEnabled() && raw->isEnabled() && reboot->isEnabled() && deviceInfo->isEnabled());
        device["accessError"]=5;
        CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList, SnapshotDevices(QVariantList{device}))));
        CHECK(controller.hasDevice() && !controller.hasFlashTarget());
        CHECK(!flash->isEnabled() && !raw->isEnabled() && !reboot->isEnabled() && !deviceInfo->isEnabled());
        CHECK(convert->isEnabled());
        device["accessError"]=0;
        CHECK(QMetaObject::invokeMethod(&controller,"onDevicesReady",Qt::DirectConnection,Q_ARG(QVariantList, SnapshotDevices(QVariantList{device}))));
        CHECK(flash->isEnabled() && raw->isEnabled() && reboot->isEnabled() && deviceInfo->isEnabled());

        controller.allwinner()->setContextFirmware(2, QString());
        CHECK(!controller.allwinner()->states()[2].toMap()["valid"].toBool());
        CHECK(deviceInfo->isEnabled()); // Device info is independent of firmware.
        controller.tasks()->setBusy(true);
        CHECK(!deviceInfo->isEnabled());
        controller.tasks()->setBusy(false);
        controller.allwinner()->setContextFirmware(2, QString::fromLocal8Bit(argv[1]));
        CHECK(Await([&] { return controller.allwinner()->states()[2].toMap()["valid"].toBool(); }));

        CHECK(!find("awFlashPartitions")->isVisible());
        find("awFlashMode")->setProperty("currentIndex",0);
        CHECK(find("awFlashPartitions")->isVisible());
        CHECK(!flash->isEnabled());
        auto *partition=find("awPartition_rootfs");
        partition->setProperty("checked",true);
        CHECK(QMetaObject::invokeMethod(partition,"toggled",Qt::DirectConnection));
        CHECK(flash->isEnabled());
        find("awFlashMode")->setProperty("currentIndex",3);
        CHECK(!find("awFlashPartitions")->isVisible());
        auto *verify=find("awVerify");
        verify->forceActiveFocus(Qt::TabFocusReason);
        CHECK(verify->hasActiveFocus());
        const bool checked=verify->property("checked").toBool();
        QKeyEvent press(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier), release(QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);
        QGuiApplication::sendEvent(window,&press); QGuiApplication::sendEvent(window,&release);
        CHECK(verify->property("checked").toBool()!=checked);
        QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);
        QGuiApplication::sendEvent(window,&tab);
        CHECK(window->activeFocusItem() && window->activeFocusItem()!=verify);
        CHECK(!find("awRawAdvanced")->property("expanded").toBool());
        CHECK(!find("awConvertAdvanced")->property("expanded").toBool());
        CHECK(QMetaObject::invokeMethod(&controller,"onAdbAvailabilityChanged",Qt::DirectConnection,
            Q_ARG(bool,true),Q_ARG(QString,QString())));
        CHECK(QMetaObject::invokeMethod(&controller,"onAdbDevicesChanged",Qt::DirectConnection,
            Q_ARG(QStringList,QStringList{"fake-board"})));
        for (int context=0;context<2;++context) {
            auto *page=find(context==0?"AllwinnerRawPage":"AllwinnerFirmwarePage");
            auto *switchButton=visualFind(page,"awSwitchButton");
            auto *switchMethod=visualFind(page,"awSwitchMethod");
            CHECK(switchButton && switchMethod);
            if (switchButton && switchMethod) {
                CHECK(switchMethod->property("currentIndex").toInt()==0);
                CHECK(switchMethod->property("model").toStringList()==QStringList({QStringLiteral("Auto"),QStringLiteral("Alternate")}));
                controller.allwinner()->setContextFirmware(context,QString());
                CHECK(!controller.allwinner()->states()[context].toMap()["valid"].toBool());
                CHECK(switchButton->isEnabled()); // Switching needs ADB, not a firmware package.
                switchMethod->setProperty("currentIndex",1);
                CHECK(switchButton->isEnabled());
                switchMethod->setProperty("currentIndex",0);
                controller.allwinner()->setContextFirmware(context,QString::fromLocal8Bit(argv[1]));
            }
        }
        const QDir output(QString::fromLocal8Bit(argv[2])); QDir().mkpath(output.path());
        for(const auto &theme:QStringList{"light","dark"}) {
            controller.theme()->setMode(theme);
            for(int small=0;small<2;++small) {
                window->resize(small?1024:1300,small?640:780);
                for(int page=0;page<3;++page) {
                    window->setProperty("currentPage",page);
                    window->requestUpdate();
                    CHECK(Await([&] { return window->contentItem()->width()==window->width()
                        && window->contentItem()->height()==window->height(); }));
                    QCoreApplication::processEvents();
                    QImage screenshot;
                    CHECK(Await([&] {
                        screenshot=window->grabWindow();
                        return !screenshot.isNull() && screenshot.width()==window->width()*window->devicePixelRatio();
                    }));
                    CHECK(screenshot.save(output.filePath(QString("%1-%2-page%3.png").arg(theme).arg(small?"small":"normal").arg(page))));
                    auto *button=page==0?raw:page==1?flash:convert;
                    if(page<2) {
                        auto *workspace=find(page==0?"AllwinnerRawPage":"AllwinnerFirmwarePage");
                        auto *switchButton=visualFind(workspace,"awSwitchButton");
                        auto *switchMethod=visualFind(workspace,"awSwitchMethod");
                        CHECK(switchButton && switchMethod);
                        if(switchButton && switchMethod) {
                            const auto start=button->mapToScene(QPointF(0,button->height()/2));
                            const auto toggle=switchButton->mapToScene(QPointF(0,switchButton->height()/2));
                            const auto method=switchMethod->mapToScene(QPointF(0,switchMethod->height()/2));
                            CHECK(method.x()>=0 && method.x()+switchMethod->width()<=toggle.x());
                            CHECK(toggle.x()+switchButton->width()<=start.x());
                            CHECK(qAbs(toggle.y()-start.y())<1 && qAbs(method.y()-start.y())<1);
                        }
                    }
                    const auto point=button->mapToScene(QPointF(0,0));
                    CHECK(point.x()>=0 && point.y()>=0 && point.x()+button->width()<=window->width()
                        && point.y()+button->height()<=window->height());
                }
            }
        }
    }
    libusb_exit(nullptr);
    std::printf("Allwinner UI: %d failures\n",failures); return failures?1:0;
}
