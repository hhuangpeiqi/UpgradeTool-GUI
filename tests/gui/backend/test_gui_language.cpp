#include "tests/support/qt/TestDevices.h"
#include "gui/backend/AppController.h"
#include "gui/backend/DirModel.h"
#include "gui/backend/LanguageManager.h"
#include "gui/QtObserver.h"
#include "gui/OperationSpec.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QSettings>
#include <libusb.h>
#include <thread>
#include <atomic>
#include <cstdio>
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include "BackendTests.h"

int TestLanguage([[maybe_unused]] QGuiApplication &application) {
    int failures = 0;
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());

	// Changing the effective language retranslates the UI, which rebuilds the
	// language combo box model and resets its currentIndex. The settings dialog
	// restores the selection from modeChanged, so it must be emitted after
	// languageChanged.
	{
		gui::AppController controller(NULL);
		QStringList order;
		QObject::connect(controller.language(), &gui::LanguageManager::languageChanged,
		                 &application, [&order] { order << QStringLiteral("language"); });
		QObject::connect(controller.language(), &gui::LanguageManager::modeChanged,
		                 &application, [&order] { order << QStringLiteral("mode"); });
		controller.language()->setMode("en");
		controller.language()->setMode("zh_CN");
		CHECK(order.endsWith(QStringLiteral("mode")));
		CHECK(order.indexOf(QStringLiteral("language")) < order.lastIndexOf(QStringLiteral("mode")));
		controller.language()->setMode("auto");
	}

	// Every cached C++ string (page hints, firmware table, config status) is
	// rebuilt through LanguageManager's registry, so switching the language
	// updates them immediately rather than only on the next action.
	{
		gui::AppController controller(NULL);
		controller.language()->setMode("zh_CN");
		const QString zhAdvanced = controller.rockchip()->advancedHint();
		controller.rockchip()->setImageDir(temporary.path());                       // no parameter.txt -> error hint
		const QString zhImage = controller.rockchip()->imageHint();
		controller.rockchip()->setFirmware(temporary.filePath("missing.img"));      // unparsable -> invalid hint
		const QString zhFirmware = controller.rockchip()->firmwareHint();
		controller.config()->load();
		const QString zhStatus = controller.config()->status();
		const QVariantList zhPlaces = controller.standardPlaces();
		CHECK(!zhAdvanced.isEmpty() && !zhImage.isEmpty() && !zhFirmware.isEmpty() && !zhStatus.isEmpty());
		CHECK(!zhPlaces.isEmpty());

		controller.language()->setMode("en");
		CHECK(controller.rockchip()->advancedHint() != zhAdvanced);
		CHECK(controller.rockchip()->imageHint() != zhImage);
		CHECK(controller.rockchip()->firmwareHint() != zhFirmware);
		CHECK(controller.config()->status() != zhStatus);
		CHECK(controller.standardPlaces().first().toMap()["name"]
		      != zhPlaces.first().toMap()["name"]);
		controller.language()->setMode("auto");
	}

	// DirModel is created in QML; it registers itself the same way and rebuilds
	// its cached error text through the same registry.
	{
		gui::LanguageManager language;
		gui::DirModel dir;
		language.registerRetranslatable(&dir);
		language.setMode("zh_CN");
		CHECK(!dir.makeDirectory(".."));                                // invalid name -> translated error
		const QString zhError = dir.errorText();
		CHECK(!zhError.isEmpty());
		language.setMode("en");
		CHECK(!dir.errorText().isEmpty());
		CHECK(dir.errorText() != zhError);
		language.setMode("auto");
	}

    return failures;
}
