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
QPoint portPoint(QGraphicsView *view, const ID &id, const QString &port)
{
    const NodeItem *item = nullptr;
    for (auto *value : view->scene()->items())
        if (auto *n = dynamic_cast<NodeItem *>(value); n && n->nodeId() == id)
        {
            item = n;
            break;
        }
    if (!item)
        return {};
    const auto r = item->nodeRect();
    const QPointF point = port == "left"    ? QPointF(r.left(), 0)
                          : port == "right" ? QPointF(r.right(), 0)
                          : port == "top"   ? QPointF(0, r.top())
                                            : QPointF(0, r.bottom());
    return view->mapFromScene(item->mapToScene(point));
}
QGraphicsItem *flowHandle(QGraphicsView *view, const QString &kind)
{
    for (auto *item : view->scene()->items())
        if (item->data(0x21C3).toString() == kind)
            return item;
    return nullptr;
}
} // namespace
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
void TstUi::flowCurveMouseReconnect()
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
    QGraphicsPathItem *path = nullptr;
    for (auto *item : view->scene()->items())
        if (!item->parentItem() && item->data(0x21C1).toString() == edgeId)
            path = dynamic_cast<QGraphicsPathItem *>(item);
    QVERIFY(path);
    const QPoint center = view->mapFromScene(path->path().pointAtPercent(0.5));
    const auto rev = doc->sessionRevision();
    mouseDrag(view->viewport(), center, center + QPoint(70, 0));
    QCOMPARE(doc->sessionRevision(), rev + 1);
    QVERIFY(std::get<FlowSheet>(doc->project().sheets.first()).edges.first().bendPoint);
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
    QVERIFY(window.saveProjectTo(files.filePath("曲线.lingya")));
    MainWindow reopened;
    QVERIFY(reopened.openFile(files.filePath("曲线.lingya")));
    QCOMPARE(ProjectJson::toJson(reopened.findChild<ProjectDocument *>()->project()),
             ProjectJson::toJson(doc->project()));
    captureInteraction(window, "02-曲线与端点重连");
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
}
