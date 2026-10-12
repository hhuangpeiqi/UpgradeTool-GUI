#ifndef GUI_RETRANSLATABLE_H
#define GUI_RETRANSLATABLE_H

#include <functional>
#include <utility>

#include <QString>
#include <QVariantList>

namespace gui {

// Anything that caches text produced by tr()/translate() implements this and
// registers with LanguageManager. A language switch then refreshes every
// cached string through one path instead of ad-hoc rebuilds per feature.
class Retranslatable
{
public:
	virtual ~Retranslatable() = default;
	virtual void retranslate() = 0;
};

// A cached value rebuilt from a factory. The factory captures the state needed
// to reproduce the message and calls tr()/translate() itself, so retranslate()
// simply re-runs it in the new language.
template <typename T>
class Translatable
{
public:
	using Factory = std::function<T()>;

	void Set(Factory factory)
	{
		m_factory = std::move(factory);
		m_value = m_factory ? m_factory() : T();
	}

	void Clear()
	{
		m_factory = Factory();
		m_value = T();
	}

	void retranslate()
	{
		if (m_factory)
			m_value = m_factory();
	}

	const T &value() const { return m_value; }
	bool bound() const { return static_cast<bool>(m_factory); }

private:
	Factory m_factory;
	T m_value{};
};

using TranslatableText = Translatable<QString>;
using TranslatableList = Translatable<QVariantList>;

}

#endif
