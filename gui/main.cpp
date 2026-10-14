#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include <libusb.h>

#include "Config.h"
#include "RKLog.h"
#include "platform/PlatformPath.h"
#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/OperationSpec.h"

CRKLog *g_pLogObject = NULL;

int main(int argc, char *argv[])
{
	QGuiApplication app(argc, argv);
	app.setApplicationName("upgrade_tool");
	app.setApplicationDisplayName(QStringLiteral("UpgradeTool-GUI"));
	app.setOrganizationName("upgrade_tool");
#ifndef Q_OS_MACOS
	app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/UpgradeTool/icons/upgrade_tool.svg")));
#endif
	QQuickStyle::setStyle("Basic");

	qmlRegisterUncreatableMetaObject(gui::Operations::staticMetaObject, "UpgradeTool", 1, 0, "Operations", "Business operation identifiers");

	qmlRegisterType<gui::DirModel>("UpgradeTool", 1, 0, "DirModel");

	std::string strLogDir = platform::GetUserLogDir();
	std::string strConfigFile = platform::FindConfigFile();
	struct stat statBuf;

	platform::EnsureDirRecursive(strLogDir);
	CRKLog *pLog = new CRKLog(strLogDir.c_str(), "log", true);
	g_pLogObject = pLog;

	if (stat(strConfigFile.c_str(), &statBuf) == 0 && S_ISREG(statBuf.st_mode))
		parse_config_file(strConfigFile.c_str(), g_ConfigItemVec);

	if (libusb_init(NULL) < 0) {
		delete pLog;
		return -1;
	}

	QString strScreenshot;
	QString strImageDir;
	QString strFirmware;
	QString strLanguage;
	int iTab = 1;
    int vendor = 0;
	for (int i = 1; i < argc; i++) {
		if (qstrcmp(argv[i], "--vendor") == 0 && i + 1 < argc)
            { ++i; vendor = qstrcmp(argv[i], "qualcomm") == 0 ? 2 : qstrcmp(argv[i], "allwinner") == 0 ? 1 : 0; }
        else if (qstrcmp(argv[i], "--lang") == 0 && i + 1 < argc)
			strLanguage = QString::fromLocal8Bit(argv[++i]);
		else if (qstrcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
			strScreenshot = QString::fromLocal8Bit(argv[++i]);
		else if (qstrcmp(argv[i], "--tab") == 0 && i + 1 < argc)
			iTab = atoi(argv[++i]);
		else if (qstrcmp(argv[i], "--image-dir") == 0 && i + 1 < argc)
			strImageDir = QString::fromLocal8Bit(argv[++i]);
		else if (qstrcmp(argv[i], "--firmware") == 0 && i + 1 < argc)
			strFirmware = QString::fromLocal8Bit(argv[++i]);
	}

	int iRet = 0;
	{
		gui::AppController controller(pLog);
        controller.setVendor(vendor);
        if (!strLanguage.isEmpty()) controller.language()->setMode(strLanguage);
        if(!strScreenshot.isEmpty()) controller.ignorePermission();

		QQmlApplicationEngine engine;
		engine.rootContext()->setContextProperty("app", &controller);
		engine.rootContext()->setContextProperty("startTab", iTab);
		QObject::connect(controller.language(), &gui::LanguageManager::languageChanged,
		                 &engine, &QQmlApplicationEngine::retranslate);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
		engine.loadFromModule("UpgradeTool", "Main");
#else
		engine.load(QUrl(QStringLiteral("qrc:/qt/qml/UpgradeTool/Main.qml")));
#endif
		if (engine.rootObjects().isEmpty()) {
			libusb_exit(NULL);
			delete pLog;
			return -1;
		}

		if (!strImageDir.isEmpty())
			controller.setImageDir(strImageDir);
		if (!strFirmware.isEmpty()) {
			if(vendor == 2) controller.qualcomm()->loadFirmware(strFirmware);
            else if(vendor == 1) controller.setAllwinnerContextFirmware(qBound(0, iTab, 2), strFirmware);
            else controller.setFirmware(strFirmware);
        }

		if (!strScreenshot.isEmpty()) {
			QTimer::singleShot(1500, &app, [&engine, strScreenshot]() {
				QQuickWindow *pWin = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
				if (pWin)
					pWin->grabWindow().save(strScreenshot);
				QGuiApplication::quit();
			});
		}

		iRet = app.exec();
	}

	delete pLog;
	libusb_exit(NULL);
	return iRet;
}
