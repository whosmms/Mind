#pragma once
// 所有领域值类型（规范§4.2“C++领域类型与JSON映射”的字段契约，逐字采用）
// 约束：领域对象按值存储，不持有QWidget、QGraphicsItem或QObject指针（规范§2.3）。
// 枚举的QString代码值只允许规范§4.2列出的取值，由ProjectValidator与ProjectJson严格验证；
// 页面显示使用中文名称。ProjectJson显式逐字段读写，不序列化内存地址或对象二进制。
#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>
#include <variant>

namespace lingya {


using ID = QString;
struct AppError { QString code, message; };
struct NodeStyle {
    QString fill, color, borderColor;
    int fontSize = 16;
    QString fontFamily = "system";
    bool bold = false;
    QString shape = "roundRect";
    double borderWidth = 1.5;
};
struct NodeStyleOverride {
    std::optional<QString> fill, color, borderColor;
    std::optional<int> fontSize;
    std::optional<QString> fontFamily, shape;
    std::optional<bool> bold;
    std::optional<double> borderWidth;
};
struct EdgeStyle {
    QString color;
    double width = 1.5;
    bool dashed = false;
};
struct Provenance {
    QString kind = "ai", requestId, model;
    QStringList sourceParagraphIds;
    QString origin = "source";
};
struct BaseNode {
    ID id;
    QString title, note;
    std::optional<QString> iconId, link;
    QStringList tags;
    NodeStyleOverride style;
    qint64 contentRevision = 0;
    QString imagePng; // 可选嵌入PNG的Base64；空串不序列化。
    std::optional<Provenance> provenance;
};
struct TreeNode : BaseNode {
    std::optional<ID> parentId;
    int order = 0;
    bool collapsed = false;
    QString side = "auto";
    std::optional<QPointF> manualPosition; // 手动中心坐标；缺失时使用自动布局。
};
struct FlowNode : BaseNode {
    QString role = "process";
    QPointF position;
};
struct Relation {
    ID id, sourceId, targetId;
    QString label;
    QString arrow = "end";
    EdgeStyle style;
};
struct FlowEdge : Relation {
    QString sourcePort, targetPort;
    std::optional<QPointF> bendPoint; // 手动曲线中点；缺失时自动走线。
};
struct GroupAnnotation {
    ID id, parentId;
    QString kind;
    QStringList memberIds;
    QString text, color;
};
struct Comment {
    ID id, nodeId;
    QString author, body;
    QDateTime createdAt, updatedAt;
    bool resolved = false;
};
struct Viewport {
    double x = 0, y = 0, zoom = 1;
};
struct CanvasImage
{
    ID id;
    QPointF position;
    QSizeF size;
    QString imagePng;
};
struct SheetBase {
    ID id;
    QString name, themeId = "mint";
    Viewport viewport;
    NodeStyle defaultNodeStyle;
    EdgeStyle defaultEdgeStyle;
    QVector<CanvasImage> images;
};
struct TreeSheet : SheetBase {
    QString kind = "mindmap";
    ID rootId;
    QString layout = "bilateral";
    QMap<ID, TreeNode> nodes;
    QVector<Relation> relations;
    QVector<GroupAnnotation> annotations;
    QVector<Comment> comments;
};
struct FlowSheet : SheetBase {
    QString kind = "flowchart";
    QMap<ID, FlowNode> nodes;
    QVector<FlowEdge> edges;
    QVector<Comment> comments;
};
struct GanttFilter {
    std::optional<ID> sourceSheetId;
    bool includeStandalone = true;
};
struct GanttSheet : SheetBase {
    QString kind = "gantt";
    GanttFilter filter;
    QString scale = "week";
};
using Sheet = std::variant<TreeSheet, FlowSheet, GanttSheet>;
struct TaskSource { ID sheetId, nodeId; };
struct Task {
    ID id;
    QString title;
    std::optional<TaskSource> source;
    std::optional<QDate> startDate, endDate;
    int progress = 0;
    QString owner;
    bool milestone = false;
    QStringList predecessorIds;
};
struct ReviewCard {
    ID id, sheetId, nodeId;
    QString front, back;
    QString status = "new", mode = "linked";
    QString sourceHash, sourceState = "ok";
    std::optional<QDateTime> lastReviewedAt;
};
struct PresentationStep {
    ID id, nodeId;
    QString speakerNote;
};
struct PresentationRoute {
    ID id, sheetId;
    QString title;
    QVector<PresentationStep> steps;
};
struct DraftSourceText
{
    QString title, note;

    bool operator==(const DraftSourceText &other) const
    {
        return title == other.title && note == other.note;
    }
};
using DraftSourceSnapshot = QMap<ID, DraftSourceText>;

struct DraftSection {
    QString heading, text;
    QStringList sourceNodeIds;
    QString origin = "source";
    // 未记录表示旧稿缺历史，空映射表示已记录的来源全部缺失。
    std::optional<DraftSourceSnapshot> sourceSnapshot = std::nullopt;
};
struct TextDraft {
    ID id, sourceSheetId;
    QString title;
    qint64 sourceRevision = 0;
    QString purpose = "summary";
    QVector<DraftSection> sections;
    QDateTime createdAt;
    // 整表指纹用于核验旧稿能否补录，逐段对照使用各段的来源快照。
    std::optional<QString> sourceHash;
};
struct Project {
    int schemaVersion = 1;
    ID id, activeSheetId;
    QString title;
    qint64 revision = 0;
    QDateTime createdAt, updatedAt;
    QVector<Sheet> sheets;
    QVector<Task> tasks;
    QVector<ReviewCard> cards;
    QVector<PresentationRoute> routes;
    QVector<TextDraft> drafts;
};

}  // namespace lingya
