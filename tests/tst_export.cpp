#include "domain/ProjectFactory.h"
#include "graphics/NodeItem.h"
#include "services/ExportService.h"
#include "services/ProjectJson.h"
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
using namespace lingya;
class TstExport : public QObject
{
    Q_OBJECT
  private slots:
    void sizeLimits();
    void pngPixelsAndFileProtection();
    void pdfPagination();
    void outlineIncludesCollapsedAndNotes();
    void nodeSnapshotIgnoresSelection();
};
void TstExport::sizeLimits()
{
    using namespace ExportService;
    QCOMPARE(std::get<QSize>(pngSize({-20, -10, 100, 200}, 2)), QSize(200, 400));
    QVERIFY(std::holds_alternative<AppError>(pngSize({0, 0, 8000, 8000}, 1)));
    QVERIFY(std::holds_alternative<AppError>(pngSize({0, 0, 1e20, 1e20}, 1)));
    QVERIFY(std::holds_alternative<AppError>(pngSize({0, 0, 0, 1}, 1)));
    QVERIFY(std::holds_alternative<AppError>(
        pngSize({0, 0, 10, 10}, std::numeric_limits<double>::quiet_NaN())));
    QVERIFY(std::holds_alternative<AppError>(pdfPageCount({0, 0, 1e10, 1e10})));
}
void TstExport::pngPixelsAndFileProtection()
{
    QTemporaryDir dir;
    ExportService::Snapshot snapshot;
    snapshot.bounds = {-10, -20, 100, 80};
    QPainter painter(&snapshot.picture);
    painter.fillRect(snapshot.bounds, QColor("#247A62"));
    painter.end();
    const QString path = dir.filePath("中文 图片.png");
    QVERIFY(std::holds_alternative<bool>(ExportService::writePng(snapshot, path)));
    QImage image(path);
    QCOMPARE(image.size(), QSize(100, 80));
    QCOMPARE(image.pixelColor(50, 40), QColor("#247A62"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto before = file.readAll();
    file.close();
    QVERIFY(std::holds_alternative<AppError>(ExportService::writePng(snapshot, path, 0)));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), before);
    const QString project = dir.filePath("工程.lingya");
    QVERIFY(std::holds_alternative<AppError>(ExportService::writePng(snapshot, project)));
    QVERIFY(!QFile::exists(project));
    QVERIFY(std::holds_alternative<AppError>(ExportService::writeText("文本", project)));
    QVERIFY(!QFile::exists(project));
    QVERIFY(std::holds_alternative<AppError>(
        ExportService::writeText("文本", dir.filePath("不存在/文稿.txt"))));
}
void TstExport::pdfPagination()
{
    QTemporaryDir dir;
    ExportService::Snapshot snapshot;
    snapshot.bounds = {0, 0, 2000, 1100};
    QPainter painter(&snapshot.picture);
    painter.drawText(10, 40, QStringLiteral("中文分页导出"));
    painter.end();
    const auto count = ExportService::pdfPageCount(snapshot.bounds);
    QVERIFY(std::holds_alternative<int>(count));
    QVERIFY(std::get<int>(count) > 1);
    const QString path = dir.filePath("分页.pdf");
    const auto result = ExportService::writePdf(snapshot, path);
    QVERIFY(std::holds_alternative<int>(result));
    QCOMPARE(std::get<int>(result), std::get<int>(count));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY(bytes.startsWith("%PDF-"));
    QVERIFY(bytes.contains("/Count " + QByteArray::number(std::get<int>(count))));
}
void TstExport::outlineIncludesCollapsedAndNotes()
{
    const auto project = ProjectFactory::createFishbone("中文导出");
    const auto &tree = std::get<TreeSheet>(project.sheets.first());
    const auto json = ProjectJson::toJson(project);
    const auto text = ExportService::outline(project, tree.id);
    QVERIFY(std::holds_alternative<QString>(text));
    for (const auto &node : tree.nodes)
        QVERIFY(std::get<QString>(text).contains(node.title));
    QTemporaryDir dir;
    const QString path = dir.filePath("大纲.md");
    QVERIFY(std::holds_alternative<bool>(ExportService::writeText(std::get<QString>(text), path)));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), std::get<QString>(text).toUtf8());
    QCOMPARE(ProjectJson::toJson(project), json);
    QVERIFY(std::holds_alternative<AppError>(ExportService::outline(project, "absent")));
}
void TstExport::nodeSnapshotIgnoresSelection()
{
    NodeItem node("中文主题", QFont("Microsoft YaHei", 12));
    const auto paint = [&]
    {
        QImage image(400, 150, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.translate(200, 75);
        node.paintSnapshot(&painter);
        painter.end();
        return image;
    };
    const auto initial = paint();
    node.setSelected(true);
    node.setCutPending(true);
    QCOMPARE(paint(), initial);
    QVERIFY(node.isSelected());
}
QTEST_MAIN(TstExport)
#include "tst_export.moc"
