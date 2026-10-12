#include "gui/backend/LanguageManager.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

#include "gui/backend/Retranslatable.h"

namespace gui {

namespace {

const char *kSettingsKey = "ui/language";
const char *kAutoMode = "auto";
const char *kChinese = "zh_CN";
const char *kEnglish = "en";

QString SystemLanguage()
{
	const QString str = QLocale::system().name();
	return str.startsWith(QLatin1String("zh")) ? QLatin1String(kChinese)
	                                           : QLatin1String(kEnglish);
}

}

LanguageManager::LanguageManager(QObject *parent)
	: QObject(parent),
	  m_bAppLoaded(false),
	  m_bQtLoaded(false)
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope,
	                   "upgrade_tool", "upgrade_tool");
	m_strMode = NormalizeMode(settings.value(QString::fromLatin1(kSettingsKey),
	                                         QString::fromLatin1(kAutoMode)).toString());
	Apply();
}

QString LanguageManager::NormalizeMode(const QString &strMode)
{
	const QString str = strMode.trimmed().toLower();
	if (str == QLatin1String("en"))
		return QString::fromLatin1(kEnglish);
	if (str == QLatin1String("zh") || str == QLatin1String("zh_cn"))
		return QString::fromLatin1(kChinese);
	return QString::fromLatin1(kAutoMode);
}

QString LanguageManager::Effective() const
{
	if (m_strMode == QLatin1String(kChinese))
		return QString::fromLatin1(kChinese);
	if (m_strMode == QLatin1String(kEnglish))
		return QString::fromLatin1(kEnglish);
	return SystemLanguage();
}

void LanguageManager::setMode(const QString &strMode)
{
	const QString str = NormalizeMode(strMode);
	if (str == m_strMode)
		return;

	m_strMode = str;
	QSettings settings(QSettings::IniFormat, QSettings::UserScope,
	                   "upgrade_tool", "upgrade_tool");
	settings.setValue(QString::fromLatin1(kSettingsKey), m_strMode);

	// Apply first: retranslating the UI rebuilds the language combo box model,
	// and a Qt model change resets its currentIndex back to the first item.
	// Observers of modeChanged restore the selection, so the notification must
	// come after the retranslation.
	Apply();
	emit modeChanged();
}

void LanguageManager::registerRetranslatable(QObject *target)
{
	if (!target || !dynamic_cast<Retranslatable *>(target))
		return;
	for (const QPointer<QObject> &existing : m_targets)
		if (existing == target)
			return;
	m_targets.append(target);
}

void LanguageManager::RetranslateAll()
{
	for (const QPointer<QObject> &target : m_targets) {
		if (!target)
			continue;
		if (auto *retranslatable = dynamic_cast<Retranslatable *>(target.data()))
			retranslatable->retranslate();
	}
}

void LanguageManager::Apply()
{
	const QString strEffective = Effective();
	const bool bChinese = strEffective == QLatin1String(kChinese);

	// Application catalog: English is the source language, so only Chinese needs a translator.
	if (bChinese) {
		if (!m_bAppLoaded) {
			m_appTranslator.load(QLatin1String("upgrade_tool_zh_CN"), QLatin1String(":/i18n"));
			m_bAppLoaded = QCoreApplication::installTranslator(&m_appTranslator);
		}
	} else if (m_bAppLoaded) {
		QCoreApplication::removeTranslator(&m_appTranslator);
		m_bAppLoaded = false;
	}

	// Qt's own catalog so standard dialogs follow the selected language.
	const QString strCatalog = QLatin1String("qtbase_") + strEffective;
	const QString strQtDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
	if (m_bQtLoaded) {
		QCoreApplication::removeTranslator(&m_qtTranslator);
		m_bQtLoaded = false;
	}
	if (!strQtDir.isEmpty()) {
		m_qtTranslator.load(strCatalog, strQtDir);
		m_bQtLoaded = QCoreApplication::installTranslator(&m_qtTranslator);
	}

	const bool bChanged = strEffective != m_strEffective;
	m_strEffective = strEffective;
	if (bChanged) {
		// Refresh every registered cached string before notifying QML, so
		// property reads see the new language immediately.
		RetranslateAll();
		emit languageChanged();
	}
}

}
