// H048鼠标交互：走真实Qt事件与命令，不把模型直接赋值冒充手势完成。
namespace
{
void captureInteraction(QWidget &widget, const QString &name)
{
    const auto dir = qEnvironmentVariable("LINGYA_INTERACTION_CAPTURE_DIR");
    if (dir.isEmpty())
        return;
    QVERIFY(QDir().mkpath(dir));
    if (auto *window = qobject_cast<MainWindow *>(&widget))
    {
        window->resize(1536, 900);
        QApplication::processEvents();
        if (auto *view = window->findChild<QGraphicsView *>("topicPreview");
            view && view->isVisible())
            window->findChild<QAction *>("fitTopic")->trigger();
    }
    QVERIFY(widget.grab().save(dir + "/" + name + ".png"));
}
void answerImageFile(const QString &path, bool accept = true)
{
    auto *timer = new QTimer(qApp);
    timer->setInterval(1);
    QObject::connect(
        timer, &QTimer::timeout, timer,
        [timer, path, accept]
        {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            timer->stop();
            if (accept)
            {
                dialog->selectFile(path);
                QVERIFY(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
            }
            else
                dialog->reject();
            timer->deleteLater();
        });
    timer->start();
    QTimer::singleShot(3000, timer, &QObject::deleteLater);
}
void mouseDrag(QWidget *viewport, QPoint from, QPoint to,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool release = true)
{
    QTest::mousePress(viewport, Qt::LeftButton, modifiers, from);
    QMouseEvent move(QEvent::MouseMove, to, viewport->mapToGlobal(to), Qt::NoButton, Qt::LeftButton,
                     modifiers);
    QApplication::sendEvent(viewport, &move);
    if (release)
        QTest::mouseRelease(viewport, Qt::LeftButton, modifiers, to);
}
void contextAction(MainWindow &window, QGraphicsView *view, QPoint at, const QString &name,
                   const std::function<void()> &beforeTrigger = {})
{
    QTimer::singleShot(0, &window,
                       [&window, name, beforeTrigger]
                       {
                           auto *menu = window.findChild<QMenu *>("canvasContextMenu");
                           QVERIFY(menu);
                           const auto close = qScopeGuard([menu] { menu->close(); });
                           QAction *chosen = nullptr;
                           for (auto *action : menu->actions())
                               if (action->objectName() == name)
                                   chosen = action;
                           QVERIFY(chosen);
                           if (beforeTrigger)
                               beforeTrigger();
                           chosen->trigger();
                           menu->close();
                       });
    QContextMenuEvent event(QContextMenuEvent::Mouse, at, view->viewport()->mapToGlobal(at));
    QApplication::sendEvent(view->viewport(), &event);
}
QString tinyImage()
{
    QImage image(120, 70, QImage::Format_ARGB32);
    image.fill(QColor("#4A9E83"));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}
QGraphicsItem *flowPort(QGraphicsView *view, const ID &id, const QString &port)
{
    for (auto *value : view->scene()->items())
        if (auto *n = dynamic_cast<NodeItem *>(value); n && n->nodeId() == id)
            for (auto *child : n->childItems())
                if (child->data(0x21C0).toString() == port)
                    return child;
    return nullptr;
}
QPoint portPoint(QGraphicsView *view, const ID &id, const QString &port)
{
    if (const auto *item = flowPort(view, id, port))
        return view->mapFromScene(item->sceneBoundingRect().center());
    return {};
}
const QGraphicsPathItem *flowLine(QGraphicsView *view, const ID &id)
{
    for (auto *item : view->scene()->items())
        if (!item->parentItem() && item->data(0x21C1).toString() == id)
            if (const auto *line = dynamic_cast<QGraphicsPathItem *>(item))
                return line;
    return nullptr;
}
void captureEfficiency(QWidget &widget, const QString &name,
                       const char *environment = "LINGYA_H052_CAPTURE_DIR")
{
    const auto directory = qEnvironmentVariable(environment);
    if (directory.isEmpty())
        return;
    QVERIFY(QDir().mkpath(directory));
    QApplication::processEvents();
    QVERIFY(widget.grab().save(QDir(directory).filePath(name + ".png")));
}
NodeItem *visibleTopic(QGraphicsView *view, const ID &id)
{
    for (auto *item : view->scene()->items())
        if (auto *node = dynamic_cast<NodeItem *>(item); node && node->nodeId() == id)
            return node;
    return nullptr;
}
bool dropImage(QGraphicsView *view, const QMimeData &mime, const QPoint &position)
{
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &enter);
    if (!enter.isAccepted())
        return false;
    QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &drop);
    return drop.isAccepted() && drop.dropAction() == Qt::CopyAction;
}
QGraphicsItem *flowHandle(QGraphicsView *view, const QString &kind)
{
    for (auto *item : view->scene()->items())
        if (item->data(0x21C3).toString() == kind)
            return item;
    return nullptr;
}
QMap<ID, QTreeWidgetItem *> reviewPickerRows(QDialog *picker)
{
    QMap<ID, QTreeWidgetItem *> rows;
    auto *tree = picker->findChild<QTreeWidget *>("reviewTopicTree");
    if (!tree)
        return rows;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
    {
        const auto id = (*it)->data(0, Qt::UserRole).toString();
        if (!id.isEmpty())
            rows.insert(id, *it);
    }
    return rows;
}
void answerChildDialog(QWidget &parent, const char *name,
                       const std::function<void(QDialog *)> &answer)
{
    QTimer::singleShot(0, &parent,
                       [&parent, name, answer]
                       {
                           auto *dialog = parent.findChild<QDialog *>(name);
                           QVERIFY(dialog && dialog->isVisible());
                           const auto close = qScopeGuard(
                               [dialog]
                               {
                                   if (dialog->isVisible())
                                       dialog->reject();
                               });
                           answer(dialog);
                       });
}
} // namespace

void TstUi::branchReviewLimitsQueueAndTopicPicker()
{
    MainWindow window;
    auto project = canvasProject(2, 2);
    auto &tree = std::get<TreeSheet>(project.sheets.front());
    const auto branch = canvasId("branch-0");
    const auto leaf = canvasId("leaf-0-0");
    const auto unmade = canvasId("leaf-0-1");
    const auto other = canvasId("branch-1");
    tree.nodes[leaf].note = QStringLiteral("已经掌握的答案");
    tree.nodes[unmade].note = QStringLiteral("来自备注的新答案");
    project.cards = {*ReviewService::createCard(project, tree.id, branch),
                     *ReviewService::createCard(project, tree.id, leaf),
                     *ReviewService::createCard(project, tree.id, other)};
    project.cards[1].status = "mastered";
    project.cards[1].lastReviewedAt = QDateTime::currentDateTimeUtc();
    loadEditorFixture(window, project);
    window.resize(1240, 760);
    window.show();
    QApplication::processEvents();
    window.findChild<QAction *>("fitTopic")->trigger();
    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    auto *node = visibleTopic(view, branch);
    QVERIFY(node);
    auto *doc = window.findChild<ProjectDocument *>();
    const auto revision = doc->sessionRevision();
    const auto pickTopic = [&](QDialog *picker)
    {
        const auto rows = reviewPickerRows(picker);
        QCOMPARE(rows.size(), 3);
        QVERIFY(rows.contains(branch) && rows.contains(leaf) && rows.contains(unmade));
        QCOMPARE(rows.value(leaf)->text(1), QStringLiteral("已加入"));
        rows.value(unmade)->setCheckState(0, Qt::Checked);
        picker->findChild<QPushButton *>("confirmReviewTopics")->click();
    };
    const auto checkReview = [&](QDialog *dialog)
    {
        auto *filter = dialog->findChild<QComboBox *>("reviewStatusFilter");
        auto *list = dialog->findChild<QListWidget *>("reviewCards");
        auto *page = dialog->findChild<ReviewPage *>();
        const auto *scope = dialog->findChild<QLabel *>("reviewScopeLabel");
        QVERIFY(scope && scope->text().contains(QStringLiteral("阶段1")));
        QCOMPARE(filter->currentData().toString(), QString("due"));
        QCOMPARE(list->count(), 1);
        QCOMPARE(page->totalCount(), 1);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), project.cards[0].id);
        captureEfficiency(*dialog, QStringLiteral("01-分支复习"), "LINGYA_H053_CAPTURE_DIR");
        answerChildDialog(*dialog, "pickReviewTopicsDialog", pickTopic);
        dialog->findChild<QPushButton *>("pickReviewTopics")->click();
        QCOMPARE(doc->sessionRevision(), revision + 1);
        QCOMPARE(list->count(), 2);
        QCOMPARE(page->totalCount(), 1);
        filter->setCurrentIndex(0);
        dialog->findChild<QPushButton *>("startReview")->click();
        QCOMPARE(list->count(), 3);
        QCOMPARE(page->totalCount(), 3);
    };
    contextAction(window, view, view->mapFromScene(node->scenePos()), "reviewBranch",
                  [&] { answerChildDialog(window, "reviewCardsDialog", checkReview); });
    QCOMPARE(doc->project().cards.size(), 4);
    QCOMPARE(doc->project().cards.back().nodeId, unmade);
    QCOMPARE(doc->project().cards.back().back, QStringLiteral("来自备注的新答案"));
    window.findChild<CommandService *>()->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), ProjectJson::toJson(project));
}

void TstUi::compareReviewSourceKeepsProgressAndFindsTopic()
{
    MainWindow window;
    auto project = canvasProject(1, 1);
    auto &tree = std::get<TreeSheet>(project.sheets.front());
    const auto leaf = canvasId("leaf-0-0");
    const auto sourceSheet = tree.id;
    tree.nodes[leaf].note = QStringLiteral("保存时的答案");
    ReviewCard card = *ReviewService::createCard(project, tree.id, leaf);
    card.status = "mastered";
    card.lastReviewedAt = QDateTime::currentDateTimeUtc();
    auto missing = card;
    missing.id = canvasId("missing-card");
    missing.nodeId = canvasId("missing-source");
    project.cards = {card, missing};
    tree.nodes[leaf].title = QStringLiteral("重新命名的主题");
    tree.nodes[leaf].note = QStringLiteral("修改后的答案");
    auto other = ProjectFactory::createMindmap(QStringLiteral("另一张表"));
    project.activeSheetId = other.activeSheetId;
    project.sheets.append(other.sheets.front());
    loadEditorFixture(window, project);
    window.show();
    auto *doc = window.findChild<ProjectDocument *>();
    const auto revision = doc->sessionRevision();
    runEditor(
        window, "showReviewCards", "reviewCardsDialog",
        [&](QDialog *dialog)
        {
            auto *list = dialog->findChild<QListWidget *>("reviewCards");
            auto *compare = dialog->findChild<QPushButton *>("compareReviewSource");
            auto *locate = dialog->findChild<QPushButton *>("locateReviewSource");
            list->setCurrentRow(1);
            QVERIFY(!compare->isEnabled() && !locate->isEnabled());
            list->setCurrentRow(0);
            QVERIFY(compare->isEnabled() && locate->isEnabled());
            const auto showComparison = [&](bool apply)
            {
                answerChildDialog(
                    *dialog, "reviewSourceComparison",
                    [apply](QDialog *preview)
                    {
                        auto *saved = preview->findChild<QPlainTextEdit *>("savedReviewContent");
                        auto *latest = preview->findChild<QPlainTextEdit *>("latestReviewContent");
                        QVERIFY(saved && latest && saved->isReadOnly() && latest->isReadOnly());
                        QVERIFY(saved->toPlainText().contains(QStringLiteral("保存时的答案")));
                        QVERIFY(latest->toPlainText().contains(QStringLiteral("修改后的答案")));
                        if (apply)
                            preview->findChild<QPushButton *>("adoptReviewSource")->click();
                        else
                            captureEfficiency(*preview, QStringLiteral("02-卡片来源对照"),
                                              "LINGYA_H053_CAPTURE_DIR");
                    });
                compare->click();
            };
            showComparison(false);
            QCOMPARE(doc->sessionRevision(), revision);
            QCOMPARE(doc->project().cards.front().back, card.back);
            showComparison(true);
            QCOMPARE(doc->sessionRevision(), revision + 1);
            const auto updated = doc->project().cards.front();
            QCOMPARE(updated.id, card.id);
            QCOMPARE(updated.front, QStringLiteral("重新命名的主题"));
            QCOMPARE(updated.back, QStringLiteral("修改后的答案"));
            QCOMPARE(updated.status, card.status);
            QCOMPARE(updated.lastReviewedAt, card.lastReviewedAt);
            locate->click();
        });
    QCOMPARE(doc->project().activeSheetId, sourceSheet);
    auto *outline = window.findChild<QTreeWidget *>("topicOutline");
    QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(), leaf);
    QCOMPARE(doc->sessionRevision(), revision + 1);
    window.findChild<CommandService *>()->undo();
    QCOMPARE(doc->project().cards.front().back, card.back);
}

void TstUi::branchDraftPreviewAndCardsFromSources()
{
    MainWindow window;
    auto project = canvasProject(2, 2);
    auto &tree = std::get<TreeSheet>(project.sheets.front());
    const auto branch = canvasId("branch-0");
    const auto leaf = canvasId("leaf-0-0");
    const auto empty = canvasId("leaf-0-1");
    tree.nodes[branch].note = QStringLiteral("原图里的阶段备注");
    tree.nodes[branch].collapsed = true;
    tree.nodes[leaf].note = QStringLiteral("原图里的任务备注");
    loadEditorFixture(window, project);
    window.resize(1240, 760);
    window.show();
    QApplication::processEvents();
    window.findChild<QAction *>("fitTopic")->trigger();
    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    auto *doc = window.findChild<ProjectDocument *>();
    const auto revision = doc->sessionRevision();
    const auto pickTopics = [&](QDialog *picker)
    {
        const auto rows = reviewPickerRows(picker);
        QCOMPARE(rows.size(), 3);
        QVERIFY(rows.contains(branch) && rows.contains(leaf) && rows.contains(empty));
        QCOMPARE(rows.value(empty)->text(1), QStringLiteral("缺少答案"));
        rows.value(branch)->setCheckState(0, Qt::Checked);
        rows.value(leaf)->setCheckState(0, Qt::Checked);
        captureEfficiency(*picker, QStringLiteral("04-从文稿选题建卡"), "LINGYA_H053_CAPTURE_DIR");
        picker->findChild<QPushButton *>("confirmReviewTopics")->click();
    };
    const auto checkManager = [&](QDialog *manager)
    {
        QCOMPARE(doc->sessionRevision(), revision + 1);
        QCOMPARE(manager->findChild<QLineEdit *>("draftTitle")->text(),
                 QStringLiteral("阶段讲解稿"));
        auto *edit = manager->findChild<QPlainTextEdit *>("draftText");
        edit->setPlainText(QStringLiteral("尚未保存的手改稿"));
        answerChildDialog(*manager, "pickReviewTopicsDialog", pickTopics);
        manager->findChild<QPushButton *>("reviewDraftSources")->click();
        QCOMPARE(doc->sessionRevision(), revision + 2);
        QCOMPARE(edit->toPlainText(), QStringLiteral("尚未保存的手改稿"));
        manager->findChild<QPushButton *>("saveDraftText")->click();
        QCOMPARE(doc->sessionRevision(), revision + 3);
    };
    const auto openPreview = [&](bool save)
    {
        auto *node = visibleTopic(view, branch);
        QVERIFY(node);
        const auto checkPreview = [&, save](QDialog *preview)
        {
            const auto text =
                preview->findChild<QPlainTextEdit *>("branchDraftText")->toPlainText();
            QVERIFY(text.contains(QStringLiteral("原图里的阶段备注")));
            QVERIFY(text.contains(QStringLiteral("原图里的任务备注")));
            QVERIFY(text.contains(QStringLiteral("待补充正文")));
            QVERIFY(!text.contains(QStringLiteral("阶段2")));
            if (!save)
                return;
            preview->findChild<QLineEdit *>("branchDraftTitle")
                ->setText(QStringLiteral("阶段讲解稿"));
            captureEfficiency(*preview, QStringLiteral("03-分支文稿预览"),
                              "LINGYA_H053_CAPTURE_DIR");
            answerChildDialog(window, "draftManager", checkManager);
            preview->findChild<QPushButton *>("saveBranchDraft")->click();
        };
        contextAction(window, view, view->mapFromScene(node->scenePos()), "draftFromBranch",
                      [&] { answerChildDialog(window, "branchDraftPreview", checkPreview); });
    };
    openPreview(false);
    QCOMPARE(doc->sessionRevision(), revision);
    QVERIFY(doc->project().drafts.isEmpty());
    openPreview(true);
    QCOMPARE(doc->project().drafts.size(), 1);
    QCOMPARE(doc->project().cards.size(), 2);
    const auto &draft = doc->project().drafts.front();
    QVERIFY(draft.sourceHash.has_value());
    QCOMPARE(draft.sections.size(), 3);
    QCOMPARE(draft.sections[0].sourceNodeIds, QStringList{branch});
    QCOMPARE(draft.sections[1].sourceNodeIds, QStringList{leaf});
    QCOMPARE(draft.sections[2].sourceNodeIds, QStringList{empty});
    QCOMPARE(draft.sections.front().text, QStringLiteral("尚未保存的手改稿"));
    QCOMPARE(doc->project().cards.front().back, QStringLiteral("原图里的阶段备注"));
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const auto path = files.filePath(QStringLiteral("分支文字稿.lingya"));
    QVERIFY(window.saveProjectTo(path));
    MainWindow reopened;
    QVERIFY(reopened.openFile(path));
    reopened.show();
    auto *reopenedDoc = reopened.findChild<ProjectDocument *>();
    QCOMPARE(ProjectJson::toJson(reopenedDoc->project()), ProjectJson::toJson(doc->project()));
    runEditor(reopened, "manageDrafts", "draftManager",
              [&](QDialog *manager)
              {
                  manager->findChild<QListWidget *>("draftSections")->setCurrentRow(1);
                  manager->findChild<QPushButton *>("locateDraftSource")->click();
                  auto *outline = reopened.findChild<QTreeWidget *>("topicOutline");
                  QCOMPARE(outline->currentItem()->data(0, Qt::UserRole).toString(), leaf);
              });
}

void TstUi::draftSourceComparisonPreservesManualText()
{
    MainWindow window;
    auto project = canvasProject(2, 2);
    auto &tree = std::get<TreeSheet>(project.sheets.front());
    const auto sheetId = tree.id;
    const auto first = canvasId("branch-0");
    const auto second = canvasId("branch-1");
    const auto removed = canvasId("leaf-0-1");
    tree.nodes[first].title = QStringLiteral("原主题标题");
    tree.nodes[first].note = QStringLiteral("旧备注内容");
    tree.nodes[second].note = QStringLiteral("保持原样的备注");
    tree.nodes[removed].note = QStringLiteral("删除前的备注");
    loadEditorFixture(window, project);
    window.show();
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    DraftProposal proposal;
    proposal.title = QStringLiteral("阶段复盘稿");
    proposal.sections = {
        {QStringLiteral("结论"), QStringLiteral("已保存的手改结论"), {first}},
        {QStringLiteral("另一个阶段"), QStringLiteral("未受影响的正文"), {second}},
        {QStringLiteral("综合分析"), QStringLiteral("已整理的分析"), {second, first}},
        {QStringLiteral("原例子"), QStringLiteral("已写好的例子"), {removed}},
        {QStringLiteral("补充"), QStringLiteral("无来源的补充"), {}, "supplement"}};
    QVERIFY(std::holds_alternative<ID>(
        AiCommands(history, doc).saveDraft(proposal, sheetId, "summary", doc->sessionRevision())));
    QVERIFY(std::holds_alternative<bool>(
        history->applyEdit(QStringLiteral("修改原导图"),
                           [&](Project &changed)
                           {
                               auto &sheet = std::get<TreeSheet>(changed.sheets.front());
                               sheet.nodes[first].title = QStringLiteral("更新后的主题");
                               sheet.nodes[first].note = QStringLiteral("最新备注内容");
                               sheet.nodes[second].manualPosition = QPointF(80, 20);
                               sheet.nodes[sheet.rootId].note = QStringLiteral("与稿件无关的修改");
                               sheet.nodes.remove(removed);
                           })));
    const auto revision = doc->sessionRevision();
    const auto before = ProjectJson::toJson(doc->project());
    runEditor(window, "manageDrafts", "draftManager",
              [&](QDialog *manager)
              {
                  auto *sections = manager->findChild<QListWidget *>("draftSections");
                  auto *text = manager->findChild<QPlainTextEdit *>("draftText");
                  auto *status = manager->findChild<QLabel *>("draftSourceStatus");
                  auto *compare = manager->findChild<QPushButton *>("compareDraftSource");
                  QVERIFY(sections && text && status && compare);
                  const QString badge = QStringLiteral("来源变更");
                  QVERIFY(status->text().contains(QStringLiteral("3 段来源有变更")));
                  QVERIFY(sections->item(0)->text().startsWith(badge));
                  QCOMPARE(sections->item(1)->text(), proposal.sections.at(1).heading);
                  QVERIFY(sections->item(2)->text().startsWith(badge));
                  QVERIFY(sections->item(3)->text().startsWith(badge));
                  QCOMPARE(sections->item(4)->text(), proposal.sections.at(4).heading);
                  sections->setCurrentRow(4);
                  QVERIFY(!compare->isEnabled());
                  sections->setCurrentRow(0);
                  text->setPlainText(QStringLiteral("窗口中尚未保存的手改正文"));
                  captureEfficiency(*manager, QStringLiteral("01-段落来源标记"),
                                    "LINGYA_H054_CAPTURE_DIR");
                  const auto checkFirstComparison = [&](QDialog *dialog, bool confirm)
                  {
                      auto *old = dialog->findChild<QPlainTextEdit *>("draftPreviousSource");
                      auto *now = dialog->findChild<QPlainTextEdit *>("draftCurrentSource");
                      auto *preview = dialog->findChild<QPlainTextEdit *>("draftComparisonText");
                      QVERIFY(old && now && preview && old->isReadOnly() && now->isReadOnly());
                      QVERIFY(old->toPlainText().contains(QStringLiteral("原主题标题")));
                      QVERIFY(old->toPlainText().contains(QStringLiteral("旧备注内容")));
                      QVERIFY(now->toPlainText().contains(QStringLiteral("更新后的主题")));
                      QVERIFY(now->toPlainText().contains(QStringLiteral("最新备注内容")));
                      const auto changes =
                          dialog->findChild<QLabel *>("draftComparisonChanges")->text();
                      QVERIFY(changes.contains(QStringLiteral("标题已修改")));
                      QVERIFY(changes.contains(QStringLiteral("备注已修改")));
                      QCOMPARE(preview->toPlainText(), text->toPlainText());
                      dialog->findChild<QPushButton *>("insertDraftSourceNote")->click();
                      QVERIFY(preview->toPlainText().startsWith(text->toPlainText()));
                      QVERIFY(preview->toPlainText().endsWith(QStringLiteral("最新备注内容")));
                      if (confirm)
                          dialog->findChild<QPushButton *>("confirmDraftSourcesOnly")->click();
                      else
                          captureEfficiency(*dialog, QStringLiteral("02-原主题前后对照"),
                                            "LINGYA_H054_CAPTURE_DIR");
                  };
                  answerChildDialog(*manager, "draftSourceComparison",
                                    [&](QDialog *dialog) { checkFirstComparison(dialog, false); });
                  compare->click();
                  QCOMPARE(doc->sessionRevision(), revision);
                  QCOMPARE(ProjectJson::toJson(doc->project()), before);
                  QCOMPARE(text->toPlainText(), QStringLiteral("窗口中尚未保存的手改正文"));
                  answerChildDialog(*manager, "draftSourceComparison",
                                    [&](QDialog *dialog) { checkFirstComparison(dialog, true); });
                  compare->click();
                  QCOMPARE(doc->sessionRevision(), revision + 1);
                  QCOMPARE(doc->project().drafts.front().sections.front().text,
                           proposal.sections.front().text);
                  QCOMPARE(text->toPlainText(), QStringLiteral("窗口中尚未保存的手改正文"));
                  QCOMPARE(sections->item(0)->text(), proposal.sections.front().heading);
                  QVERIFY(sections->item(2)->text().startsWith(badge));

                  sections->setCurrentRow(2);
                  answerChildDialog(
                      *manager, "draftSourceComparison",
                      [&](QDialog *dialog)
                      {
                          auto *topics = dialog->findChild<QComboBox *>("draftComparisonTopic");
                          QCOMPARE(topics->count(), 2);
                          QCOMPARE(topics->currentData().toString(), first);
                          topics->setCurrentIndex(topics->findData(second));
                          QVERIFY(dialog->findChild<QLabel *>("draftComparisonChanges")
                                      ->text()
                                      .contains(QStringLiteral("一致")));
                          dialog->findChild<QLineEdit *>("draftComparisonHeading")
                              ->setText(QStringLiteral("综合分析修订稿"));
                          dialog->findChild<QPlainTextEdit *>("draftComparisonText")
                              ->setPlainText(QStringLiteral("用户确认合并后的正文"));
                          dialog->findChild<QPushButton *>("applyDraftSourceUpdate")->click();
                      });
                  compare->click();
                  QCOMPARE(doc->sessionRevision(), revision + 2);
                  QCOMPARE(text->toPlainText(), QStringLiteral("用户确认合并后的正文"));
                  QCOMPARE(sections->item(2)->text(), QStringLiteral("综合分析修订稿"));
                  QVERIFY(sections->item(3)->text().startsWith(badge));
                  sections->setCurrentRow(0);
                  QCOMPARE(text->toPlainText(), QStringLiteral("窗口中尚未保存的手改正文"));
                  manager->findChild<QPushButton *>("saveDraftText")->click();
                  QCOMPARE(doc->sessionRevision(), revision + 3);
              });
    QCOMPARE(doc->project().drafts.front().sections.front().text,
             QStringLiteral("窗口中尚未保存的手改正文"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("逐段核对稿件.lingya"));
    QVERIFY(window.saveProjectTo(path));
    MainWindow reopened;
    QVERIFY(reopened.openFile(path));
    reopened.show();
    runEditor(
        reopened, "manageDrafts", "draftManager",
        [](QDialog *manager)
        {
            QVERIFY(manager->findChild<QLabel *>("draftSourceStatus")
                        ->text()
                        .contains(QStringLiteral("1 段来源有变更")));
            manager->findChild<QListWidget *>("draftSections")->setCurrentRow(3);
            answerChildDialog(
                *manager, "draftSourceComparison",
                [](QDialog *dialog)
                {
                    QVERIFY(dialog->findChild<QPlainTextEdit *>("draftPreviousSource")
                                ->toPlainText()
                                .contains(QStringLiteral("删除前的备注")));
                    QVERIFY(dialog->findChild<QLabel *>("draftComparisonChanges")
                                ->text()
                                .contains(QStringLiteral("已删除")));
                    QVERIFY(
                        !dialog->findChild<QPushButton *>("insertDraftSourceNote")->isEnabled());
                    captureEfficiency(*dialog, QStringLiteral("03-删除来源对照"),
                                      "LINGYA_H054_CAPTURE_DIR");
                });
            manager->findChild<QPushButton *>("compareDraftSource")->click();
        });
}

void TstUi::legacyDraftComparisonLeavesUnverifiedHistoryUnknown()
{
    MainWindow window;
    auto project = canvasProject(1, 0);
    auto &tree = std::get<TreeSheet>(project.sheets.front());
    tree.nodes[tree.rootId].note = QStringLiteral("现在看到的备注");
    TextDraft old;
    old.id = canvasId("legacy-draft");
    old.sourceSheetId = tree.id;
    old.title = QStringLiteral("旧文字稿");
    old.createdAt = QDateTime::currentDateTimeUtc();
    old.sections = {{QStringLiteral("旧段落"), QStringLiteral("保留这段手写内容"), {tree.rootId}}};
    project.drafts.append(old);
    loadEditorFixture(window, project);
    window.show();
    auto *doc = window.findChild<ProjectDocument *>();
    runEditor(
        window, "manageDrafts", "draftManager",
        [&](QDialog *manager)
        {
            auto *list = manager->findChild<QListWidget *>("draftSections");
            QVERIFY(list->item(0)->text().startsWith(QStringLiteral("来源待核对")));
            answerChildDialog(
                *manager, "draftSourceComparison",
                [](QDialog *dialog)
                {
                    QCOMPARE(
                        dialog->findChild<QPlainTextEdit *>("draftPreviousSource")->toPlainText(),
                        QStringLiteral("旧来源未记录"));
                    QVERIFY(dialog->findChild<QPlainTextEdit *>("draftCurrentSource")
                                ->toPlainText()
                                .contains(QStringLiteral("现在看到的备注")));
                    dialog->findChild<QPushButton *>("confirmDraftSourcesOnly")->click();
                });
            manager->findChild<QPushButton *>("compareDraftSource")->click();
            QCOMPARE(list->item(0)->text(), old.sections.front().heading);
            QCOMPARE(doc->project().drafts.front().sections.front().text,
                     old.sections.front().text);
            QVERIFY(doc->project().drafts.front().sections.front().sourceSnapshot);
        });
}

void TstUi::outlinePastePreviewsAndCommitsOnce()
{
    MainWindow window;
    loadEditorFixture(window, canvasProject(1, 0));
    window.show();
    const ID target = canvasId("branch-0");
    chooseTopic(window, target);
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    const auto before = ProjectJson::toJson(doc->project());
    const auto revision = doc->sessionRevision();
    auto *clipboard = QApplication::clipboard();
    const auto clearClipboard = qScopeGuard([clipboard] { clipboard->clear(); });
    clipboard->setText(QStringLiteral("- 准备材料\r\n\t1. 汇总来源\r\n\t2) 确认时间\r\n"
                                      "\t    - 发送通知\r\n\r\n+ 汇报结果"));
    runEditor(window, "pasteOutline", "pasteOutlineDialog",
              [&](QDialog *dialog)
              {
                  auto *input = dialog->findChild<QPlainTextEdit *>("outlinePasteText");
                  auto *preview = dialog->findChild<QTreeWidget *>("outlinePastePreview");
                  auto *add = dialog->findChild<QPushButton *>("applyOutlinePaste");
                  QVERIFY(input && preview && add);
                  QCOMPARE(preview->topLevelItemCount(), 2);
                  auto *first = preview->topLevelItem(0);
                  QCOMPARE(first->text(0), QStringLiteral("准备材料"));
                  QCOMPARE(first->childCount(), 2);
                  QCOMPARE(first->child(0)->text(0), QStringLiteral("汇总来源"));
                  QCOMPARE(first->child(1)->child(0)->text(0), QStringLiteral("发送通知"));
                  QCOMPARE(preview->topLevelItem(1)->text(0), QStringLiteral("汇报结果"));
                  captureEfficiency(*dialog, QStringLiteral("01-大纲预览"));
                  input->setPlainText(QString(2001, u'字'));
                  QVERIFY(!add->isEnabled());
                  QCOMPARE(preview->topLevelItemCount(), 0);
                  input->setPlainText(QStringLiteral("第一行\n  第二行"));
                  QVERIFY(add->isEnabled());
                  QCOMPARE(preview->topLevelItem(0)->child(0)->text(0), QStringLiteral("第二行"));
              });
    QCOMPARE(ProjectJson::toJson(doc->project()), before);
    QCOMPARE(doc->sessionRevision(), revision);
    QVERIFY(!history->canUndo());

    runEditor(window, "pasteOutline", "pasteOutlineDialog", [](QDialog *dialog)
              { dialog->findChild<QPushButton *>("applyOutlinePaste")->click(); });
    QCOMPARE(doc->sessionRevision(), revision + 1);
    const auto tree = std::get<TreeSheet>(doc->project().sheets.first());
    QCOMPARE(tree.nodes.size(), 7);
    QMap<QString, TreeNode> byTitle;
    for (const auto &node : tree.nodes)
        byTitle.insert(node.title, node);
    const auto first = byTitle.value(QStringLiteral("准备材料"));
    const auto second = byTitle.value(QStringLiteral("汇报结果"));
    const auto time = byTitle.value(QStringLiteral("确认时间"));
    QCOMPARE(first.parentId.value_or(ID{}), target);
    QCOMPARE(second.parentId.value_or(ID{}), target);
    QCOMPARE(first.order, 0);
    QCOMPARE(second.order, 1);
    QCOMPARE(time.parentId.value_or(ID{}), first.id);
    QCOMPARE(byTitle.value(QStringLiteral("发送通知")).parentId.value_or(ID{}), time.id);
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), before);
    QVERIFY(!history->canUndo());
    history->redo();
    QCOMPARE(std::get<TreeSheet>(doc->project().sheets.first()).nodes.size(), 7);
}

void TstUi::dropAndPasteImages_data()
{
    QTest::addColumn<QString>("kind");
    for (const auto *kind : {"mindmap", "fishbone", "flowchart", "gantt"})
        QTest::newRow(kind) << QString::fromLatin1(kind);
}

void TstUi::dropAndPasteImages()
{
    QFETCH(QString, kind);
    MainWindow window;
    auto project = ProjectFactory::createMindmap(QStringLiteral("导入图片"));
    const auto sheet = ProjectFactory::createSheet(kind, QStringLiteral("导入图片"));
    QVERIFY(std::holds_alternative<Sheet>(sheet));
    project.sheets = {std::get<Sheet>(sheet)};
    project.activeSheetId = std::visit([](const auto &s) { return s.id; }, project.sheets.front());
    loadEditorFixture(window, project);
    window.resize(1350, 820);
    window.show();
    QTest::qWait(35);
    auto *view = kind == "gantt" ? static_cast<QGraphicsView *>(window.findChild<GanttView *>())
                                 : window.findChild<QGraphicsView *>("topicPreview");
    QVERIFY(view);
    QVERIFY(view->viewport()->acceptDrops());
    if (kind != "gantt")
        window.findChild<QAction *>("fitTopic")->trigger();
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    const auto initial = ProjectJson::toJson(doc->project());
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const auto path = files.filePath(QStringLiteral("参考 图.png"));
    QImage raster(360, 180, QImage::Format_ARGB32);
    raster.fill(QColor("#C8E4D3"));
    {
        QPainter painter(&raster);
        painter.setPen(QColor("#205746"));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 18));
        painter.drawText(raster.rect(), Qt::AlignCenter, QStringLiteral("参考示意图"));
    }
    QVERIFY(raster.save(path));
    QMimeData imageFile;
    imageFile.setUrls({QUrl::fromLocalFile(path)});
    if (kind != "gantt")
    {
        const auto id = kind == "flowchart"
                            ? std::get<FlowSheet>(project.sheets.front()).nodes.firstKey()
                            : std::get<TreeSheet>(project.sheets.front()).rootId;
        auto *node = visibleTopic(view, id);
        QVERIFY(node);
        QVERIFY(dropImage(view, imageFile, view->mapFromScene(node->scenePos())));
        const auto nodeImage =
            kind == "flowchart"
                ? std::get<FlowSheet>(doc->project().sheets.front()).nodes.value(id).imagePng
                : std::get<TreeSheet>(doc->project().sheets.front()).nodes.value(id).imagePng;
        QCOMPARE(ImageService::decode(nodeImage).size(), raster.size());
        history->undo();
        QCOMPARE(ProjectJson::toJson(doc->project()), initial);
    }
    const auto blank = view->viewport()->rect().bottomRight() - QPoint(100, 110);
    QMimeData remote;
    remote.setUrls({QUrl(QStringLiteral("https://example.invalid/不导入.png"))});
    QVERIFY(!dropImage(view, remote, blank));
    QCOMPARE(ProjectJson::toJson(doc->project()), initial);
    const auto expected = view->mapToScene(blank);
    QVERIFY(dropImage(view, imageFile, blank));
    const auto images = [&]
    { return std::visit([](const auto &s) { return s.images; }, doc->project().sheets.front()); };
    QCOMPARE(images().size(), 1);
    QCOMPARE(images().front().position, expected);
    QCOMPARE(ImageService::decode(images().front().imagePng).size(), raster.size());
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), initial);

    auto *clipboard = QApplication::clipboard();
    const auto clearClipboard = qScopeGuard([clipboard] { clipboard->clear(); });
    raster.setDevicePixelRatio(2);
    clipboard->setImage(raster);
    view->setFocus();
    const auto center = view->mapToScene(view->viewport()->rect().center());
    QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(images().size(), 1);
    QCOMPARE(images().front().position, center);
    const auto decoded = ImageService::decode(images().front().imagePng);
    QCOMPARE(decoded.size(), raster.size());
    QCOMPARE(decoded.devicePixelRatio(), 1.0);
    if (kind == "mindmap")
        captureEfficiency(window, QStringLiteral("02-画布粘贴截图"));

    const auto stored = files.filePath(QStringLiteral("嵌入图片.lingya"));
    QVERIFY(window.saveProjectTo(stored));
    const auto reopened = FileService::load(stored);
    QVERIFY(std::holds_alternative<Project>(reopened));
    QCOMPARE(std::visit([](const auto &s) { return s.images.front().imagePng; },
                        std::get<Project>(reopened).sheets.front()),
             images().front().imagePng);
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), initial);
}

void TstUi::focusBranchKeepsViewAndFolding()
{
    MainWindow window;
    auto project = canvasProject(2, 2);
    const auto branch = canvasId("branch-0");
    const auto neighbour = canvasId("branch-1");
    const auto child = canvasId("leaf-0-0");
    std::get<TreeSheet>(project.sheets.front()).nodes[branch].collapsed = true;
    loadEditorFixture(window, project);
    window.resize(1350, 820);
    window.show();
    QTest::qWait(35);
    chooseTopic(window, branch);
    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    auto *exit = window.findChild<QPushButton *>("exitBranchFocus");
    QVERIFY(view && exit);
    QVERIFY(!exit->isVisible());
    QVERIFY(!visibleTopic(view, child));
    view->setTransform(QTransform::fromScale(0.75, 0.75));
    QApplication::processEvents();
    view->centerOn(45, -32);
    const auto originalTransform = view->transform();
    const auto originalCenter = view->mapToScene(view->viewport()->rect().center());
    const auto revision = doc->sessionRevision();
    const auto initial = ProjectJson::toJson(doc->project());
    auto *target = visibleTopic(view, branch);
    QVERIFY(target);
    contextAction(window, view, view->mapFromScene(target->scenePos()),
                  QStringLiteral("focusBranch"));
    QVERIFY(exit->isVisible());
    QVERIFY(visibleTopic(view, child));
    QCOMPARE(visibleTopic(view, child)->opacity(), 1.0);
    QCOMPARE(visibleTopic(view, neighbour)->opacity(), 0.25);
    QCOMPARE(ProjectJson::toJson(doc->project()), initial);
    QCOMPARE(doc->sessionRevision(), revision);
    QVERIFY(!history->canUndo());
    captureEfficiency(window, QStringLiteral("03-分支专注"));

    target = visibleTopic(view, branch);
    contextAction(window, view, view->mapFromScene(target->scenePos()),
                  QStringLiteral("quickAddNode"));
    QCOMPARE(doc->sessionRevision(), revision + 1);
    QVERIFY(history->canUndo());
    QTest::keyClick(view, Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY(!exit->isVisible());
    QVERIFY(!visibleTopic(view, child));
    QCOMPARE(visibleTopic(view, neighbour)->opacity(), 1.0);
    QCOMPARE(view->transform(), originalTransform);
    const auto restoredCenter = view->mapToScene(view->viewport()->rect().center());
    const auto centerDelta = QLineF(originalCenter, restoredCenter).length();
    QVERIFY2(centerDelta <= 2, qPrintable(QStringLiteral("画布中心偏移 %1").arg(centerDelta)));
    QCOMPARE(doc->sessionRevision(), revision + 1);
    const auto tree = std::get<TreeSheet>(doc->project().sheets.front());
    QVERIFY(tree.nodes.value(branch).collapsed);
    QCOMPARE(tree.nodes.size(), 8);
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), initial);
}

void TstUi::dueReviewStartsOnlyAvailableCards()
{
    MainWindow window;
    auto project = canvasProject(1, 0);
    const auto tree = std::get<TreeSheet>(project.sheets.front());
    const auto day = QDate::currentDate();
    const auto card = [&](const QString &title, const QString &status, int daysAgo,
                          const QString &mode = QStringLiteral("custom"))
    {
        ReviewCard result;
        result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        result.sheetId = tree.id;
        result.nodeId = tree.rootId;
        result.front = title;
        result.back = QStringLiteral("示例答案");
        result.status = status;
        result.mode = mode;
        result.lastReviewedAt = QDateTime(day.addDays(-daysAgo), QTime(9, 0)).toUTC();
        return result;
    };
    project.cards = {card(QStringLiteral("新卡片"), "new", 0),
                     card(QStringLiteral("昨日报到"), "review", 1),
                     card(QStringLiteral("今天刚复习"), "review", 0),
                     card(QStringLiteral("上周已掌握"), "mastered", 7),
                     card(QStringLiteral("六天前掌握"), "mastered", 6),
                     card(QStringLiteral("来源需处理"), "new", 0, "linked")};
    loadEditorFixture(window, project);
    window.show();
    runEditor(window, "showReviewCards", "reviewCardsDialog",
              [](QDialog *dialog)
              {
                  auto *today = dialog->findChild<QPushButton *>("reviewDueToday");
                  auto *filter = dialog->findChild<QComboBox *>("reviewStatusFilter");
                  auto *list = dialog->findChild<QListWidget *>("reviewCards");
                  auto *progress = dialog->findChild<QProgressBar *>("reviewProgress");
                  QVERIFY(today && filter && list && progress);
                  QCOMPARE(today->text(), QStringLiteral("今日待复习（3）"));
                  QCOMPARE(list->count(), 6);
                  QTest::mouseClick(today, Qt::LeftButton);
                  QCOMPARE(filter->currentData().toString(), QStringLiteral("due"));
                  QCOMPARE(list->count(), 4);
                  QCOMPARE(progress->maximum(), 3);
                  QCOMPARE(dialog->findChild<QLabel *>("reviewFront")->text(),
                           QStringLiteral("新卡片"));
                  captureEfficiency(*dialog, QStringLiteral("04-今日待复习"));
                  dialog->findChild<QPushButton *>("reviewReveal")->click();
                  dialog->findChild<QPushButton *>("reviewReviewed")->click();
                  QCOMPARE(today->text(), QStringLiteral("今日待复习（2）"));
                  QCOMPARE(progress->value(), 1);
                  QCOMPARE(dialog->findChild<QLabel *>("reviewFront")->text(),
                           QStringLiteral("昨日报到"));
              });
}

void TstUi::historyCopyNeverOverwritesExistingFiles()
{
    MainWindow window;
    loadEditorFixture(window, canvasProject(1, 0));
    window.show();
    auto *doc = window.findChild<ProjectDocument *>();
    const auto id = doc->project().id;
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const auto original = files.filePath(QStringLiteral("课程工程.lingya"));
    const auto clone = files.filePath(QStringLiteral("课程工程 - 历史副本.lingya"));
    QVERIFY(window.renameRootTo(QStringLiteral("第一版内容")));
    QVERIFY(window.saveProjectTo(original));
    QVERIFY(window.renameRootTo(QStringLiteral("第二版内容")));
    QVERIFY(window.saveProjectTo(original));
    const auto latest = ProjectJson::toJson(doc->project());
    runEditor(window, "versionHistory", "versionHistoryDialog",
              [](QDialog *dialog)
              {
                  auto *list = dialog->findChild<QListWidget *>("savedVersions");
                  auto *open = dialog->findChild<QPushButton *>("openVersionCopy");
                  QVERIFY(list && open);
                  QCOMPARE(list->count(), 2);
                  list->setCurrentRow(1);
                  QVERIFY(open->isEnabled());
                  QVERIFY(dialog->findChild<QLabel *>("savedVersionPreview")
                              ->text()
                              .contains(QStringLiteral("第一版内容")));
                  captureEfficiency(*dialog, QStringLiteral("05-本机版本历史"));
                  QTest::mouseClick(open, Qt::LeftButton);
              });
    QVERIFY(doc->project().id != id);
    const auto &oldTree = std::get<TreeSheet>(doc->project().sheets.front());
    QCOMPARE(oldTree.nodes.value(oldTree.rootId).title, QStringLiteral("第一版内容"));
    bool warned = false;
    QTimer::singleShot(0, &window,
                       [&]
                       {
                           if (auto *box =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                           {
                               warned = box->text().contains(QStringLiteral("文件名已存在"));
                               box->accept();
                           }
                       });
    QVERIFY(!window.saveProjectTo(original));
    QVERIFY(warned);
    QCOMPARE(ProjectJson::toJson(std::get<Project>(FileService::load(original))), latest);
    QVERIFY(window.saveProjectTo(clone));
    QCOMPARE(std::get<Project>(FileService::load(clone)).id, doc->project().id);
    QCOMPARE(ProjectJson::toJson(std::get<Project>(FileService::load(original))), latest);
}

void TstUi::freeTreeMouseAndPlus()
{
    MainWindow window;
    loadEditorFixture(window, canvasProject(2, 1));
    window.show();
    QTest::qWait(30);
    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    window.findChild<QAction *>("fitTopic")->trigger();
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    const ID id = canvasId("branch-0");
    auto before = canvasNodes(view);
    const auto origin = before[id]->pos();
    const auto parent = std::get<TreeSheet>(doc->project().sheets.first()).nodes[id].parentId;
    QPoint start = view->mapFromScene(origin), end = start + QPoint(54, 38);
    const auto delta = view->mapToScene(end) - view->mapToScene(start);
    const auto revision = doc->sessionRevision();
    mouseDrag(view->viewport(), start, end, Qt::NoModifier, false);
    QCOMPARE(doc->sessionRevision(), revision);
    QCOMPARE(canvasNodes(view)[id]->pos(), origin + delta);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(doc->sessionRevision(), revision + 1);
    QCOMPARE(std::get<TreeSheet>(doc->project().sheets.first()).nodes[id].parentId, parent);
    QVERIFY(
        std::get<TreeSheet>(doc->project().sheets.first()).nodes[id].manualPosition.has_value());
    history->undo();
    QCOMPARE(canvasNodes(view)[id]->pos(), origin);
    history->redo();
    QCOMPARE(canvasNodes(view)[id]->pos(), origin + delta);
    start = view->mapFromScene(canvasNodes(view)[id]->pos());
    mouseDrag(view->viewport(), start, start + QPoint(30, -20), Qt::NoModifier, false);
    QTest::keyClick(view, Qt::Key_Escape);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, start + QPoint(30, -20));
    QCOMPARE(doc->sessionRevision(), revision + 3);
    chooseTopic(window, id);
    const auto count = std::get<TreeSheet>(doc->project().sheets.first()).nodes.size();
    QGraphicsItem *plus = nullptr;
    for (auto *child : canvasNodes(view)[id]->childItems())
        if (child->data(0x21C5).isValid())
            plus = child;
    QVERIFY(plus);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      view->mapFromScene(plus->sceneBoundingRect().center()));
    QCOMPARE(std::get<TreeSheet>(doc->project().sheets.first()).nodes.size(), count + 1);
    const auto afterPlus = std::get<TreeSheet>(doc->project().sheets.first());
    for (const auto &candidate : afterPlus.nodes)
        if (!canvasNodes(view).contains(candidate.id))
            QFAIL("新增主题必须出现在画布中");
    ID created;
    for (const auto &candidate : afterPlus.nodes)
        if (candidate.title == QStringLiteral("未命名主题"))
            created = candidate.id;
    QVERIFY(!created.isEmpty());
    const auto newBounds = canvasNodes(view).value(created)->sceneBoundingRect();
    for (auto *existing : canvasNodes(view))
        if (existing->nodeId() != created)
            QVERIFY(!newBounds.intersects(existing->sceneBoundingRect()));
    const QPoint blank = view->viewport()->rect().bottomLeft() + QPoint(70, -50);
    const auto expected = view->mapToScene(blank);
    contextAction(window, view, blank, "createTopicHere");
    const auto tree = std::get<TreeSheet>(doc->project().sheets.first());
    bool atClick = false;
    for (const auto &n : tree.nodes)
        atClick |= n.manualPosition == std::optional<QPointF>(expected);
    QVERIFY(atClick);
    QTemporaryDir files;
    QVERIFY(window.saveProjectTo(files.filePath("自由位置.lingya")));
    MainWindow reopened;
    QVERIFY(reopened.openFile(files.filePath("自由位置.lingya")));
    QCOMPARE(ProjectJson::toJson(reopened.findChild<ProjectDocument *>()->project()),
             ProjectJson::toJson(doc->project()));
    window.findChild<QAction *>("automaticTreeLayout")->trigger();
    for (const auto &n : std::get<TreeSheet>(doc->project().sheets.first()).nodes)
        QVERIFY(!n.manualPosition);
    history->undo();
    captureInteraction(window, "01-自由分支与加号");
}
void TstUi::flowAutoRoutes_data()
{
    QTest::addColumn<QPointF>("position");
    QTest::addColumn<QString>("sourcePort");
    QTest::addColumn<QString>("targetPort");
    QTest::addColumn<bool>("curved");
    QTest::addColumn<bool>("blocked");
    QTest::newRow("horizontal") << QPointF(330, 0) << QString("right") << QString("left") << false
                                << false;
    QTest::newRow("horizontal-reverse")
        << QPointF(-330, 0) << QString("left") << QString("right") << false << false;
    QTest::newRow("short-gap") << QPointF(140, 0) << QString("right") << QString("left") << false
                               << false;
    QTest::newRow("vertical") << QPointF(0, 250) << QString("bottom") << QString("top") << false
                              << false;
    QTest::newRow("vertical-reverse")
        << QPointF(0, -250) << QString("top") << QString("bottom") << false << false;
    QTest::newRow("perpendicular")
        << QPointF(330, 200) << QString("right") << QString("top") << true << false;
    QTest::newRow("offset") << QPointF(330, 120) << QString("right") << QString("left") << true
                            << false;
    QTest::newRow("offset-vertical")
        << QPointF(120, 300) << QString("bottom") << QString("top") << true << false;
    QTest::newRow("same-side") << QPointF(330, 0) << QString("top") << QString("top") << true
                               << false;
    QTest::newRow("obstacle") << QPointF(500, 0) << QString("right") << QString("left") << true
                              << true;
}

void TstUi::flowAutoRoutes()
{
    QFETCH(QPointF, position);
    QFETCH(QString, sourcePort);
    QFETCH(QString, targetPort);
    QFETCH(bool, curved);
    QFETCH(bool, blocked);
    MainWindow window;
    auto project = ProjectFactory::createFlowchart(QStringLiteral("流程自动走线"));
    auto &flow = std::get<FlowSheet>(project.sheets.first());
    const ID source = flow.nodes.firstKey(), target = canvasId("route-target"),
             blocker = canvasId("route-blocker");
    flow.nodes[source].role = "process";
    FlowNode node;
    node.id = target;
    node.title = QStringLiteral("终点");
    node.position = position;
    flow.nodes.insert(target, node);
    if (blocked)
    {
        node.id = blocker;
        node.title = QStringLiteral("中间节点");
        node.position = {250, 0};
        flow.nodes.insert(blocker, node);
        node.id = canvasId("far-away");
        node.title = QStringLiteral("无关节点");
        node.position = {780, 140};
        flow.nodes.insert(node.id, node);
    }
    loadEditorFixture(window, project);
    window.show();
    QTest::qWait(30);
    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    window.findChild<QAction *>("fitTopic")->trigger();
    auto *doc = window.findChild<ProjectDocument *>();
    const auto *a = flowPort(view, source, sourcePort);
    const auto *b = flowPort(view, target, targetPort);
    QVERIFY(a && b);
    const auto start = a->sceneBoundingRect().center(), finish = b->sceneBoundingRect().center();
    const auto startClick = view->mapFromScene(start);
    QVERIFY(view->viewport()->rect().contains(startClick));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, startClick);
    const auto finishClick = view->mapFromScene(finish);
    QVERIFY(view->viewport()->rect().contains(finishClick));
    if (QByteArray(QTest::currentDataTag()) == "short-gap")
    {
        // 强制起点的加号覆盖目标端口，消除随机节点ID对叠放顺序的影响。
        auto *sourceItem = canvasNodes(view).value(source);
        QVERIFY(sourceItem);
        sourceItem->setZValue(1);
        const auto *hit = view->itemAt(finishClick);
        QVERIFY(hit && hit->data(0x21C5).toBool());
    }
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, finishClick);
    const auto &shown = std::get<FlowSheet>(doc->project().sheets.first());
    QCOMPARE(shown.edges.size(), 1);
    const auto &edge = shown.edges.first();
    QCOMPARE(edge.sourceId, source);
    QCOMPARE(edge.targetId, target);
    QCOMPARE(edge.sourcePort, sourcePort);
    QCOMPARE(edge.targetPort, targetPort);
    const auto *line = flowLine(view, edge.id);
    QVERIFY(line);
    const auto path = line->path();
    QCOMPARE(QPointF(path.elementAt(0)), start);
    QCOMPARE(path.currentPosition(), finish);
    int curves = 0;
    for (int i = 1; i < path.elementCount(); ++i)
        curves += path.elementAt(i).isCurveTo();
    if (blocked)
    {
        QVERIFY(curves >= 2);
        QPainterPathStroker stroke;
        stroke.setWidth(line->pen().widthF());
        QVERIFY(!stroke.createStroke(path).intersects(
            canvasNodes(view).value(blocker)->sceneBoundingRect()));
        QVERIFY(path.boundingRect().right() <= finish.x());
        captureInteraction(window, "流程圆滑避障");
    }
    else
    {
        QCOMPARE(curves, curved ? 1 : 0);
        if (!curved)
        {
            QCOMPARE(path.elementCount(), 2);
            QVERIFY(path.elementAt(1).isLineTo());
        }
        if (QByteArray(QTest::currentDataTag()) == "horizontal")
            captureInteraction(window, "端口水平直连");
    }
}

void TstUi::flowCurveFollowsDraggedNode()
{
    MainWindow window;
    auto project = ProjectFactory::createFlowchart(QStringLiteral("拖动节点时曲线跟随"));
    auto &sheet = std::get<FlowSheet>(project.sheets.first());
    const ID source = sheet.nodes.firstKey(), target = canvasId("dragged-flow-target");
    FlowNode node;
    node.id = target;
    node.title = QStringLiteral("处理步骤");
    node.position = {330, 0};
    sheet.nodes.insert(target, node);
    loadEditorFixture(window, project);
    // 离屏高缩放会缩小默认窗口，拖动测试使用固定逻辑画布尺寸。
    window.resize(1280, 800);
    window.show();
    QTest::qWait(30);
    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    window.findChild<QAction *>("fitTopic")->trigger();
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      portPoint(view, source, "right"));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      portPoint(view, target, "left"));
    const auto &edges = std::get<FlowSheet>(doc->project().sheets.first()).edges;
    QCOMPARE(edges.size(), 1);
    const ID edgeId = edges.first().id;
    QVERIFY(flowLine(view, edgeId));
    const auto straight = flowLine(view, edgeId)->path();
    QCOMPARE(straight.elementCount(), 2);

    const auto before = ProjectJson::toJson(doc->project());
    const auto revision = doc->sessionRevision();
    auto *targetItem = canvasNodes(view).value(target);
    QVERIFY(targetItem);
    const auto origin = targetItem->pos();
    const QPoint from = view->mapFromScene(origin);
    const QPoint to = from + QPoint(-35, 125);
    QVERIFY(view->viewport()->rect().contains(from));
    QCOMPARE(view->itemAt(from), targetItem);
    QVERIFY(view->viewport()->rect().contains(to));
    mouseDrag(view->viewport(), from, to, Qt::NoModifier, false);
    QVERIFY(canvasNodes(view).value(target)->pos() != origin);
    QCOMPARE(doc->sessionRevision(), revision);
    QCOMPARE(ProjectJson::toJson(doc->project()), before);
    QVERIFY(flowLine(view, edgeId));
    const auto preview = flowLine(view, edgeId)->path();
    const auto *port = flowPort(view, target, "left");
    QVERIFY(port);
    QCOMPARE(preview.currentPosition(), port->sceneBoundingRect().center());
    QCOMPARE(QPointF(preview.elementAt(0)), QPointF(straight.elementAt(0)));
    bool hasCurve = false;
    for (int i = 1; i < preview.elementCount(); ++i)
        hasCurve |= preview.elementAt(i).isCurveTo();
    QVERIFY(hasCurve);
    captureEfficiency(*view->viewport(), "拖动节点时曲线跟随", "LINGYA_H055_CAPTURE_DIR");

    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(doc->sessionRevision(), revision + 1);
    QVERIFY(flowLine(view, edgeId));
    QCOMPARE(flowLine(view, edgeId)->path(), preview);
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), before);
    QVERIFY(flowLine(view, edgeId));
    QCOMPARE(flowLine(view, edgeId)->path(), straight);
    history->redo();
    QVERIFY(flowLine(view, edgeId));
    QCOMPARE(flowLine(view, edgeId)->path(), preview);
}

void TstUi::flowAutoRouteAndReconnect()
{
    MainWindow window;
    auto project = ProjectFactory::createFlowchart("重连手势");
    auto &flow = std::get<FlowSheet>(project.sheets.first());
    const ID first = flow.nodes.firstKey(), second = canvasId("curve-target"),
             third = canvasId("curve-third");
    FlowNode n;
    n.id = second;
    n.title = "第二步";
    n.position = {0, 250};
    flow.nodes.insert(n.id, n);
    n.id = third;
    n.title = "第三步";
    n.position = {300, 250};
    flow.nodes.insert(n.id, n);
    loadEditorFixture(window, project);
    window.show();
    QTest::qWait(30);
    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    window.findChild<QAction *>("fitTopic")->trigger();
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      portPoint(view, first, "bottom"));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      portPoint(view, second, "top"));
    QCOMPARE(std::get<FlowSheet>(doc->project().sheets.first()).edges.size(), 1);
    const auto edgeId = std::get<FlowSheet>(doc->project().sheets.first()).edges.first().id;
    const auto *line = flowLine(view, edgeId);
    QVERIFY(line);
    const auto original = line->path();
    const QPoint center = view->mapFromScene(original.pointAtPercent(0.5));
    const auto rev = doc->sessionRevision();
    mouseDrag(view->viewport(), center, center + QPoint(70, 0));
    QCOMPARE(doc->sessionRevision(), rev);
    QVERIFY(!std::get<FlowSheet>(doc->project().sheets.first()).edges.first().bendPoint);
    QVERIFY(!flowHandle(view, "bend"));
    QVERIFY(flowLine(view, edgeId));
    QCOMPARE(flowLine(view, edgeId)->path(), original);
    auto *handle = flowHandle(view, "target");
    QVERIFY(handle);
    mouseDrag(view->viewport(), view->mapFromScene(handle->sceneBoundingRect().center()),
              portPoint(view, third, "left"));
    auto changed = std::get<FlowSheet>(doc->project().sheets.first()).edges.first();
    QCOMPARE(changed.id, edgeId);
    QCOMPARE(changed.targetId, third);
    QCOMPARE(changed.targetPort, QString("left"));
    const auto before = ProjectJson::toJson(doc->project());
    handle = flowHandle(view, "source");
    QVERIFY(handle);
    mouseDrag(view->viewport(), view->mapFromScene(handle->sceneBoundingRect().center()),
              view->viewport()->rect().topLeft() + QPoint(20, 20));
    QCOMPARE(ProjectJson::toJson(doc->project()), before);
    handle = flowHandle(view, "source");
    mouseDrag(view->viewport(), view->mapFromScene(handle->sceneBoundingRect().center()),
              portPoint(view, second, "bottom"));
    QCOMPARE(std::get<FlowSheet>(doc->project().sheets.first()).edges.first().sourceId, second);
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), before);
    history->redo();
    const QPoint blank = view->viewport()->rect().bottomRight() - QPoint(25, 25);
    contextAction(window, view, blank, "createFlowHere_decision");
    QCOMPARE(std::get<FlowSheet>(doc->project().sheets.first()).nodes.size(), 4);
    QTemporaryDir files;
    QVERIFY(window.saveProjectTo(files.filePath("自动走线.lingya")));
    MainWindow reopened;
    QVERIFY(reopened.openFile(files.filePath("自动走线.lingya")));
    QCOMPARE(ProjectJson::toJson(reopened.findChild<ProjectDocument *>()->project()),
             ProjectJson::toJson(doc->project()));
    auto legacy = doc->project();
    const QPointF oldBend(-170, -280);
    std::get<FlowSheet>(legacy.sheets.first()).edges.first().bendPoint = oldBend;
    const auto legacyPath = files.filePath("旧手动曲线.lingya");
    QVERIFY(std::holds_alternative<qint64>(FileService::save(legacyPath, legacy)));
    QVERIFY(reopened.openFile(legacyPath));
    const auto *legacyDoc = reopened.findChild<ProjectDocument *>();
    QVERIFY(!legacyDoc->isModified());
    QCOMPARE(
        std::get<FlowSheet>(legacyDoc->project().sheets.first()).edges.first().bendPoint.value(),
        oldBend);
    const auto *reopenedLine =
        flowLine(reopened.findChild<QGraphicsView *>("topicPreview"), edgeId);
    QVERIFY(reopenedLine && flowLine(view, edgeId));
    QCOMPARE(reopenedLine->path(), flowLine(view, edgeId)->path());
    captureInteraction(window, "端点重连");
}

void TstUi::flowInlinePropertiesEditAndRoundTrip()
{
    MainWindow window;
    auto project = ProjectFactory::createFlowchart(QStringLiteral("流程节点属性"));
    auto &flow = std::get<FlowSheet>(project.sheets.first());
    const ID nodeId = flow.nodes.firstKey();
    flow.nodes[nodeId].role = "decision";
    flow.nodes[nodeId].style.shape = QStringLiteral("rect");
    loadEditorFixture(window, project);
    window.resize(1536, 960);
    window.show();
    QTest::qWait(30);
    auto *propertyDock = window.findChild<QDockWidget *>("propertyDock");
    QVERIFY(propertyDock);
    // 缩放后可用宽度变窄时，像用户一样通过属性开关打开侧栏。
    if (propertyDock->isHidden())
    {
        window.findChild<QAction *>("toggleProperties")->trigger();
    }
    QTRY_VERIFY(propertyDock->isVisible());
    auto *doc = window.findChild<ProjectDocument *>();
    auto *commands = window.findChild<CommandService *>();
    auto *tabs = window.findChild<QTabWidget *>("propertyTabs");
    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    const auto selected = [&]
    { return std::get<FlowSheet>(doc->project().sheets.first()).nodes.value(nodeId); };
    auto *fill = window.findChild<QComboBox *>("inlineFill");
    QVERIFY(fill && fill->isEnabled());
    QVERIFY(!tabs->isTabVisible(3));
    QCOMPARE(tabs->currentIndex(), 0);
    QVERIFY(window.findChild<QComboBox *>("inlineShape")->isHidden());
    QVERIFY(window.findChild<QToolButton *>("inlineFlowType")->isEnabled());
    const auto beforeStyle = doc->sessionRevision();
    fill->setCurrentIndex(fill->findData("#FFF0D0"));
    QCOMPARE(doc->sessionRevision(), beforeStyle + 1);
    QCOMPARE(selected().style.fill.value(), QString("#FFF0D0"));
    QCOMPARE(selected().style.shape.value(), QString("rect"));
    QCOMPARE(canvasNodes(view).value(nodeId)->nodeStyle().shape, QString("diamond"));
    QCOMPARE(canvasNodes(view).value(nodeId)->nodeStyle().fill, QString("#FFF0D0"));
    captureEfficiency(*propertyDock, "01-流程节点样式", "LINGYA_H056_CAPTURE_DIR");

    tabs->setCurrentIndex(2);
    auto *note = window.findChild<QPlainTextEdit *>("inlineNote");
    auto *link = window.findChild<QLineEdit *>("inlineLink");
    auto *apply = window.findChild<QPushButton *>("applyInlineNotes");
    QVERIFY(note->isEnabled() && link->isEnabled() && apply->isEnabled());
    const auto text = QStringLiteral("  先检查输入\n再执行下一步  ");
    note->setPlainText(text);
    tabs->setCurrentIndex(1);
    auto *icon = window.findChild<QToolButton *>("inlineIcon_idea");
    QVERIFY(icon->isVisible() && icon->isEnabled());
    QTest::mouseClick(icon, Qt::LeftButton);
    QVERIFY(selected().iconId);
    QCOMPARE(selected().iconId.value(), QString("idea"));
    QVERIFY(selected().note.isEmpty());
    tabs->setCurrentIndex(2);
    QCOMPARE(note->toPlainText(), text);
    const auto beforeNotes = doc->sessionRevision();
    link->setText(QStringLiteral("file:///C:/private.txt"));
    QTest::mouseClick(apply, Qt::LeftButton);
    QCOMPARE(doc->sessionRevision(), beforeNotes);
    QVERIFY(!window.findChild<QLabel *>("inlineNoteError")->text().isEmpty());
    const auto url = QStringLiteral("https://example.com/flow");
    link->setText(url);
    QTest::mouseClick(apply, Qt::LeftButton);
    QCOMPARE(doc->sessionRevision(), beforeNotes + 1);
    QCOMPARE(selected().note, text);
    QCOMPARE(selected().link.value(), url);
    QVERIFY(window.findChild<QAction *>("openNodeLink")->isEnabled());
    QVERIFY(window.findChild<QAction *>("editNodeProperties")->isEnabled());
    captureEfficiency(*propertyDock, "02-流程节点备注链接", "LINGYA_H056_CAPTURE_DIR");
    commands->undo();
    QVERIFY(selected().note.isEmpty());
    QVERIFY(!selected().link);
    commands->redo();
    QCOMPARE(selected().note, text);
    QCOMPARE(selected().link.value(), url);
    runEditor(window, "editFlowNode", "flowNodeDialog",
              [](QDialog *dialog)
              {
                  auto *role = dialog->findChild<QComboBox *>("flowRole");
                  role->setCurrentIndex(role->findData("io"));
                  dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
              });
    QCOMPARE(selected().role, QString("io"));
    QCOMPARE(selected().note, text);
    QTemporaryDir files;
    const auto path = files.filePath("流程节点属性.lingya");
    QVERIFY(window.saveProjectTo(path));
    MainWindow reopened;
    QVERIFY(reopened.openFile(path));
    QCOMPARE(ProjectJson::toJson(reopened.findChild<ProjectDocument *>()->project()),
             ProjectJson::toJson(doc->project()));
}

void TstUi::flowPropertyActionsAndEdgeSelection()
{
    MainWindow window;
    auto project = ProjectFactory::createFlowchart(QStringLiteral("流程工作表属性"));
    auto &flow = std::get<FlowSheet>(project.sheets.first());
    const ID source = flow.nodes.firstKey(), target = canvasId("property-edge-target");
    flow.nodes[source].tags = {QStringLiteral("待清理")};
    FlowNode second;
    second.id = target;
    second.title = QStringLiteral("后续步骤");
    second.position = {330, 0};
    flow.nodes.insert(target, second);
    FlowEdge edge;
    edge.id = canvasId("property-edge");
    edge.sourceId = source;
    edge.sourcePort = "right";
    edge.targetId = target;
    edge.targetPort = "left";
    edge.style = flow.defaultEdgeStyle;
    flow.edges.append(edge);
    loadEditorFixture(window, project);
    window.resize(1536, 960);
    window.show();
    QTest::qWait(30);
    chooseTopic(window, source);
    auto *doc = window.findChild<ProjectDocument *>();
    const auto current = [&]() -> const FlowSheet &
    { return std::get<FlowSheet>(doc->project().sheets.first()); };
    auto *tagAction = window.findChild<QAction *>("editNodeTags");
    auto *commentAction = window.findChild<QAction *>("editComments");
    auto *defaultStyle = window.findChild<QAction *>("editSheetStyle");
    QVERIFY(tagAction->isEnabled() && commentAction->isEnabled() && defaultStyle->isEnabled());
    runEditor(window, "editNodeTags", "nodeTagsDialog",
              [](QDialog *dialog)
              {
                  dialog->findChild<QPushButton *>("clearNodeTags")->click();
                  dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
              });
    QVERIFY(current().nodes.value(source).tags.isEmpty());
    runEditor(window, "editComments", "commentsDialog",
              [](QDialog *dialog)
              {
                  auto *body = dialog->findChild<QPlainTextEdit *>("commentBody");
                  auto *records = dialog->findChild<QListWidget *>("records");
                  body->setPlainText(QStringLiteral("检查流程节点"));
                  dialog->findChild<QPushButton *>("applyRecord")->click();
                  QCOMPARE(records->count(), 1);
                  QTest::mouseClick(records->viewport(), Qt::LeftButton, Qt::NoModifier,
                                    records->visualItemRect(records->item(0)).center());
                  dialog->findChild<QPushButton *>("removeRecord")->click();
              });
    QCOMPARE(current().comments.size(), 1);
    QCOMPARE(current().comments.first().nodeId, source);
    QVERIFY(current().comments.first().resolved);

    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    window.findChild<QAction *>("fitTopic")->trigger();
    const auto *line = flowLine(view, edge.id);
    QVERIFY(line);
    const QPoint center = view->mapFromScene(line->path().pointAtPercent(0.5));
    QVERIFY(view->viewport()->rect().contains(center));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, center);
    auto *fill = window.findChild<QComboBox *>("inlineFill");
    QVERIFY(!fill->isEnabled());
    QVERIFY(!tagAction->isEnabled() && !commentAction->isEnabled());
    QVERIFY(defaultStyle->isEnabled());
    runEditor(window, "editSheetStyle", "sheetStyleDialog",
              [](QDialog *dialog)
              {
                  dialog->findChild<QLineEdit *>("styleFill")->setText("#DDF2E7");
                  dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
              });
    QCOMPARE(current().defaultNodeStyle.fill, QString("#DDF2E7"));
    QVERIFY(!fill->isEnabled());
    QVERIFY(flowHandle(view, "target"));
    chooseTopic(window, target);
    QVERIFY(fill->isEnabled());
    QCOMPARE(canvasNodes(view).value(target)->nodeStyle().fill, QString("#DDF2E7"));
    doc->setClean();
}

void TstUi::propertyTabsFollowNodeAndTaskSelection()
{
    MainWindow window;
    auto project = canvasProject(2, 0);
    const auto &tree = std::get<TreeSheet>(project.sheets.first());
    const ID treeId = tree.id, root = tree.rootId;
    const auto makeSheet = [](const QString &kind)
    { return std::get<Sheet>(ProjectFactory::createSheet(kind, kind)); };
    const auto fish = makeSheet("fishbone"), flow = makeSheet("flowchart"),
               gantt = makeSheet("gantt");
    const ID fishId = std::get<TreeSheet>(fish).id, flowId = std::get<FlowSheet>(flow).id,
             ganttId = std::get<GanttSheet>(gantt).id;
    project.sheets << fish << flow << gantt;
    Task task;
    task.id = canvasId("other-node-task");
    task.title = QStringLiteral("根主题的任务");
    task.source = TaskSource{treeId, root};
    project.tasks.append(task);
    loadEditorFixture(window, project);
    window.resize(1536, 960);
    window.show();
    QTest::qWait(30);
    auto *doc = window.findChild<ProjectDocument *>();
    const auto revision = doc->sessionRevision();
    auto *selector = window.findChild<QComboBox *>("sheetSelector");
    auto *tabs = window.findChild<QTabWidget *>("propertyTabs");
    auto *summary = window.findChild<QLabel *>("nodeTaskSummary");
    auto *fill = window.findChild<QComboBox *>("inlineFill");
    auto *shape = window.findChild<QComboBox *>("inlineShape");
    auto *globalTask = window.findChild<QAction *>("editTaskProperties");
    chooseTopic(window, root);
    auto *taskTable = window.findChild<QTableView *>("taskTable");
    taskTable->setCurrentIndex(taskTable->model()->index(0, 0));
    QVERIFY(globalTask->isEnabled());
    tabs->setCurrentIndex(3);
    QVERIFY(summary->text().contains(task.title));
    chooseTopic(window, canvasId("branch-0"));
    QVERIFY(!summary->text().contains(task.title));
    auto *propertyTask = window.findChild<QAction *>("editPropertyTask");
    QVERIFY(propertyTask && !propertyTask->isEnabled());
    QVERIFY(globalTask->isEnabled());

    const auto switchTo = [selector](const ID &id)
    { selector->setCurrentIndex(selector->findData(id)); };
    switchTo(flowId);
    QVERIFY(fill->isEnabled() && shape->isHidden());
    QVERIFY(!tabs->isTabVisible(3));
    switchTo(ganttId);
    QCOMPARE(tabs->currentIndex(), 3);
    for (int index : {0, 1, 2})
        QVERIFY(!tabs->isTabVisible(index));
    QVERIFY(!fill->isEnabled());
    QVERIFY(!window.findChild<QAction *>("linkSelectedTask")->isEnabled());
    QVERIFY(propertyTask->isEnabled() && summary->text().contains(task.title));
    runEditor(window, "editPropertyTask", "taskDialog", [&](QDialog *dialog)
              { QCOMPARE(dialog->findChild<QLineEdit *>("taskTitle")->text(), task.title); });
    switchTo(fishId);
    QVERIFY(fill->isEnabled() && !shape->isHidden());
    for (int index : {0, 1, 2, 3})
        QVERIFY(tabs->isTabVisible(index));
    QVERIFY(!propertyTask->isEnabled());
    switchTo(treeId);
    QVERIFY(fill->isEnabled() && !shape->isHidden());
    QCOMPARE(doc->sessionRevision(), revision);
    QVERIFY(!window.findChild<CommandService *>()->canUndo());
    doc->setClean();
}

void TstUi::instantStyleWheelAndBrand()
{
    MainWindow window;
    loadEditorFixture(window, canvasProject(2, 0));
    window.show();
    window.resize(1366, 700);
    QTest::qWait(30);
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    QVERIFY(!window.findChild<QPushButton *>("applyInlineStyle"));
    auto *fill = window.findChild<QComboBox *>("inlineFill");
    const auto rev = doc->sessionRevision();
    fill->setCurrentIndex(fill->findData("#FFF0D0"));
    QCOMPARE(doc->sessionRevision(), rev + 1);
    const auto &tree = std::get<TreeSheet>(doc->project().sheets.first());
    QCOMPARE(tree.nodes[tree.rootId].style.fill.value(), QString("#FFF0D0"));
    const auto selected = fill->currentIndex();
    const auto previous = ProjectJson::toJson(doc->project());
    QWheelEvent wheel(QPointF(5, 5), fill->mapToGlobal(QPoint(5, 5)), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(fill, &wheel);
    QCOMPARE(fill->currentIndex(), selected);
    QCOMPARE(ProjectJson::toJson(doc->project()), previous);
    auto *bold = window.findChild<QCheckBox *>("inlineBold");
    bold->setCheckState(Qt::Checked);
    QCOMPARE(doc->sessionRevision(), rev + 2);
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), previous);
    auto *name = window.findChild<QLabel *>("brandName");
    auto *mark = window.findChild<QLabel *>("brandMark");
    QVERIFY(name && mark);
    const int gap = name->geometry().top() - mark->geometry().bottom() - 1;
    QVERIFY2(gap >= 0 && gap <= 6, qPrintable(QString::number(gap)));
    captureInteraction(window, "03-即时样式与品牌间距");
}
void TstUi::embeddedImagesAcrossViews_data()
{
    QTest::addColumn<QString>("kind");
    for (const auto *k : {"mindmap", "fishbone", "flowchart", "gantt"})
        QTest::newRow(k) << QString::fromLatin1(k);
}
void TstUi::embeddedImagesAcrossViews()
{
    QFETCH(QString, kind);
    MainWindow window;
    const auto made = ProjectFactory::createSheet(kind, "图片工作表");
    QVERIFY(std::holds_alternative<Sheet>(made));
    auto project = ProjectFactory::createMindmap("图片工程");
    project.sheets = {std::get<Sheet>(made)};
    project.activeSheetId = std::visit([](const auto &s) { return s.id; }, project.sheets.first());
    loadEditorFixture(window, project);
    window.show();
    QTest::qWait(30);
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    const auto png = tinyImage();
    ImageCommands commands(history, doc);
    const auto added = commands.add(project.activeSheetId, png, {160, 150});
    QVERIFY(std::holds_alternative<ID>(added));
    QGraphicsView *view = kind == "gantt"
                              ? static_cast<QGraphicsView *>(window.findChild<GanttView *>())
                              : window.findChild<QGraphicsView *>("topicPreview");
    QVERIFY(view);
    CanvasImageItem *image = nullptr;
    for (auto *item : view->scene()->items())
        if (auto *found = dynamic_cast<CanvasImageItem *>(item))
            image = found;
    QVERIFY(image);
    view->fitInView(view->scene()->itemsBoundingRect().adjusted(-100, -100, 100, 100),
                    Qt::KeepAspectRatio);
    const auto origin = image->pos();
    const QPoint start = view->mapFromScene(origin), end = start + QPoint(35, 25);
    const auto delta = view->mapToScene(end) - view->mapToScene(start);
    const auto rev = doc->sessionRevision();
    mouseDrag(view->viewport(), start, end);
    QCOMPARE(doc->sessionRevision(), rev + 1);
    const auto images =
        std::visit([](const auto &s) { return s.images; }, doc->project().sheets.first());
    QCOMPARE(images.first().position, origin + delta);
    history->undo();
    history->redo();
    image = nullptr;
    for (auto *item : view->scene()->items())
        if (auto *found = dynamic_cast<CanvasImageItem *>(item))
            image = found;
    QVERIFY(image);
    QVERIFY(image->isSelected());
    QGraphicsItem *handle = nullptr;
    for (auto *child : image->childItems())
        if (child->data(0x21C7).isValid())
            handle = child;
    QVERIFY(handle);
    const auto handleAt = view->mapFromScene(handle->sceneBoundingRect().center());
    const auto beforeResize = ProjectJson::toJson(doc->project());
    mouseDrag(view->viewport(), handleAt, handleAt + QPoint(15, 12));
    QVERIFY(ProjectJson::toJson(doc->project()) != beforeResize);
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), beforeResize);
    QTest::keyClick(view, Qt::Key_Delete);
    QVERIFY(std::visit([](const auto &s) { return s.images.isEmpty(); },
                       doc->project().sheets.first()));
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), beforeResize);
    if (kind != "gantt")
    {
        const ID node = kind == "flowchart"
                            ? std::get<FlowSheet>(doc->project().sheets.first()).nodes.firstKey()
                            : std::get<TreeSheet>(doc->project().sheets.first()).rootId;
        QVERIFY(
            std::holds_alternative<bool>(commands.setNodeImage(project.activeSheetId, node, png)));
        QVERIFY(canvasNodes(view)[node]->nodeRect().height() > 95);
    }
    QTemporaryDir files;
    const auto path = files.filePath("图片.lingya");
    QVERIFY(window.saveProjectTo(path));
    MainWindow reopened;
    QVERIFY(reopened.openFile(path));
    QCOMPARE(ProjectJson::toJson(reopened.findChild<ProjectDocument *>()->project()),
             ProjectJson::toJson(doc->project()));
    const auto dir = qEnvironmentVariable("LINGYA_INTERACTION_CAPTURE_DIR");
    if (!dir.isEmpty())
    {
        QDir().mkpath(dir);
        QVERIFY(window.grab().save(dir + "/图片-" + kind + ".png"));
    }
}
void TstUi::templateGalleryAndAiTutorial()
{
    MainWindow window;
    loadEditorFixture(window, canvasProject(2, 1));
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    const auto previous = ProjectJson::toJson(doc->project());
    runEditor(window, "showTemplateGallery", "templateGallery",
              [](QDialog *dialog)
              {
                  auto *tabs = dialog->findChild<QTabWidget *>("templateKinds");
                  QVERIFY(tabs);
                  for (int i = 0; i < tabs->count(); ++i)
                  {
                      tabs->setCurrentIndex(i);
                      QApplication::processEvents();
                      captureInteraction(*dialog, "模板库-" + tabs->tabText(i));
                  }
                  auto *button = dialog->findChild<QPushButton *>("applyStructure_down");
                  QVERIFY(button);
                  button->click();
              });
    QCOMPARE(std::get<TreeSheet>(doc->project().sheets.first()).layout, QString("down"));
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), previous);
    const auto count = doc->project().sheets.size();
    runEditor(window, "showTemplateGallery", "templateGallery",
              [](QDialog *dialog)
              {
                  auto *button = dialog->findChild<QToolButton *>("template_flow-cycle");
                  QVERIFY(button);
                  button->click();
              });
    QCOMPARE(doc->project().sheets.size(), count + 1);
    history->undo();
    QCOMPARE(ProjectJson::toJson(doc->project()), previous);
    SessionSecretStore store;
    FakeAiProvider provider;
    AiService service(&provider, &store);
    AiPanel panel(&service);
    QSignalSpy requests(&service, &AiService::stateChanged);
    bool shown = false;
    QTimer::singleShot(0, &panel,
                       [&]
                       {
                           auto *dialog = panel.findChild<QDialog *>("aiConnectionHelpDialog");
                           QVERIFY(dialog);
                           auto *text = dialog->findChild<QTextBrowser *>("aiConnectionHelpText");
                           QVERIFY(text);
                           QVERIFY(text->toPlainText().contains("401"));
                           QVERIFY(text->toPlainText().contains("保存密钥"));
                           captureInteraction(*dialog, "AI接入教程");
                           shown = true;
                           dialog->reject();
                       });
    panel.findChild<QPushButton *>("aiConnectionHelp")->click();
    QVERIFY(shown);
    QCOMPARE(requests.size(), 0);
    QCOMPARE(service.state(), AiService::State::Idle);
}
void TstUi::quickMouseRelationsGroupsAndImagePicker()
{
    const bool native = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard(
        [native] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native); });
    MainWindow window;
    loadEditorFixture(window, canvasProject(2, 0));
    window.show();
    QTest::qWait(30);
    auto *view = window.findChild<QGraphicsView *>("topicPreview");
    window.findChild<QAction *>("fitTopic")->trigger();
    auto *doc = window.findChild<ProjectDocument *>();
    auto *history = window.findChild<CommandService *>();
    const ID first = canvasId("branch-0"), second = canvasId("branch-1");
    auto at = [view](const ID &id) { return view->mapFromScene(canvasNodes(view)[id]->pos()); };
    contextAction(window, view, at(first), "quickRelation");
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, at(second));
    QCOMPARE(std::get<TreeSheet>(doc->project().sheets.first()).relations.size(), 1);
    history->undo();
    contextAction(window, view, at(first), "quickRelation");
    const auto rev = doc->sessionRevision();
    QTest::keyClick(view, Qt::Key_Escape);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, at(second));
    QCOMPARE(doc->sessionRevision(), rev);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, at(first));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, at(second));
    contextAction(window, view, at(first), "quickBoundary");
    QCOMPARE(std::get<TreeSheet>(doc->project().sheets.first()).annotations.size(), 1);
    QCOMPARE(
        std::get<TreeSheet>(doc->project().sheets.first()).annotations.first().memberIds.size(), 2);
    history->undo();
    contextAction(window, view, at(first), "quickSummary");
    QCOMPARE(std::get<TreeSheet>(doc->project().sheets.first()).annotations.first().kind,
             QString("summary"));
    history->undo();
    QTemporaryDir files;
    QImage input(120, 70, QImage::Format_ARGB32);
    input.fill(QColor("#4A9E83"));
    const auto original = files.filePath("鼠标插入原图.png");
    QVERIFY(input.save(original));
    QFile originalFile(original);
    QVERIFY(originalFile.open(QIODevice::ReadOnly));
    const auto originalBytes = originalFile.readAll();
    originalFile.close();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, at(first));
    answerImageFile(original);
    window.findChild<QAction *>("insertNodeImageAction")->trigger();
    const auto tree = std::get<TreeSheet>(doc->project().sheets.first());
    QVERIFY(!tree.nodes[first].imagePng.isEmpty());
    auto *node = canvasNodes(view)[first];
    QImage painted(node->boundingRect().size().toSize() + QSize(8, 8), QImage::Format_ARGB32);
    painted.fill(Qt::white);
    {
        QPainter painter(&painted);
        painter.translate(painted.width() / 2.0, painted.height() / 2.0);
        node->paintSnapshot(&painter);
    }
    int green = 0;
    for (int y = 0; y < painted.height(); ++y)
        for (int x = 0; x < painted.width(); ++x)
            green += painted.pixelColor(x, y) == QColor("#4A9E83");
    QVERIFY(green > 500);
    const auto before = ProjectJson::toJson(doc->project());
    answerImageFile(original, false);
    window.findChild<QAction *>("insertCanvasImage")->trigger();
    QCOMPARE(ProjectJson::toJson(doc->project()), before);
    const QPoint blank = view->viewport()->rect().bottomRight() - QPoint(55, 55);
    const auto position = view->mapToScene(blank);
    answerImageFile(original);
    contextAction(window, view, blank, "insertCanvasImageHere");
    const auto stored = std::get<TreeSheet>(doc->project().sheets.first()).images;
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.first().position, position);
    bool imageSelected = false;
    for (auto *item : view->scene()->items())
        if (auto *image = dynamic_cast<CanvasImageItem *>(item))
            imageSelected |= image->isSelected();
    QVERIFY(imageSelected);
    QVERIFY(!window.findChild<QAction *>("deleteTopic")->isEnabled());
    QVERIFY(originalFile.open(QIODevice::ReadOnly));
    QCOMPARE(originalFile.readAll(), originalBytes);
    originalFile.close();
    captureInteraction(window, "04-鼠标分组与图片");
    const auto exported = files.filePath("含嵌入图.png");
    QTimer::singleShot(
        0, &window,
        [&]
        {
            auto *dialog = window.findChild<QDialog *>("exportDialog");
            QVERIFY(dialog);
            answerImageFile(exported);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
    window.findChild<QAction *>("exportSheet")->trigger();
    QImage output(exported);
    QVERIFY(!output.isNull());
    green = 0;
    for (int y = 0; y < output.height(); ++y)
        for (int x = 0; x < output.width(); ++x)
            green += output.pixelColor(x, y) == QColor("#4A9E83");
    QVERIFY(green > 1000);
    loadEditorFixture(window, ProjectFactory::createFishbone(QStringLiteral("鱼骨快捷添加")));
    window.findChild<QAction *>("fitTopic")->trigger();
    const auto fish = std::get<TreeSheet>(doc->project().sheets.first());
    contextAction(window, view, view->viewport()->rect().topRight() + QPoint(-20, 20),
                  "createTopicHere");
    const auto after = std::get<TreeSheet>(doc->project().sheets.first());
    QCOMPARE(after.nodes.size(), fish.nodes.size() + 1);
    for (const auto &added : after.nodes)
        QVERIFY(!added.manualPosition.has_value());
}
