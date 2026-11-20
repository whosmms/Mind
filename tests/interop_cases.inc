void TstUi::importExternalMapRoundTrip_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("title");
    QTest::addColumn<int>("sheets");
    QTest::newRow("XMind") << QStringLiteral("modern.xmind") << QStringLiteral("课程设计") << 2;
    QTest::newRow("OPML") << QStringLiteral("outline.opml") << QStringLiteral("学习计划") << 1;
    QTest::newRow("FreeMind") << QStringLiteral("mind.mm") << QStringLiteral("章节 复习") << 1;
}

void TstUi::importExternalMapRoundTrip()
{
    QFETCH(QString, fileName);
    QFETCH(QString, title);
    QFETCH(int, sheets);
    const bool nativeDialog = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restoreDialogs =
        qScopeGuard([nativeDialog]
                    { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialog); });
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const auto original = QFINDTESTDATA(QStringLiteral("fixtures/import/") + fileName);
    QVERIFY(!original.isEmpty());
    const auto path = files.filePath(fileName);
    QVERIFY(QFile::copy(original, path));
    const auto source = FileService::stamp(path);

    MainWindow window;
    auto *document = window.findChild<ProjectDocument *>();
    window.show();
    QVERIFY(window.renameRootTo(QStringLiteral("旧工程未保存的编辑")));
    const auto before = ProjectJson::toJson(document->project());
    answerDeletion(QStringLiteral("取消"));
    QVERIFY(!window.openFile(path));
    QCOMPARE(ProjectJson::toJson(document->project()), before);

    document->setClean();
    window.showHomePage();
    auto *action = window.findChild<QAction *>("importMindMap");
    QVERIFY(action && action->isEnabled());
    QTimer responder;
    responder.setInterval(10);
    connect(&responder, &QTimer::timeout, &window,
            [&]
            {
                auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                {
                    return;
                }
                responder.stop();
                dialog->selectFile(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                if (dialog->isVisible())
                {
                    dialog->reject();
                }
            });
    responder.start();
    action->trigger();
    responder.stop();
    QCOMPARE(window.currentRootTitle(), title);
    QCOMPARE(document->project().sheets.size(), sheets);
    QVERIFY(document->isModified());
    QVERIFY(!window.findChild<CommandService *>()->canUndo());
    const auto target = files.filePath("imported.lingya");
    const auto imported = ProjectJson::toJson(document->project());
    QVERIFY(window.saveProjectTo(target));
    QCOMPARE(std::get<FileService::FileStamp>(FileService::stamp(path)).sha256,
             std::get<FileService::FileStamp>(source).sha256);
    MainWindow reopened;
    QVERIFY(reopened.openFile(target));
    QCOMPARE(ProjectJson::toJson(reopened.findChild<ProjectDocument *>()->project()), imported);
    const auto capture = qEnvironmentVariable("LINGYA_H059_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(window.grab().save(QDir(capture).filePath(fileName + ".png")));
}

void TstUi::svgExportFromEditor()
{
    const bool native = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard(
        [native] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native); });
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const auto capture = qEnvironmentVariable("LINGYA_H059_CAPTURE_DIR");
    const auto path =
        QDir(capture.isEmpty() ? files.path() : capture).filePath(QStringLiteral("工作表.svg"));
    MainWindow window;
    window.show();
    loadEditorFixture(window, canvasProject(2, 1));
    auto *document = window.findChild<ProjectDocument *>();
    const auto before = ProjectJson::toJson(document->project());
    const auto revision = document->sessionRevision();
    QSignalSpy changes(document, &ProjectDocument::changed);
    QTimer::singleShot(
        0, &window,
        [&]
        {
            auto *dialog = window.findChild<QDialog *>("exportDialog");
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto *formats = dialog->findChild<QComboBox *>("exportFormat");
            const int index = formats->findData("svg");
            QVERIFY(index >= 0);
            formats->setCurrentIndex(index);
            QVERIFY(dialog->findChild<QLabel *>("exportInfo")->text().contains("SVG"));
            if (!capture.isEmpty())
                QVERIFY(dialog->grab().save(QDir(capture).filePath("SVG导出.png")));
            QTimer::singleShot(
                0, dialog,
                [&]
                {
                    auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                    QVERIFY(picker);
                    picker->selectFile(path);
                    QVERIFY(QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection));
                    if (picker->isVisible())
                        picker->reject();
                });
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
    window.findChild<QAction *>("exportSheet")->trigger();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY(bytes.contains("<svg"));
    QVERIFY(bytes.contains("<path"));
    QCOMPARE(changes.count(), 0);
    QCOMPARE(document->sessionRevision(), revision);
    QCOMPARE(ProjectJson::toJson(document->project()), before);
}

void TstUi::searchReplaceEditorWorkflow()
{
    auto project = canvasProject(2, 0);
    auto &tree = std::get<TreeSheet>(project.sheets.first());
    const auto id = tree.rootId;
    tree.nodes[id].title = QStringLiteral("阶段：旧设计");
    tree.nodes[id].note = QStringLiteral("复习旧设计");
    for (auto &node : tree.nodes)
        if (node.id != id)
            node.title = QStringLiteral("旧设计分支");
    MainWindow window;
    window.show();
    loadEditorFixture(window, project);
    auto *document = window.findChild<ProjectDocument *>();
    const auto before = ProjectJson::toJson(document->project());
    const auto revision = document->sessionRevision();
    const auto capture = qEnvironmentVariable("LINGYA_H059_CAPTURE_DIR");
    QTimer::singleShot(
        0, &window,
        [&]
        {
            auto *dialog = window.findChild<QDialog *>("projectSearch");
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto *query = dialog->findChild<QLineEdit *>("searchQuery");
            auto *replacement = dialog->findChild<QLineEdit *>("searchReplacement");
            auto *list = dialog->findChild<QListWidget *>("searchResults");
            auto *all = dialog->findChild<QPushButton *>("replaceAllSearchResults");
            auto *selected = dialog->findChild<QPushButton *>("replaceSearchSelection");
            QVERIFY(query && replacement && list && all && selected);
            QVERIFY(!all->isEnabled());
            query->setText(QStringLiteral("旧设计"));
            replacement->setText(QStringLiteral("新方案"));
            QCOMPARE(list->count(), 3);
            if (!capture.isEmpty())
                QVERIFY(dialog->grab().save(QDir(capture).filePath("查找和替换.png")));
            answerDeletion(QStringLiteral("取消"));
            all->click();
            QCOMPARE(ProjectJson::toJson(document->project()), before);
            QCOMPARE(document->sessionRevision(), revision);
            answerDeletion(QStringLiteral("全部替换"));
            all->click();
            QCOMPARE(document->sessionRevision(), revision + 1);
            QCOMPARE(list->count(), 0);
            QCOMPARE(std::get<TreeSheet>(document->project().sheets.first()).nodes.value(id).note,
                     QStringLiteral("复习新方案"));
            auto *commands = window.findChild<CommandService *>();
            commands->undo();
            QCOMPARE(ProjectJson::toJson(document->project()), before);
            QVERIFY(!commands->canUndo());
            for (int row = 0; row < list->count(); ++row)
                if (list->item(row)->text().contains(QStringLiteral("阶段：旧设计")))
                    list->setCurrentRow(row);
            selected->click();
            QCOMPARE(list->count(), 2);
            QCOMPARE(window.currentRootTitle(), QStringLiteral("阶段：新方案"));
        });
    window.findChild<QAction *>("showSearch")->trigger();
    QCOMPARE(window.currentRootTitle(), QStringLiteral("阶段：新方案"));
}

void TstUi::presentationScreenSeparation()
{
    auto project = canvasProject(2, 1);
    const auto &tree = std::get<TreeSheet>(project.sheets.first());
    auto route = PresentationController::defaultRoute(tree);
    route.steps[0].speakerNote = QStringLiteral("演讲者私有备注：请先提问");
    route.steps[1].speakerNote = QStringLiteral("第二步只在演讲者屏显示");
    project.routes.append(route);
    const auto displays = QGuiApplication::screens();
    QVERIFY(!displays.isEmpty());
    MainWindow window;
    window.resize(1040, 720);
    window.move(displays.first()->geometry().topLeft() + QPoint(48, 48));
    window.show();
    loadEditorFixture(window, project);
    auto *document = window.findChild<ProjectDocument *>();
    auto *editor = window.findChild<QGraphicsView *>("topicPreview");
    const auto before = ProjectJson::toJson(document->project());
    const auto transform = editor->transform();
    const auto revision = document->sessionRevision();
    QSignalSpy changes(document, &ProjectDocument::changed);
    bool played = false;
    QTimer::singleShot(
        0, &window,
        [&]
        {
            auto *routes = window.findChild<QDialog *>("presentationRoutes");
            QVERIFY(routes);
            const auto close = qScopeGuard([&] { routes->reject(); });
            auto *output = routes->findChild<QComboBox *>("presentationOutput");
            QVERIFY(output);
            QCOMPARE(output->count(), displays.size());
            if (displays.size() == 1)
            {
                QCOMPARE(output->currentText(), QStringLiteral("单屏讲解"));
                QVERIFY(!output->isEnabled());
                return;
            }
            auto *presenterScreen = routes->screen();
            output->setCurrentIndex(1);
            routes->findChild<QComboBox *>("presentationRouteChoice")->setCurrentIndex(1);
            QTimer::singleShot(
                0, &window,
                [&, presenterScreen]
                {
                    auto *player = window.findChild<QDialog *>("presentationPlayer");
                    QVERIFY(player && player->isVisible());
                    const auto exit = qScopeGuard([&] { player->reject(); });
                    auto *audience = window.findChild<QDialog *>("presentationAudience");
                    QVERIFY(audience && audience->isVisible());
                    QCOMPARE(player->screen(), presenterScreen);
                    QVERIFY(audience->screen() != presenterScreen);
                    QVERIFY(player->isFullScreen());
                    QVERIFY(audience->isFullScreen());
                    QVERIFY(presenterScreen->geometry().contains(player->geometry().center()));
                    QVERIFY(audience->screen()->geometry().contains(audience->geometry().center()));
                    QVERIFY(audience->findChildren<QLabel *>().isEmpty());
                    QVERIFY(audience->findChildren<QAbstractButton *>().isEmpty());
                    auto *view = audience->findChild<QGraphicsView *>("presentationAudienceCanvas");
                    QVERIFY(view);
                    QCOMPARE(view->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
                    QCOMPARE(view->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
                    const auto note = player->findChild<QLabel *>("presentationVisibleNote");
                    const auto next = player->findChild<QLabel *>("presentationFollowing");
                    const auto timer = player->findChild<QLabel *>("presentationTime");
                    QVERIFY(note && next);
                    QVERIFY(timer && !timer->text().isEmpty());
                    QCOMPARE(note->text(), route.steps[0].speakerNote);
                    QVERIFY(note->isVisible());
                    QCOMPARE(next->text(), QStringLiteral("下一步  ·  %1")
                                               .arg(tree.nodes.value(route.steps[1].nodeId).title));
                    QTest::qWait(20);
                    const auto initialAudience = view->grab().toImage();
                    QTest::keyClick(player, Qt::Key_Right);
                    QCOMPARE(note->text(), route.steps[1].speakerNote);
                    QVERIFY(view->grab().toImage() != initialAudience);
                    auto *pause = player->findChild<QPushButton *>("presentationPause");
                    QTest::keyClick(player, Qt::Key_Space);
                    QCOMPARE(pause->text(), QStringLiteral("继续计时"));
                    const auto capture = qEnvironmentVariable("LINGYA_H059_CAPTURE_DIR");
                    if (!capture.isEmpty())
                    {
                        QVERIFY(player->grab().save(QDir(capture).filePath("演讲者屏.png")));
                        QVERIFY(audience->grab().save(QDir(capture).filePath("观众屏.png")));
                    }
                    QTest::keyClick(player, Qt::Key_Right);
                    QCOMPARE(next->text(), QStringLiteral("已到最后一步"));
                    QTest::keyClick(player, Qt::Key_Escape);
                    QVERIFY(!player->isVisible());
                    QVERIFY(!audience->isVisible());
                    played = true;
                });
            routes->findChild<QPushButton *>("playPresentation")->click();
        });
    window.findChild<QAction *>("managePresentation")->trigger();
    QCOMPARE(played, displays.size() > 1);
    QCOMPARE(changes.count(), 0);
    QCOMPARE(document->sessionRevision(), revision);
    QCOMPARE(ProjectJson::toJson(document->project()), before);
    QCOMPARE(editor->transform(), transform);
}
