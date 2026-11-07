#include "app/MainWindow.h"

#include "commands/NodeCommands.h"
#include "document/ProjectDocument.h"
#include "graphics/NodeItem.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <tuple>

namespace lingya
{
namespace
{
bool sameStyle(const NodeStyleOverride &a, const NodeStyleOverride &b)
{
    return std::tie(a.fill, a.color, a.borderColor, a.fontFamily, a.fontSize, a.bold, a.shape,
                    a.borderWidth) == std::tie(b.fill, b.color, b.borderColor, b.fontFamily,
                                               b.fontSize, b.bold, b.shape, b.borderWidth);
}
QWidget *formContainer(QVBoxLayout *layout, const char *name)
{
    auto *widget = new QWidget;
    widget->setObjectName(QString::fromLatin1(name));
    layout->insertWidget(0, widget);
    return widget;
}

QLabel *errorLabel(QWidget *parent, const char *name)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QString::fromLatin1(name));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setProperty("validationError", true);
    return label;
}

void selectOptional(QComboBox *combo, const std::optional<QString> &value)
{
    if (!value)
        combo->setCurrentIndex(0);
    else
    {
        int index = combo->findData(*value);
        if (index < 0)
        {
            combo->addItem(*value, *value);
            index = combo->count() - 1;
        }
        combo->setCurrentIndex(index);
    }
}

std::optional<QString> optionalValue(QComboBox *combo)
{
    if (combo->isEditable() && combo->currentText() != combo->itemText(combo->currentIndex()))
        return combo->currentText().trimmed();
    if (combo->currentIndex() == 0)
        return std::nullopt;
    if (combo->currentIndex() >= 0)
        return combo->currentData().toString();
    return combo->currentText().trimmed();
}
} // namespace

void MainWindow::buildInlineStyle(QVBoxLayout *layout)
{
    auto *box = formContainer(layout, "inlineStyleControls");
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 8);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *shape = new QComboBox(box);
    shape->setObjectName("inlineShape");
    shape->addItem(QStringLiteral("继承工作表"));
    const QStringList codes{"roundRect", "rect", "ellipse", "diamond", "parallelogram"};
    const QStringList names{QStringLiteral("圆角矩形"), QStringLiteral("矩形"),
                            QStringLiteral("椭圆"), QStringLiteral("菱形"),
                            QStringLiteral("平行四边形")};
    for (int i = 0; i < codes.size(); ++i)
        shape->addItem(names.at(i), codes.at(i));
    form->addRow(QStringLiteral("形状"), shape);
    const auto color = [box, form](const char *name, const QString &label)
    {
        auto *combo = new QComboBox(box);
        combo->setObjectName(QString::fromLatin1(name));
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->setMinimumWidth(0);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo->addItem(QStringLiteral("继承工作表"));
        for (const QString value :
             {"#FFFFFF", "#DDF2E7", "#247A62", "#FFF0D0", "#EEE8FA", "#243A34"})
        {
            QPixmap swatch(18, 18);
            swatch.fill(QColor(value));
            combo->addItem(QIcon(swatch), value, value);
        }
        form->addRow(label, combo);
        return combo;
    };
    auto *fill = color("inlineFill", QStringLiteral("填充"));
    color("inlineTextColor", QStringLiteral("文字"));
    color("inlineBorderColor", QStringLiteral("边框"));
    auto *palette = new QHBoxLayout;
    palette->setSpacing(5);
    for (int i = 1; i < fill->count(); ++i)
    {
        auto *swatch = new QToolButton(box);
        swatch->setFixedSize(30, 30);
        swatch->setIcon(fill->itemIcon(i));
        swatch->setIconSize(QSize(18, 18));
        swatch->setToolTip(
            QStringLiteral("填充色 %1；点击应用后生效").arg(fill->itemData(i).toString()));
        connect(swatch, &QToolButton::clicked, this, [fill, i] { fill->setCurrentIndex(i); });
        palette->addWidget(swatch);
    }
    column->addLayout(palette);
    auto *family = new QLineEdit(box);
    family->setObjectName("inlineFontFamily");
    family->setPlaceholderText(QStringLiteral("系统字体"));
    form->addRow(QStringLiteral("字体"), family);
    auto *inherit = new QCheckBox(QStringLiteral("使用工作表字体"), box);
    inherit->setObjectName("inlineInheritFont");
    connect(inherit, &QCheckBox::toggled, family,
            [family](bool value) { family->setEnabled(!value); });
    form->addRow(QString(), inherit);
    auto *size = new QComboBox(box);
    size->setObjectName("inlineFontSize");
    size->addItem(QStringLiteral("继承工作表"));
    for (int value = 12; value <= 32; ++value)
        size->addItem(QString::number(value), value);
    form->addRow(QStringLiteral("字号"), size);
    auto *bold = new QCheckBox(QStringLiteral("加粗"), box);
    bold->setObjectName("inlineBold");
    bold->setTristate(true);
    form->addRow(QStringLiteral("字重"), bold);
    auto *width = new QDoubleSpinBox(box);
    width->setObjectName("inlineBorderWidth");
    width->setRange(0, 20);
    width->setDecimals(2);
    width->setSingleStep(0.1);
    width->setSpecialValueText(QStringLiteral("继承工作表"));
    form->addRow(QStringLiteral("边宽"), width);
    column->addLayout(form);
    column->addWidget(errorLabel(box, "inlineStyleError"));
    auto *apply = new QPushButton(QStringLiteral("应用样式"), box);
    apply->setObjectName("applyInlineStyle");
    apply->setProperty("primary", true);
    connect(apply, &QPushButton::clicked, this, [this] { applyInlineStyle(); });
    column->addWidget(apply);
    auto *reset = new QPushButton(QStringLiteral("恢复工作表样式"), box);
    reset->setObjectName("resetInlineStyle");
    connect(reset, &QPushButton::clicked, this, [this] { applyInlineStyle(true); });
    column->addWidget(reset);
}

void MainWindow::buildInlineIcons(QVBoxLayout *layout)
{
    auto *box = formContainer(layout, "inlineIconControls");
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 8);
    column->addWidget(new QLabel(QStringLiteral("主题图标"), box));
    auto *group = new QButtonGroup(box);
    group->setObjectName("inlineIconGroup");
    group->setExclusive(true);
    const QStringList ids{QString(), "idea", "star", "check"};
    const QStringList names{QStringLiteral("无图标"), QStringLiteral("灵感"),
                            QStringLiteral("重点"), QStringLiteral("完成")};
    for (int i = 0; i < ids.size(); ++i)
    {
        auto *button = new QToolButton(box);
        button->setObjectName(QStringLiteral("inlineIcon_") +
                              (ids.at(i).isEmpty() ? QStringLiteral("none") : ids.at(i)));
        button->setText(names.at(i));
        button->setIcon(NodeItem::iconForId(ids.at(i)));
        button->setIconSize(QSize(28, 28));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setCheckable(true);
        group->addButton(button);
        column->addWidget(button);
        connect(button, &QToolButton::clicked, this,
                [this, id = ids.at(i)] { applyInlineIcon(id); });
    }
    column->addWidget(errorLabel(box, "inlineIconError"));
}

void MainWindow::buildInlineNotes(QVBoxLayout *layout)
{
    auto *box = formContainer(layout, "inlineNoteControls");
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 8);
    column->addWidget(new QLabel(QStringLiteral("备注"), box));
    auto *note = new QPlainTextEdit(box);
    note->setObjectName("inlineNote");
    note->setPlaceholderText(QStringLiteral("为主题补充说明…"));
    note->setMinimumHeight(120);
    note->setMaximumHeight(220);
    column->addWidget(note);
    column->addWidget(new QLabel(QStringLiteral("链接"), box));
    auto *link = new QLineEdit(box);
    link->setObjectName("inlineLink");
    link->setPlaceholderText(QStringLiteral("https://…"));
    column->addWidget(link);
    column->addWidget(errorLabel(box, "inlineNoteError"));
    auto *apply = new QPushButton(QStringLiteral("应用备注与链接"), box);
    apply->setObjectName("applyInlineNotes");
    apply->setProperty("primary", true);
    connect(apply, &QPushButton::clicked, this, &MainWindow::applyInlineNotes);
    column->addWidget(apply);
}

void MainWindow::updateInlineProperties()
{
    const auto *tree = activeTreeSheet();
    const bool valid = tree && tree->nodes.contains(m_selectedNodeId) && !m_homeVisible &&
                       m_centralStack->currentIndex() == 0;
    for (const char *name : {"inlineStyleControls", "inlineIconControls", "inlineNoteControls"})
        if (auto *box = findChild<QWidget *>(name))
            box->setEnabled(valid);
    if (!valid || !findChild<QComboBox *>("inlineShape"))
        return;
    if (m_propertySheetId == tree->id && m_propertyNodeId == m_selectedNodeId &&
        m_propertyRevision == m_doc->sessionRevision())
        return;
    const BaseNode incoming = tree->nodes.value(m_selectedNodeId);
    const bool sameNode = m_propertySheetId == tree->id && m_propertyNodeId == m_selectedNodeId;
    const bool keepStyle = sameNode && sameStyle(incoming.style, m_propertyInitial.style);
    const bool keepNotes = sameNode && incoming.note == m_propertyInitial.note &&
                           incoming.link == m_propertyInitial.link;
    m_propertySheetId = tree->id;
    m_propertyNodeId = m_selectedNodeId;
    m_propertyRevision = m_doc->sessionRevision();
    m_propertyInitial = incoming;
    m_propertyStyle = m_propertyInitial.style;
    if (!keepStyle)
    {
        selectOptional(findChild<QComboBox *>("inlineShape"), m_propertyStyle.shape);
        selectOptional(findChild<QComboBox *>("inlineFill"), m_propertyStyle.fill);
        selectOptional(findChild<QComboBox *>("inlineTextColor"), m_propertyStyle.color);
        selectOptional(findChild<QComboBox *>("inlineBorderColor"), m_propertyStyle.borderColor);
        auto *family = findChild<QLineEdit *>("inlineFontFamily");
        family->setText(m_propertyStyle.fontFamily.value_or(QString()));
        family->setProperty("initialText", family->text());
        findChild<QCheckBox *>("inlineInheritFont")
            ->setChecked(!m_propertyStyle.fontFamily.has_value());
        family->setEnabled(m_propertyStyle.fontFamily.has_value());
        auto *size = findChild<QComboBox *>("inlineFontSize");
        size->setCurrentIndex(m_propertyStyle.fontSize ? size->findData(*m_propertyStyle.fontSize)
                                                       : 0);
        findChild<QCheckBox *>("inlineBold")
            ->setCheckState(!m_propertyStyle.bold   ? Qt::PartiallyChecked
                            : *m_propertyStyle.bold ? Qt::Checked
                                                    : Qt::Unchecked);
        auto *width = findChild<QDoubleSpinBox *>("inlineBorderWidth");
        width->setValue(m_propertyStyle.borderWidth.value_or(0));
        width->setProperty("initialValue", width->value());
        findChild<QLabel *>("inlineStyleError")->clear();
    }
    if (!keepNotes)
    {
        findChild<QPlainTextEdit *>("inlineNote")->setPlainText(m_propertyInitial.note);
        findChild<QLineEdit *>("inlineLink")->setText(m_propertyInitial.link.value_or(QString()));
        findChild<QLabel *>("inlineNoteError")->clear();
    }
    const QString icon = m_propertyInitial.iconId.value_or(QString());
    auto *group = findChild<QButtonGroup *>("inlineIconGroup");
    group->setExclusive(false);
    for (const QString id :
         {QString(), QStringLiteral("idea"), QStringLiteral("star"), QStringLiteral("check")})
        if (auto *button = findChild<QToolButton *>(QStringLiteral("inlineIcon_") +
                                                    (id.isEmpty() ? QStringLiteral("none") : id)))
        {
            const QSignalBlocker blocker(button);
            button->setChecked(icon == id);
        }
    group->setExclusive(true);
    findChild<QLabel *>("inlineIconError")->clear();
}

void MainWindow::applyInlineStyle(bool reset)
{
    if (m_homeVisible || m_propertySheetId != m_doc->project().activeSheetId ||
        m_propertyRevision != m_doc->sessionRevision() || m_propertyNodeId != m_selectedNodeId)
    {
        updateInlineProperties();
        return;
    }
    NodeStyleOverride style;
    if (!reset)
    {
        style = m_propertyStyle;
        style.shape = optionalValue(findChild<QComboBox *>("inlineShape"));
        style.fill = optionalValue(findChild<QComboBox *>("inlineFill"));
        style.color = optionalValue(findChild<QComboBox *>("inlineTextColor"));
        style.borderColor = optionalValue(findChild<QComboBox *>("inlineBorderColor"));
        auto *family = findChild<QLineEdit *>("inlineFontFamily");
        if (findChild<QCheckBox *>("inlineInheritFont")->isChecked())
            style.fontFamily.reset();
        else if (family->text() != family->property("initialText").toString())
            style.fontFamily = family->text().trimmed();
        auto *size = findChild<QComboBox *>("inlineFontSize");
        style.fontSize = size->currentIndex() == 0
                             ? std::nullopt
                             : std::optional<int>(size->currentData().toInt());
        const auto bold = findChild<QCheckBox *>("inlineBold")->checkState();
        style.bold =
            bold == Qt::PartiallyChecked ? std::nullopt : std::optional<bool>(bold == Qt::Checked);
        auto *width = findChild<QDoubleSpinBox *>("inlineBorderWidth");
        if (width->value() != width->property("initialValue").toDouble())
            style.borderWidth =
                width->value() == 0 ? std::nullopt : std::optional<double>(width->value());
    }
    const auto result =
        NodeCommands(m_commands, m_doc).setStyle(m_propertySheetId, m_propertyNodeId, style);
    if (const auto *error = std::get_if<AppError>(&result))
        findChild<QLabel *>("inlineStyleError")->setText(error->message);
    else
        findChild<QLabel *>("inlineStyleError")->clear();
}

void MainWindow::applyInlineIcon(const QString &id)
{
    if (m_homeVisible || m_propertySheetId != m_doc->project().activeSheetId ||
        m_propertyRevision != m_doc->sessionRevision() || m_propertyNodeId != m_selectedNodeId)
    {
        updateInlineProperties();
        return;
    }
    NodeMetaUpdate update;
    update.iconId = id;
    const auto result =
        NodeCommands(m_commands, m_doc).updateNodeMeta(m_propertySheetId, m_propertyNodeId, update);
    if (const auto *error = std::get_if<AppError>(&result))
        findChild<QLabel *>("inlineIconError")->setText(error->message);
    else
        findChild<QLabel *>("inlineIconError")->clear();
}

void MainWindow::applyInlineNotes()
{
    if (m_homeVisible || m_propertySheetId != m_doc->project().activeSheetId ||
        m_propertyRevision != m_doc->sessionRevision() || m_propertyNodeId != m_selectedNodeId)
    {
        updateInlineProperties();
        return;
    }
    NodeMetaUpdate update;
    const QString note = findChild<QPlainTextEdit *>("inlineNote")->toPlainText();
    const QString link = findChild<QLineEdit *>("inlineLink")->text();
    if (note != m_propertyInitial.note)
        update.note = note;
    if (link != m_propertyInitial.link.value_or(QString()))
        update.link = link;
    const auto result =
        NodeCommands(m_commands, m_doc).updateNodeMeta(m_propertySheetId, m_propertyNodeId, update);
    if (const auto *error = std::get_if<AppError>(&result))
        findChild<QLabel *>("inlineNoteError")->setText(error->message);
    else
        findChild<QLabel *>("inlineNoteError")->clear();
}
} // namespace lingya
