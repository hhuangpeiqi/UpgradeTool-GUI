#include "gui/backend/ThemeManager.h"

#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>

namespace gui {

namespace {

const char *kSettingsKey = "ui/theme";
const char *kAutoMode = "auto";

bool SystemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	QStyleHints *pHints = QGuiApplication::styleHints();
	return pHints && pHints->colorScheme() == Qt::ColorScheme::Dark;
#else
	return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
}

}

ThemeManager::ThemeManager(QObject *parent)
	: QObject(parent),
	  m_bDark(false)
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope,
	                   "upgrade_tool", "upgrade_tool");
	m_strMode = NormalizeMode(settings.value(QString::fromLatin1(kSettingsKey),
	                                         QString::fromLatin1(kAutoMode)).toString());

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	QStyleHints *pHints = QGuiApplication::styleHints();
	if (pHints)
		connect(pHints, &QStyleHints::colorSchemeChanged,
		        this, &ThemeManager::onSystemSchemeChanged);
#endif

	Recompute();
}

QString ThemeManager::NormalizeMode(const QString &strMode)
{
	QString str = strMode.trimmed().toLower();
	if (str == QLatin1String("light") || str == QLatin1String("dark"))
		return str;
	return QString::fromLatin1(kAutoMode);
}

void ThemeManager::setMode(const QString &strMode)
{
	QString str = NormalizeMode(strMode);
	if (str == m_strMode)
		return;

	m_strMode = str;
	QSettings settings(QSettings::IniFormat, QSettings::UserScope,
	                   "upgrade_tool", "upgrade_tool");
	settings.setValue(QString::fromLatin1(kSettingsKey), m_strMode);
	emit modeChanged();
	Recompute();
}

void ThemeManager::onSystemSchemeChanged()
{
	if (m_strMode == QLatin1String(kAutoMode))
		Recompute();
}

void ThemeManager::Recompute()
{
	bool bDark;
	if (m_strMode == QLatin1String("dark")) {
		bDark = true;
	} else if (m_strMode == QLatin1String("light")) {
		bDark = false;
	} else {
		bDark = SystemPrefersDark();
	}

	if (bDark == m_bDark)
		return;
	m_bDark = bDark;
	emit darkChanged();
}

}
