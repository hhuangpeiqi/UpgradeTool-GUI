#include "tests/support/Checks.h"
#include "tests/support/qt/QmlHelpers.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickStyle>
#include <QColor>
#include <QKeyEvent>
#include <QtTest/QTest>
#include <memory>

static int failures = 0;
// Exercise the shared controls without submitting device operations.
static void TestFocusBorders(QQmlEngine &engine) {
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Controls
import UpgradeTool
Window {
    width: 520; height: 520; visible: true
    property color focusBorder: Theme.primary
    property color normalBorder: Theme.border
    property int activations: 0
    Column {
        x: 20; y: 20; spacing: 8
        AppTextField { objectName: "focusStart"; width: 250 }
        PrimaryButton { objectName: "focusPrimary"; focusPolicy: Qt.StrongFocus; text: "Primary"; onClicked: ++activations }
        SecondaryButton { objectName: "focusSecondary"; focusPolicy: Qt.StrongFocus; text: "Secondary"; onClicked: ++activations }
        AppCheckBox { objectName: "focusCheck"; focusPolicy: Qt.StrongFocus; text: "Check" }
        AppComboBox { objectName: "focusCombo"; model: ["First", "Second"] }
        DeviceComboBox { objectName: "focusDevice"; model: ["Device 1", "Device 2"] }
        SegmentedControl { objectName: "focusSegments"; model: ["First", "Second"] }
        DisclosureSection { objectName: "focusDisclosure"; width: 250; title: "Advanced" }
        AppTextField { objectName: "focusInput"; width: 250 }
    }
}
)QML", QUrl());
    std::unique_ptr<QObject> instance(component.create());
    CHECK(instance);
    if (!instance) {
        std::fprintf(stderr, "%s\n", component.errorString().toUtf8().constData());
        return;
    }
    auto *window = qobject_cast<QQuickWindow *>(instance.get());
    CHECK(window);
    if (!window) return;
    window->requestActivate();
    CHECK(AwaitUi([&] { return window->isActive() && !window->grabWindow().isNull(); }));
    auto find = [&](const char *name) { return window->findChild<QQuickItem *>(name); };
    auto *disclosure = find("focusDisclosure");
    QQuickItem *header = nullptr;
    for (auto *child : disclosure->childItems())
        if (child->property("visualFocus").isValid()) header = child;
    CHECK(header);
    if (!header) return;
    const QList<QQuickItem *> controls{find("focusPrimary"), find("focusSecondary"), find("focusCheck"),
        find("focusCombo"), find("focusDevice"), find("focusSegments"), header};
    auto border = [](QQuickItem *item, const char *property) {
        return QQmlProperty(item->property("background").value<QObject *>(), QString::fromLatin1(property)).read();
    };
    auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent press(QEvent::KeyPress, code, modifiers, code == Qt::Key_A ? "a" : ""), release(QEvent::KeyRelease, code, modifiers);
        QCoreApplication::sendEvent(window, &press);
        QCoreApplication::sendEvent(window, &release);
        QCoreApplication::processEvents();
    };
    auto click = [&](QQuickItem *item) {
        const QPointF point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
        auto *popup = item->property("popup").value<QObject *>();
        if (popup) {
            CHECK(popup->property("visible").toBool());
            CHECK(QMetaObject::invokeMethod(popup, "close"));
        }
        QTest::mouseMove(window, QPoint(500, 500));
        QCoreApplication::processEvents();
    };
    // Real Tab traversal keeps all existing keyboard entry points reachable.
    find("focusStart")->forceActiveFocus(Qt::TabFocusReason);
    for (auto *control : controls) {
        CHECK(control);
        if (!control) return;
        key(Qt::Key_Tab);
        CHECK(control->hasActiveFocus());
        CHECK(control->property("visualFocus").toBool());
    }
    key(Qt::Key_Tab);
    CHECK(find("focusInput")->hasActiveFocus());
    key(Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK(header->hasActiveFocus() && header->property("visualFocus").toBool());
    for (int i = 0; i < controls.size(); ++i) {
        auto *control = controls[i];
        control->forceActiveFocus(Qt::TabFocusReason);
        CHECK(control->property("visualFocus").toBool());
        if (i < 2) CHECK(border(control, "border.width").toInt() == 2);
        else CHECK(border(control, "border.color").value<QColor>() == window->property("focusBorder").value<QColor>());
        click(control);
        CHECK(control->hasActiveFocus());
        if (control->property("visualFocus").toBool()) std::fprintf(stderr, "Mouse focus still visible: %s\n", control->objectName().toUtf8().constData());
        CHECK(!control->property("visualFocus").toBool());
        if (i < 3 || control == header)
            CHECK(border(control, "border.width").toInt() == (i == 1 ? 1 : 0));
        else
            CHECK(border(control, "border.color").value<QColor>() == window->property("normalBorder").value<QColor>());
        key(Qt::Key_Tab);
        key(Qt::Key_Backtab, Qt::ShiftModifier);
        CHECK(control->hasActiveFocus() && control->property("visualFocus").toBool());
    }
    CHECK(window->property("activations").toInt() == 2);
    auto *input = find("focusInput");
    click(input);
    CHECK(input->hasActiveFocus());
    CHECK(border(input, "border.color").value<QColor>() == window->property("focusBorder").value<QColor>());
    key(Qt::Key_A);
    CHECK(input->property("text").toString() == "a");
    controls[0]->forceActiveFocus(Qt::TabFocusReason);
    const int activations = window->property("activations").toInt();
    key(Qt::Key_Space);
    CHECK(window->property("activations").toInt() == activations + 1);
    controls[2]->forceActiveFocus(Qt::TabFocusReason);
    const bool checked = controls[2]->property("checked").toBool();
    key(Qt::Key_Space);
    CHECK(controls[2]->property("checked").toBool() != checked);
    controls[3]->forceActiveFocus(Qt::TabFocusReason);
    key(Qt::Key_Down);
    CHECK(controls[3]->property("currentIndex").toInt() == 1);
    controls[5]->forceActiveFocus(Qt::TabFocusReason);
    key(Qt::Key_Left);
    CHECK(controls[5]->property("currentIndex").toInt() == 0);
    key(Qt::Key_Right);
    CHECK(controls[5]->property("currentIndex").toInt() == 1);
    header->forceActiveFocus(Qt::TabFocusReason);
    const bool expanded = disclosure->property("expanded").toBool();
    key(Qt::Key_Return);
    CHECK(disclosure->property("expanded").toBool() != expanded);
}

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Basic");
    QQmlEngine engine;
    // Theme is a QML singleton; setting its mode exercises both palettes.
    QQmlComponent theme(&engine);
    theme.setData("import QtQuick; import UpgradeTool; QtObject { property bool dark: false; onDarkChanged: Theme.dark = dark }", QUrl());
    std::unique_ptr<QObject> palette(theme.create());
    CHECK(palette);
    if (!palette) return 1;
    for (bool dark : {false, true}) {
        palette->setProperty("dark", dark);
        TestFocusBorders(engine);
    }
    return failures ? 1 : 0;
}
