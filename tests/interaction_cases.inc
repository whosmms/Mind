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
void contextAction(MainWindow &window, QGraphicsView *view, QPoint at, const QString &name)
{
    QTimer::singleShot(0, &window,
                       [&window, name]
                       {
                           auto *menu = window.findChild<QMenu *>("canvasContextMenu");
                           QVERIFY(menu);
                           const auto close = qScopeGuard([menu] { menu->close(); });
                           QAction *chosen = nullptr;
                           for (auto *action : menu->actions())
                               if (action->objectName() == name)
                                   chosen = action;
                           QVERIFY(chosen);
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
void captureEfficiency(QWidget &widget, const QString &name)
{
    const auto directory = qEnvironmentVariable("LINGYA_H052_CAPTURE_DIR");
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
} // namespace
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
    QTest::addColumn<int>("turns");
    QTest::addColumn<bool>("blocked");
    QTest::newRow("horizontal") << QPointF(330, 0) << QString("right") << QString("left") << 0
                                << false;
    QTest::newRow("horizontal-reverse")
        << QPointF(-330, 0) << QString("left") << QString("right") << 0 << false;
    QTest::newRow("short-gap") << QPointF(140, 0) << QString("right") << QString("left") << 0
                               << false;
    QTest::newRow("vertical") << QPointF(0, 250) << QString("bottom") << QString("top") << 0
                              << false;
    QTest::newRow("vertical-reverse")
        << QPointF(0, -250) << QString("top") << QString("bottom") << 0 << false;
    QTest::newRow("perpendicular")
        << QPointF(330, 200) << QString("right") << QString("top") << 1 << false;
    QTest::newRow("offset") << QPointF(330, 120) << QString("right") << QString("left") << 2
                            << false;
    QTest::newRow("same-side") << QPointF(330, 0) << QString("top") << QString("top") << 2 << false;
    QTest::newRow("obstacle") << QPointF(500, 0) << QString("right") << QString("left") << 4
                              << true;
}

void TstUi::flowAutoRoutes()
{
    QFETCH(QPointF, position);
    QFETCH(QString, sourcePort);
    QFETCH(QString, targetPort);
    QFETCH(int, turns);
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
    QCOMPARE(path.elementCount(), turns + 2);
    QCOMPARE(QPointF(path.elementAt(0)), start);
    QCOMPARE(path.currentPosition(), finish);
    for (int i = 1; i < path.elementCount(); ++i)
    {
        QVERIFY(path.elementAt(i).isLineTo());
        const QPointF previous = path.elementAt(i - 1), point = path.elementAt(i);
        QVERIFY((point.x() == previous.x()) != (point.y() == previous.y()));
        if (blocked)
            QVERIFY(!QRectF(previous, point)
                         .normalized()
                         .adjusted(-0.1, -0.1, 0.1, 0.1)
                         .intersects(canvasNodes(view).value(blocker)->sceneBoundingRect()));
    }
    if (blocked)
    {
        QVERIFY(path.boundingRect().right() <= finish.x());
        captureInteraction(window, "流程自动避障");
    }
    else if (QByteArray(QTest::currentDataTag()) == "horizontal")
        captureInteraction(window, "端口水平直连");
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
