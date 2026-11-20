#include "services/FileService.h"
#include "services/ImageService.h"
#include "services/MindMapImport.h"
#include "services/ProjectJson.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace lingya;

namespace
{
QString fixture(const char *name)
{
    return QStringLiteral(FIXTURES_DIR "/import/") + QString::fromLatin1(name);
}

TreeNode topic(const TreeSheet &sheet, const QString &title)
{
    for (const auto &node : sheet.nodes)
    {
        if (node.title == title)
        {
            return node;
        }
    }
    return {};
}
} // namespace

class TstImport : public QObject
{
    Q_OBJECT
  private slots:
    void modernXMindPreservesSheetsAndNotes();
    void legacyXMindReadsXmlAndStoredEntries();
    void corruptArchiveIsRejected_data();
    void corruptArchiveIsRejected();
    void opmlReadsOutlineAndEncodedMarkup();
    void freeMindReadsRichNodesAndFoldedBranches();
    void invalidXmlLeavesNoPartialProject_data();
    void invalidXmlLeavesNoPartialProject();
};

void TstImport::modernXMindPreservesSheetsAndNotes()
{
    const auto loaded = MindMapImport::load(fixture("modern.xmind"));
    if (const auto *error = std::get_if<AppError>(&loaded))
    {
        QFAIL(qPrintable(error->message));
    }
    const auto &result = std::get<MindMapImport::Result>(loaded);
    QCOMPARE(result.project.sheets.size(), 2);
    QCOMPARE(result.topics, 6);
    QCOMPARE(result.skippedImages, 1);
    const auto &first = std::get<TreeSheet>(result.project.sheets.at(0));
    QCOMPARE(first.id, result.project.activeSheetId);
    QCOMPARE(first.name, QStringLiteral("课程总览"));
    QCOMPARE(first.layout, QStringLiteral("right"));
    const auto root = first.nodes.value(first.rootId);
    QCOMPARE(root.title, QStringLiteral("课程设计"));
    QCOMPARE(root.note, QStringLiteral("第一段\n第二段"));
    QCOMPARE(root.tags, (QStringList{QStringLiteral("重点"), QStringLiteral("已完成")}));
    QCOMPARE(root.link, std::optional<QString>("https://example.org/abc?x=1&y=2"));
    const auto task = topic(first, QStringLiteral("资料整理"));
    const auto verification = topic(first, QStringLiteral("验收"));
    QCOMPARE(task.parentId, std::optional<ID>(root.id));
    QCOMPARE(task.order, 0);
    QCOMPARE(verification.order, 1);
    QVERIFY(!verification.link);
    QVERIFY(task.collapsed);
    QCOMPARE(ImageService::decode(task.imagePng).size(), QSize(2, 2));
    QCOMPARE(topic(first, QStringLiteral("中文细节")).parentId, std::optional<ID>(task.id));
    QCOMPARE(topic(first, QStringLiteral("浮动备忘")).order, 2);
    QCOMPARE(first.relations.size(), 1);
    QCOMPARE(first.relations.first().sourceId, task.id);
    QCOMPARE(first.relations.first().targetId, verification.id);
    QCOMPARE(first.relations.first().label, QStringLiteral("先后顺序"));
    const auto &second = std::get<TreeSheet>(result.project.sheets.at(1));
    QCOMPARE(second.name, QStringLiteral("下一课"));
    QCOMPARE(second.nodes.value(second.rootId).title, QStringLiteral("下一课题"));

    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const auto path = folder.filePath("roundtrip.lingya");
    QVERIFY(std::holds_alternative<qint64>(FileService::save(path, result.project)));
    const auto reopened = FileService::load(path);
    QVERIFY(std::holds_alternative<Project>(reopened));
    QCOMPARE(ProjectJson::toJson(std::get<Project>(reopened)), ProjectJson::toJson(result.project));
}

void TstImport::legacyXMindReadsXmlAndStoredEntries()
{
    const auto loaded = MindMapImport::load(fixture("legacy.xmind"));
    if (const auto *error = std::get_if<AppError>(&loaded))
    {
        QFAIL(qPrintable(error->message));
    }
    const auto &result = std::get<MindMapImport::Result>(loaded);
    QCOMPARE(result.topics, 3);
    const auto &sheet = std::get<TreeSheet>(result.project.sheets.first());
    QCOMPARE(sheet.name, QStringLiteral("旧版课件"));
    QCOMPARE(sheet.layout, QStringLiteral("down"));
    QCOMPARE(sheet.nodes.value(sheet.rootId).title, QStringLiteral("汉字 & 科学"));
    QCOMPARE(sheet.nodes.value(sheet.rootId).note, QStringLiteral("旧版备注\n保留换行"));
    QCOMPARE(topic(sheet, QStringLiteral("左分支")).link,
             std::optional<QString>("https://example.org"));
    QCOMPARE(ImageService::decode(topic(sheet, QStringLiteral("右分支")).imagePng).size(),
             QSize(2, 2));
    QCOMPARE(sheet.relations.size(), 1);
}

void TstImport::corruptArchiveIsRejected_data()
{
    QTest::addColumn<bool>("truncate");
    QTest::newRow("截断") << true;
    QTest::newRow("校验不符") << false;
}

void TstImport::corruptArchiveIsRejected()
{
    QFETCH(bool, truncate);
    QFile original(fixture("legacy.xmind"));
    QVERIFY(original.open(QIODevice::ReadOnly));
    auto bytes = original.readAll();
    if (truncate)
    {
        bytes.chop(8);
    }
    else
    {
        const auto content = bytes.indexOf("xmap-content");
        QVERIFY(content >= 0);
        bytes[content] = 'y';
    }
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    QFile invalidFile(folder.filePath("broken.xmind"));
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    QCOMPARE(invalidFile.write(bytes), bytes.size());
    invalidFile.close();
    const auto loaded = MindMapImport::load(invalidFile.fileName());
    QVERIFY(std::holds_alternative<AppError>(loaded));
    QCOMPARE(std::get<AppError>(loaded).code, QStringLiteral("INVALID_PROJECT"));
}

void TstImport::opmlReadsOutlineAndEncodedMarkup()
{
    const auto loaded = MindMapImport::load(fixture("outline.opml"));
    QVERIFY(std::holds_alternative<MindMapImport::Result>(loaded));
    const auto &result = std::get<MindMapImport::Result>(loaded);
    QCOMPARE(result.topics, 4);
    const auto &sheet = std::get<TreeSheet>(result.project.sheets.first());
    QCOMPARE(sheet.name, QStringLiteral("学习计划"));
    const auto root = sheet.nodes.value(sheet.rootId);
    QCOMPARE(root.title, sheet.name);
    const auto first = topic(sheet, QStringLiteral("第一章 & 准备"));
    QCOMPARE(first.note, QStringLiteral("行一\n行二"));
    QCOMPARE(first.link, std::optional<QString>("https://example.org/notes"));
    QCOMPARE(first.order, 0);
    const auto second = topic(sheet, QStringLiteral("第二章"));
    QCOMPARE(second.order, 1);
    QCOMPARE(second.link, std::optional<QString>("https://example.org/feed.xml"));
    QCOMPARE(topic(sheet, QStringLiteral("中文练习")).parentId, std::optional<ID>(first.id));
}

void TstImport::freeMindReadsRichNodesAndFoldedBranches()
{
    const auto loaded = MindMapImport::load(fixture("mind.mm"));
    QVERIFY(std::holds_alternative<MindMapImport::Result>(loaded));
    const auto &result = std::get<MindMapImport::Result>(loaded);
    QCOMPARE(result.topics, 4);
    const auto &sheet = std::get<TreeSheet>(result.project.sheets.first());
    const auto root = sheet.nodes.value(sheet.rootId);
    QCOMPARE(root.title, QStringLiteral("章节 复习"));
    QCOMPARE(root.note, QStringLiteral("备注第一行\n下一行\n第二段 & 样例"));
    const auto left = topic(sheet, QStringLiteral("左侧"));
    const auto right = topic(sheet, QStringLiteral("右侧"));
    QCOMPARE(left.side, QStringLiteral("left"));
    QCOMPARE(right.side, QStringLiteral("right"));
    QCOMPARE(right.link, std::optional<QString>("https://example.org/page"));
    QVERIFY(left.collapsed);
    QCOMPARE(topic(sheet, QStringLiteral("折叠细节")).parentId, std::optional<ID>(left.id));
    QCOMPARE(sheet.relations.size(), 1);
    QCOMPARE(sheet.relations.first().targetId, left.id);
}

void TstImport::invalidXmlLeavesNoPartialProject_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("error");
    QTest::newRow("大纲缺少标题")
        << QStringLiteral("missing.opml")
        << QByteArray("<opml version='2.0'><head/><body><outline/></body></opml>")
        << QStringLiteral("INVALID_PROJECT");
    QTest::newRow("XML 未闭合") << QStringLiteral("incomplete.mm")
                                << QByteArray("<map><node TEXT='root'/><node")
                                << QStringLiteral("INVALID_PROJECT");
    QTest::newRow("文档类型声明") << QStringLiteral("dtd.mm")
                                  << QByteArray("<!DOCTYPE map><map><node TEXT='root'/></map>")
                                  << QStringLiteral("INVALID_PROJECT");
    QTest::newRow("分支过深") << QStringLiteral("deep.mm")
                              << QByteArray("<map>" +
                                            QByteArray("<node TEXT='next'>").repeated(129) +
                                            QByteArray("</node>").repeated(129) + "</map>")
                              << QStringLiteral("LIMIT_EXCEEDED");
}

void TstImport::invalidXmlLeavesNoPartialProject()
{
    QFETCH(QString, name);
    QFETCH(QByteArray, bytes);
    QFETCH(QString, error);
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    QFile file(folder.filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
    const auto loaded = MindMapImport::load(file.fileName());
    QVERIFY(std::holds_alternative<AppError>(loaded));
    QCOMPARE(std::get<AppError>(loaded).code, error);
}

QTEST_MAIN(TstImport)
#include "tst_import.moc"
