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


    {
        gui::AppController controller(nullptr);
        CHECK(controller.bookmarkedPlaces().isEmpty());
        CHECK(!controller.addBookmark(""));
        CHECK(!controller.addBookmark(temporary.path() + "/missing"));
        int changes = 0;
        QObject::connect(&controller, &gui::AppController::bookmarkedPlacesChanged,
                         &application, [&] { ++changes; });
        CHECK(controller.addBookmark(temporary.path()));
        CHECK(controller.addBookmark(temporary.path() + "/./"));
        CHECK(changes == 1);
        CHECK(controller.bookmarkedPlaces().size() == 1);
        CHECK(controller.isBookmarked(temporary.path()));
#ifdef Q_OS_WIN
        CHECK(controller.isBookmarked(temporary.path().toUpper()));
#endif
        QFile file(temporary.path() + "/file");
        CHECK(file.open(QIODevice::WriteOnly));
        file.close();
        CHECK(!controller.addBookmark(file.fileName()));
        QDir(temporary.path()).mkdir("removed");
        CHECK(controller.addBookmark(temporary.path() + "/removed"));
        QDir(temporary.path()).rmdir("removed");
        CHECK(controller.bookmarkedPlaces().size() == 2);
    }
    {
        gui::AppController controller(nullptr);
        CHECK(controller.bookmarkedPlaces().size() == 2);
        controller.removeBookmark(temporary.path() + "/removed");
        controller.removeBookmark(temporary.path() + "/./");
        CHECK(controller.bookmarkedPlaces().isEmpty());
    }
    {
        gui::AppController controller(nullptr);
        CHECK(controller.bookmarkedPlaces().isEmpty());
    }

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
		int placeNotifications = 0;
		QObject::connect(&controller, &gui::AppController::standardPlacesChanged,
		                 &controller, [&] { ++placeNotifications; });
		controller.refreshStandardPlaces();
		controller.refreshStandardPlaces();
		CHECK(controller.standardPlaces() == zhPlaces);
		CHECK(placeNotifications == 0);
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
