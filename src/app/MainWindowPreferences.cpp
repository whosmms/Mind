#include "app/MainWindow.h"
#include "services/AppearanceService.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QVariant>
namespace lingya
{
void MainWindow::applyUserShortcuts()
{
    const auto prefs = AppearanceService::read();
    for (auto *action : findChildren<QAction *>())
    {
        if (action->objectName().isEmpty())
            continue;
        if (!action->property("factoryShortcuts").isValid())
        {
            if (action->shortcuts().isEmpty())
                continue;
            QStringList defaults;
            for (const auto &sequence : action->shortcuts())
                defaults.append(sequence.toString(QKeySequence::PortableText));
            action->setProperty("factoryShortcuts", defaults);
        }
        QList<QKeySequence> sequences;
        if (prefs.shortcuts.contains(action->objectName()))
        {
            const auto value = prefs.shortcuts.value(action->objectName());
            if (!value.isEmpty())
                sequences.append(QKeySequence::fromString(value, QKeySequence::PortableText));
        }
        else
            for (const auto &value : action->property("factoryShortcuts").toStringList())
                sequences.append(QKeySequence::fromString(value, QKeySequence::PortableText));
        action->setShortcuts(sequences);
    }
}
void MainWindow::showPreferences()
{
    const auto original = AppearanceService::read();
    QDialog dialog(this);
    dialog.setObjectName("preferencesDialog");
    dialog.setWindowTitle(QStringLiteral("设置与帮助"));
    dialog.resize(qMin(780, screen()->availableGeometry().width() - 40),
                  qMin(640, screen()->availableGeometry().height() - 60));
    auto *column = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    tabs->setObjectName("preferenceTabs");
    column->addWidget(tabs, 1);
    auto *appearance = new QWidget(tabs);
    auto *form = new QFormLayout(appearance);
    auto *theme = new QComboBox(appearance);
    theme->setObjectName("preferenceTheme");
    theme->addItem(QStringLiteral("春芽绿"), "mint");
    theme->addItem(QStringLiteral("浅紫"), "lavender");
    theme->addItem(QStringLiteral("高对比界面"), "projection");
    theme->setCurrentIndex(theme->findData(original.theme));
    form->addRow(QStringLiteral("界面配色"), theme);
    auto *explain = new QLabel(
        QStringLiteral("界面配色与导图样式可分别设置；这些偏好只保存在本机。"), appearance);
    explain->setWordWrap(true);
    form->addRow(explain);
    tabs->addTab(appearance, QStringLiteral("外观"));
    auto *keys = new QTableWidget(tabs);
    keys->setObjectName("preferenceShortcuts");
    keys->setColumnCount(2);
    keys->setHorizontalHeaderLabels({QStringLiteral("动作"), QStringLiteral("快捷键")});
    keys->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    keys->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    keys->verticalHeader()->hide();
    tabs->addTab(keys, QStringLiteral("快捷键"));
    QVector<QAction *> actions;
    QVector<QKeySequenceEdit *> editors;
    for (auto *action : findChildren<QAction *>())
    {
        if (!action->property("factoryShortcuts").isValid())
            continue;
        const int row = keys->rowCount();
        keys->insertRow(row);
        auto *label = new QTableWidgetItem(action->text().remove('&'));
        label->setFlags(label->flags() & ~Qt::ItemIsEditable);
        keys->setItem(row, 0, label);
        auto *editor = new QKeySequenceEdit(action->shortcut(), keys);
        editor->setMaximumSequenceLength(1);
        editor->setObjectName("shortcut_" + action->objectName());
        editor->setProperty("initialSequence",
                            action->shortcut().toString(QKeySequence::PortableText));
        keys->setCellWidget(row, 1, editor);
        keys->setRowHeight(row, 42);
        actions.append(action);
        editors.append(editor);
    }
    auto *help = new QTextBrowser(tabs);
    help->setOpenExternalLinks(false);
    help->setMarkdown(QStringLiteral(
        "# 灵芽思维\n\n让思考更清晰，让想法自然生长。\n\n"
        "## 快速开始\n\n从首页选择导图、鱼骨、流程或甘特模板，或打开本地工程。编辑完成后保存为 "
        "`.lingya`。\n\n"
        "## 编辑与查找\n\n默认 F2 改名，Tab 新增子主题，Enter 新增同级，Delete 删除，Ctrl+Z "
        "撤销。大纲与画布同步；Ctrl+F 搜索标题、备注和任务。快捷键可在本页修改。\n\n"
        "## 输出与讲解\n\n通过工具菜单导出 PNG、分页 PDF、Markdown 或 "
        "TXT；图片导出当前可见内容，大纲包含折叠主题。F5 "
        "编辑讲解路线并播放，左右键切步，空格暂停计时，Esc 返回。\n\n"
        "## AI "
        "与文字稿\n\n进入编辑器后打开“AI创作与密钥设置”。生成前检查来源范围和附加要求，先预览再采用"
        "。密钥默认只用于本次会话，也可存到 Windows "
        "凭据管理器。生成和连接测试会发送所选材料；退出窗口取消在途请求。稿件可逐段编辑，来源变化只"
        "提示，不自动重写。\n\n"
        "## "
        "复习、检查与恢复\n\n视图页可打开复习与规则检查；任务共享同一工程记录。自动恢复副本与正式保"
        "存分开，恢复采用可撤销。重要编辑仍应主动保存。\n\n"
        "这是课程开发候选，最终统一验收另行记录。"));
    tabs->addTab(help, QStringLiteral("使用帮助"));
    auto *info = new QLabel(&dialog);
    info->setObjectName("preferenceError");
    info->setTextFormat(Qt::PlainText);
    info->setWordWrap(true);
    column->addWidget(info);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::RestoreDefaults,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存设置"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("恢复默认"));
    column->addWidget(buttons);
    bool reset = false;
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog,
            [&]
            {
                reset = true;
                theme->setCurrentIndex(0);
                for (int i = 0; i < actions.size(); ++i)
                    editors[i]->setKeySequence(QKeySequence::fromString(
                        actions[i]->property("factoryShortcuts").toStringList().value(0),
                        QKeySequence::PortableText));
            });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(
        buttons, &QDialogButtonBox::accepted, &dialog,
        [&]
        {
            auto prefs = reset ? AppearanceService::Preferences{} : original;
            prefs.theme = theme->currentData().toString();
            for (int i = 0; i < actions.size(); ++i)
            {
                const QString name = actions[i]->objectName();
                const QString value =
                    editors[i]->keySequence().toString(QKeySequence::PortableText);
                const QString initial = editors[i]->property("initialSequence").toString();
                if (reset || value != initial)
                {
                    if (value == actions[i]->property("factoryShortcuts").toStringList().value(0))
                        prefs.shortcuts.remove(name);
                    else
                        prefs.shortcuts.insert(name, value);
                }
            }
            QMap<QString, QString> owners;
            for (auto *action : actions)
            {
                QStringList values = prefs.shortcuts.contains(action->objectName())
                                         ? QStringList{prefs.shortcuts.value(action->objectName())}
                                         : action->property("factoryShortcuts").toStringList();
                for (const auto &value : values)
                {
                    if (value.isEmpty())
                        continue;
                    const QString normalized =
                        QKeySequence::fromString(value, QKeySequence::PortableText)
                            .toString(QKeySequence::PortableText);
                    if (owners.contains(normalized) &&
                        owners.value(normalized) != action->objectName())
                    {
                        info->setText(QStringLiteral("快捷键冲突：") + normalized);
                        return;
                    }
                    owners.insert(normalized, action->objectName());
                }
            }
            const QString style = AppearanceService::styleSheet(prefs.theme);
            if (style.isEmpty())
            {
                info->setText(QStringLiteral("主题资源无法读取"));
                return;
            }
            const auto result = AppearanceService::write(prefs);
            if (const auto *error = std::get_if<AppError>(&result))
            {
                info->setText(error->message);
                return;
            }
            qApp->setStyleSheet(style);
            applyUserShortcuts();
            dialog.accept();
        });
    dialog.exec();
}
} // namespace lingya
