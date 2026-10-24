#include "services/ProjectJson.h"
#include "domain/Limits.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>

namespace lingya {


namespace ProjectJson {
namespace {

// JSON安全整数上限（规范§4.2：数值revision限定在JSON安全整数范围内）
constexpr qint64 maxJsonInt = 9007199254740991LL;

const QRegularExpression& uuidPattern() {
    static const QRegularExpression re(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    return re;
}
bool isUuid(const QString& s) { return uuidPattern().match(s).hasMatch(); }
bool isHexColor(const QString& s) {
    static const QRegularExpression re("^#[0-9A-Fa-f]{6}$");
    return re.match(s).hasMatch();
}
bool isHttpLink(const QString& s) {
    const QUrl url(s);
    return url.scheme() == "http" || url.scheme() == "https";
}

// ================================ 写出 ================================

template<typename T>
QJsonValue optJson(const std::optional<T>& value) {
    return value.has_value() ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}
QJsonValue optDateJson(const std::optional<QDate>& value) {
    return value.has_value() ? QJsonValue(value->toString("yyyy-MM-dd"))
                             : QJsonValue(QJsonValue::Null);
}
QJsonValue optDateTimeJson(const std::optional<QDateTime>& value) {
    return value.has_value() ? QJsonValue(value->toUTC().toString(Qt::ISODateWithMs))
                             : QJsonValue(QJsonValue::Null);
}

void setIfSet(QJsonObject& object, const QString& key, const std::optional<QString>& value) {
    if (value.has_value()) object[key] = *value;
}
void setIfSet(QJsonObject& object, const QString& key, const std::optional<int>& value) {
    if (value.has_value()) object[key] = *value;
}
void setIfSet(QJsonObject& object, const QString& key, const std::optional<bool>& value) {
    if (value.has_value()) object[key] = *value;
}
void setIfSet(QJsonObject& object, const QString& key, const std::optional<double>& value) {
    if (value.has_value()) object[key] = *value;
}

template<typename T, typename F>
QJsonArray mapArray(const QVector<T>& values, F writer) {
    QJsonArray array;
    for (const T& value : values) array.append(writer(value));
    return array;
}

QJsonObject writeNodeStyle(const NodeStyle& s) {
    return QJsonObject{{"fill", s.fill},
                       {"color", s.color},
                       {"borderColor", s.borderColor},
                       {"fontSize", s.fontSize},
                       {"fontFamily", s.fontFamily},
                       {"bold", s.bold},
                       {"shape", s.shape},
                       {"borderWidth", s.borderWidth}};
}

QJsonObject writeEdgeStyle(const EdgeStyle& e) {
    return QJsonObject{{"color", e.color}, {"width", e.width}, {"dashed", e.dashed}};
}

// 局部样式只写已设置的字段，全空时返回空对象（规范§4.2）
QJsonObject writeOverride(const NodeStyleOverride& o) {
    QJsonObject j;
    setIfSet(j, "fill", o.fill);
    setIfSet(j, "color", o.color);
    setIfSet(j, "borderColor", o.borderColor);
    setIfSet(j, "fontSize", o.fontSize);
    setIfSet(j, "fontFamily", o.fontFamily);
    setIfSet(j, "shape", o.shape);
    setIfSet(j, "bold", o.bold);
    setIfSet(j, "borderWidth", o.borderWidth);
    return j;
}

bool overrideEmpty(const NodeStyleOverride& o) {
    return !o.fill && !o.color && !o.borderColor && !o.fontSize && !o.fontFamily && !o.shape &&
           !o.bold && !o.borderWidth;
}

QJsonObject writeProvenance(const Provenance& p) {
    QJsonObject j{{"kind", p.kind},
                  {"requestId", p.requestId},
                  {"model", p.model},
                  {"origin", p.origin}};
    if (!p.sourceParagraphIds.isEmpty())
        j["sourceParagraphIds"] = QJsonArray::fromStringList(p.sourceParagraphIds);
    return j;
}

void writeBaseNode(QJsonObject& j, const BaseNode& n) {
    j["id"] = n.id;
    j["title"] = n.title;
    j["note"] = n.note;
    j["iconId"] = optJson(n.iconId);
    j["link"] = optJson(n.link);
    j["tags"] = QJsonArray::fromStringList(n.tags);
    if (!overrideEmpty(n.style)) j["style"] = writeOverride(n.style);
    j["contentRevision"] = n.contentRevision;
    if (n.provenance.has_value()) j["provenance"] = writeProvenance(*n.provenance);
}

QJsonObject writeTreeNode(const TreeNode& n) {
    QJsonObject j;
    writeBaseNode(j, n);
    j["parentId"] = optJson(n.parentId);
    j["order"] = n.order;
    j["collapsed"] = n.collapsed;
    j["side"] = n.side;
    return j;
}

QJsonObject writeFlowNode(const FlowNode& n) {
    QJsonObject j;
    writeBaseNode(j, n);
    j["role"] = n.role;
    j["position"] = QJsonObject{{"x", n.position.x()}, {"y", n.position.y()}};
    return j;
}

QJsonObject writeRelation(const Relation& r) {
    return QJsonObject{{"id", r.id},
                       {"sourceId", r.sourceId},
                       {"targetId", r.targetId},
                       {"label", r.label},
                       {"arrow", r.arrow},
                       {"style", writeEdgeStyle(r.style)}};
}

QJsonObject writeFlowEdge(const FlowEdge& e) {
    QJsonObject j = writeRelation(e);
    j["sourcePort"] = e.sourcePort;
    j["targetPort"] = e.targetPort;
    return j;
}

QJsonObject writeAnnotation(const GroupAnnotation& g) {
    QJsonObject j{{"id", g.id}, {"parentId", g.parentId}, {"kind", g.kind},
                  {"text", g.text}, {"color", g.color}};
    j["memberIds"] = QJsonArray::fromStringList(g.memberIds);
    return j;
}

QJsonObject writeComment(const Comment& c) {
    return QJsonObject{{"id", c.id},
                       {"nodeId", c.nodeId},
                       {"author", c.author},
                       {"body", c.body},
                       {"createdAt", c.createdAt.toUTC().toString(Qt::ISODateWithMs)},
                       {"updatedAt", c.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
                       {"resolved", c.resolved}};
}

void writeSheetBase(QJsonObject& j, const SheetBase& s) {
    j["id"] = s.id;
    j["name"] = s.name;
    j["themeId"] = s.themeId;
    j["viewport"] = QJsonObject{{"x", s.viewport.x}, {"y", s.viewport.y}, {"zoom", s.viewport.zoom}};
    j["defaultNodeStyle"] = writeNodeStyle(s.defaultNodeStyle);
    j["defaultEdgeStyle"] = writeEdgeStyle(s.defaultEdgeStyle);
}

QJsonObject writeSheet(const Sheet& s) {
    QJsonObject j;
    if (const TreeSheet* tree = std::get_if<TreeSheet>(&s)) {
        writeSheetBase(j, *tree);
        j["kind"] = tree->kind;
        j["layout"] = tree->layout;
        j["rootId"] = tree->rootId;
        QJsonObject nodes;
        for (auto it = tree->nodes.constBegin(); it != tree->nodes.constEnd(); ++it)
            nodes[it.key()] = writeTreeNode(it.value());
        j["nodes"] = nodes;
        j["relations"] = mapArray(tree->relations, writeRelation);
        j["annotations"] = mapArray(tree->annotations, writeAnnotation);
        j["comments"] = mapArray(tree->comments, writeComment);
    } else if (const FlowSheet* flow = std::get_if<FlowSheet>(&s)) {
        writeSheetBase(j, *flow);
        j["kind"] = flow->kind;
        QJsonObject nodes;
        for (auto it = flow->nodes.constBegin(); it != flow->nodes.constEnd(); ++it)
            nodes[it.key()] = writeFlowNode(it.value());
        j["nodes"] = nodes;
        j["edges"] = mapArray(flow->edges, writeFlowEdge);
        j["comments"] = mapArray(flow->comments, writeComment);
    } else if (const GanttSheet* gantt = std::get_if<GanttSheet>(&s)) {
        writeSheetBase(j, *gantt);
        j["kind"] = gantt->kind;
        QJsonObject filter;
        filter["sourceSheetId"] = optJson(gantt->filter.sourceSheetId);
        filter["includeStandalone"] = gantt->filter.includeStandalone;
        j["filter"] = filter;
        j["scale"] = gantt->scale;
    }
    return j;
}

QJsonObject writeTask(const Task& t) {
    QJsonObject j{{"id", t.id},
                  {"title", t.title},
                  {"progress", t.progress},
                  {"owner", t.owner},
                  {"milestone", t.milestone}};
    if (t.source.has_value())
        j["source"] = QJsonObject{{"sheetId", t.source->sheetId}, {"nodeId", t.source->nodeId}};
    else
        j["source"] = QJsonValue(QJsonValue::Null);
    j["startDate"] = optDateJson(t.startDate);
    j["endDate"] = optDateJson(t.endDate);
    j["predecessorIds"] = QJsonArray::fromStringList(t.predecessorIds);
    return j;
}

QJsonObject writeCard(const ReviewCard& c) {
    QJsonObject j{{"id", c.id},
                  {"sheetId", c.sheetId},
                  {"nodeId", c.nodeId},
                  {"front", c.front},
                  {"back", c.back},
                  {"status", c.status},
                  {"mode", c.mode},
                  {"sourceHash", c.sourceHash},
                  {"sourceState", c.sourceState}};
    j["lastReviewedAt"] = optDateTimeJson(c.lastReviewedAt);
    return j;
}

QJsonObject writeRoute(const PresentationRoute& r) {
    QJsonObject j{{"id", r.id}, {"sheetId", r.sheetId}, {"title", r.title}};
    j["steps"] = mapArray(r.steps, [](const PresentationStep& s) {
        return QJsonObject{{"id", s.id}, {"nodeId", s.nodeId}, {"speakerNote", s.speakerNote}};
    });
    return j;
}

QJsonObject writeDraft(const TextDraft& d) {
    QJsonObject j{{"id", d.id},
                  {"sourceSheetId", d.sourceSheetId},
                  {"title", d.title},
                  {"sourceRevision", d.sourceRevision},
                  {"purpose", d.purpose},
                  {"createdAt", d.createdAt.toUTC().toString(Qt::ISODateWithMs)}};
    j["sections"] = mapArray(d.sections, [](const DraftSection& s) {
        QJsonObject o{{"heading", s.heading}, {"text", s.text}, {"origin", s.origin}};
        o["sourceNodeIds"] = QJsonArray::fromStringList(s.sourceNodeIds);
        return o;
    });
    return j;
}

QJsonObject writeProject(const Project& p) {
    QJsonObject j;
    j["schemaVersion"] = p.schemaVersion;
    j["id"] = p.id;
    j["activeSheetId"] = p.activeSheetId;
    j["title"] = p.title;
    j["revision"] = p.revision;
    j["createdAt"] = p.createdAt.toUTC().toString(Qt::ISODateWithMs);
    j["updatedAt"] = p.updatedAt.toUTC().toString(Qt::ISODateWithMs);
    j["sheets"] = mapArray(p.sheets, writeSheet);
    j["tasks"] = mapArray(p.tasks, writeTask);
    j["cards"] = mapArray(p.cards, writeCard);
    j["routes"] = mapArray(p.routes, writeRoute);
    j["drafts"] = mapArray(p.drafts, writeDraft);
    return j;
}

// ================================ 读取 ================================

// 严格对象读取器：所有取值先检查类型与范围，不能用默认值掩盖缺失或错误类型（规范§4.2）
struct Reader {
    QJsonObject obj;
    QString path;
    QStringList* errs;

    Reader(QJsonObject object, QString objectPath, QStringList* errors)
        : obj(std::move(object)), path(std::move(objectPath)), errs(errors) {}

    // 每个已定义对象拒绝未知字段（规范§4.2）
    void unknownKeys(const QStringList& known) const {
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            if (!known.contains(it.key()))
                errs->append(path + "：存在未知字段 " + it.key());
    }

    bool has(const QString& key) const { return obj.contains(key); }
    QJsonValue val(const QString& key) const { return obj.value(key); }
    bool hasValue(const QString& key) const {
        const QJsonValue v = obj.value(key);
        return !(v.isUndefined() || v.isNull());
    }

    QString str(const QString& key) const {
        const QJsonValue v = obj.value(key);
        if (!v.isString()) {
            errs->append(path + "." + key + "：应为字符串");
            return QString();
        }
        return v.toString();
    }
    std::optional<QString> optStr(const QString& key) const {
        const QJsonValue v = obj.value(key);
        if (v.isUndefined() || v.isNull()) return std::nullopt;
        if (!v.isString()) {
            errs->append(path + "." + key + "：应为字符串或null");
            return std::nullopt;
        }
        return v.toString();
    }

    qint64 integer(const QString& key, qint64 lo, qint64 hi) const {
        const QJsonValue v = obj.value(key);
        if (!v.isDouble()) {
            errs->append(path + "." + key + "：应为数字");
            return 0;
        }
        const double d = v.toDouble();
        if (!std::isfinite(d) || d != std::floor(d)) {
            errs->append(path + "." + key + "：应为整数");
            return 0;
        }
        if (d < double(lo) || d > double(hi)) {
            errs->append(path + "." + key + "：整数超出允许范围");
            return 0;
        }
        return qint64(d);
    }
    int int32(const QString& key, int lo, int hi) const {
        return int(integer(key, lo, hi));
    }
    double real(const QString& key, double lo, double hi) const {
        const QJsonValue v = obj.value(key);
        if (!v.isDouble()) {
            errs->append(path + "." + key + "：应为数字");
            return 0;
        }
        const double d = v.toDouble();
        if (!std::isfinite(d) || d < lo || d > hi) {
            errs->append(path + "." + key + "：数值超出允许范围");
            return 0;
        }
        return d;
    }
    bool boolean(const QString& key) const {
        const QJsonValue v = obj.value(key);
        if (!v.isBool()) {
            errs->append(path + "." + key + "：应为布尔值");
            return false;
        }
        return v.toBool();
    }

    std::optional<int> optInt(const QString& key, int lo, int hi) const {
        const QJsonValue v = obj.value(key);
        if (v.isUndefined() || v.isNull()) return std::nullopt;
        if (!v.isDouble()) {
            errs->append(path + "." + key + "：应为整数或null");
            return std::nullopt;
        }
        const double d = v.toDouble();
        if (!std::isfinite(d) || d != std::floor(d) || d < double(lo) || d > double(hi)) {
            errs->append(path + "." + key + "：整数超出允许范围");
            return std::nullopt;
        }
        return int(d);
    }
    std::optional<double> optReal(const QString& key, double lo, double hi) const {
        const QJsonValue v = obj.value(key);
        if (v.isUndefined() || v.isNull()) return std::nullopt;
        if (!v.isDouble()) {
            errs->append(path + "." + key + "：应为数字或null");
            return std::nullopt;
        }
        const double d = v.toDouble();
        if (!std::isfinite(d) || d < lo || d > hi) {
            errs->append(path + "." + key + "：数值超出允许范围");
            return std::nullopt;
        }
        return d;
    }
    std::optional<bool> optBool(const QString& key) const {
        const QJsonValue v = obj.value(key);
        if (v.isUndefined() || v.isNull()) return std::nullopt;
        if (!v.isBool()) {
            errs->append(path + "." + key + "：应为布尔值或null");
            return std::nullopt;
        }
        return v.toBool();
    }

    // 日期yyyy-MM-dd并要求格式化结果与输入比对（规范§4.2）
    QDate date(const QString& key) const {
        const QString s = str(key);
        const QDate d = QDate::fromString(s, "yyyy-MM-dd");
        if (!d.isValid() || d.toString("yyyy-MM-dd") != s) {
            errs->append(path + "." + key + "：日期非法或不是yyyy-MM-dd格式");
            return QDate();
        }
        return d;
    }
    std::optional<QDate> optDate(const QString& key) const {
        const QJsonValue v = obj.value(key);
        if (v.isUndefined() || v.isNull()) return std::nullopt;
        return date(key);
    }
    QDateTime dateTime(const QString& key) const {
        const QString s = str(key);
        const QDateTime t = QDateTime::fromString(s, Qt::ISODateWithMs);
        if (!t.isValid()) {
            errs->append(path + "." + key + "：时间戳非法");
            return QDateTime();
        }
        return t.toUTC();
    }

    QStringList strList(const QString& key) const {
        QStringList out;
        const QJsonValue v = obj.value(key);
        if (!v.isArray()) {
            errs->append(path + "." + key + "：应为字符串数组");
            return out;
        }
        for (const QJsonValue& e : v.toArray())
            if (e.isString()) out.append(e.toString());
            else errs->append(path + "." + key + "：数组元素应为字符串");
        return out;
    }
    QJsonArray array(const QString& key) const {
        const QJsonValue v = obj.value(key);
        if (!v.isArray()) {
            errs->append(path + "." + key + "：应为数组");
            return QJsonArray();
        }
        return v.toArray();
    }
    QJsonObject object(const QString& key) const {
        const QJsonValue v = obj.value(key);
        if (!v.isObject()) {
            errs->append(path + "." + key + "：应为对象");
            return QJsonObject();
        }
        return v.toObject();
    }

    QString uuid(const QString& key) const {
        const QString s = str(key);
        if (!isUuid(s)) errs->append(path + "." + key + "：不是合法的UUID格式");
        return s;
    }
    QString enumVal(const QString& key, std::initializer_list<const char*> allowed) const {
        const QString s = str(key);
        QStringList names;
        for (const char* a : allowed) {
            if (s == QLatin1String(a)) return s;
            names << QLatin1String(a);
        }
        errs->append(path + "." + key + "：取值必须是 " + names.join("/"));
        return s;
    }
    QString hexColor(const QString& key) const {
        const QString s = str(key);
        if (!isHexColor(s)) errs->append(path + "." + key + "：颜色必须为#RRGGBB十六进制");
        return s;
    }
    std::optional<QString> optHexColor(const QString& key) const {
        auto s = optStr(key);
        if (s.has_value() && !isHexColor(*s)) {
            errs->append(path + "." + key + "：颜色必须为#RRGGBB十六进制");
            return std::nullopt;
        }
        return s;
    }
    std::optional<QString> optEnum(const QString& key,
                                   std::initializer_list<const char*> allowed) const {
        auto s = optStr(key);
        if (s.has_value()) {
            QStringList names;
            bool ok = false;
            for (const char* a : allowed) {
                if (*s == QLatin1String(a)) ok = true;
                names << QLatin1String(a);
            }
            if (!ok) {
                errs->append(path + "." + key + "：取值必须是 " + names.join("/"));
                return std::nullopt;
            }
        }
        return s;
    }
    // 链接只允许http/https，禁止javascript/file/data协议（规范§4.3.10）
    std::optional<QString> optLink(const QString& key) const {
        auto s = optStr(key);
        if (s.has_value() && !isHttpLink(*s)) {
            errs->append(path + "." + key + "：链接只允许http/https协议");
            return std::nullopt;
        }
        return s;
    }
};

NodeStyle readNodeStyle(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"fill", "color", "borderColor", "fontSize", "fontFamily", "bold", "shape",
                   "borderWidth"});
    NodeStyle s;
    s.fill = r.hexColor("fill");
    s.color = r.hexColor("color");
    s.borderColor = r.hexColor("borderColor");
    s.fontSize = r.int32("fontSize", Limits::kFontSizeMin, Limits::kFontSizeMax);
    s.fontFamily = r.str("fontFamily");
    s.bold = r.boolean("bold");
    s.shape = r.enumVal("shape", {"roundRect", "rect", "ellipse", "diamond", "parallelogram"});
    s.borderWidth = r.real("borderWidth", 0.1, 20.0);
    return s;
}

EdgeStyle readEdgeStyle(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"color", "width", "dashed"});
    EdgeStyle e;
    e.color = r.hexColor("color");
    e.width = r.real("width", 0.1, 20.0);
    e.dashed = r.boolean("dashed");
    return e;
}

NodeStyleOverride readOverride(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"fill", "color", "borderColor", "fontSize", "fontFamily", "shape", "bold",
                   "borderWidth"});
    NodeStyleOverride v;
    v.fill = r.optHexColor("fill");
    v.color = r.optHexColor("color");
    v.borderColor = r.optHexColor("borderColor");
    v.fontSize = r.optInt("fontSize", Limits::kFontSizeMin, Limits::kFontSizeMax);
    v.fontFamily = r.optStr("fontFamily");
    v.shape = r.optEnum("shape", {"roundRect", "rect", "ellipse", "diamond", "parallelogram"});
    v.bold = r.optBool("bold");
    v.borderWidth = r.optReal("borderWidth", 0.1, 20.0);
    return v;
}

template<typename Node>
void readBaseNode(Reader& r, Node& n, const QStringList& extraKeys) {
    QStringList known = {"id",       "title",          "note",     "iconId", "link",
                         "tags",     "contentRevision", "style",   "provenance"};
    known += extraKeys;
    r.unknownKeys(known);
    n.id = r.uuid("id");
    n.title = r.str("title");
    n.note = r.str("note");
    n.iconId = r.optStr("iconId");   // 本地图标资源ID，白名单随M2图标库校验
    n.link = r.optLink("link");
    n.tags = r.strList("tags");
    n.contentRevision = r.integer("contentRevision", 0, maxJsonInt);
    if (r.has("style")) {
        const QJsonValue sv = r.val("style");
        if (sv.isObject()) {
            if (!sv.toObject().isEmpty())
                n.style = readOverride(sv.toObject(), r.path + ".style", r.errs);
        } else {
            r.errs->append(r.path + ".style：应为对象");
        }
    }
    if (r.has("provenance")) {
        const QJsonValue pv = r.val("provenance");
        if (pv.isObject()) {
            Reader pr(pv.toObject(), r.path + ".provenance", r.errs);
            pr.unknownKeys({"kind", "requestId", "model", "sourceParagraphIds", "origin"});
            Provenance prov;
            prov.kind = pr.enumVal("kind", {"ai"});
            prov.requestId = pr.str("requestId");
            prov.model = pr.str("model");
            prov.sourceParagraphIds = pr.strList("sourceParagraphIds");
            prov.origin = pr.enumVal("origin", {"source", "supplement"});
            n.provenance = prov;
        } else {
            r.errs->append(r.path + ".provenance：应为对象");
        }
    }
}

TreeNode readTreeNode(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    TreeNode n;
    readBaseNode(r, n, {"parentId", "order", "collapsed", "side"});
    n.parentId = r.optStr("parentId");
    n.order = r.int32("order", 0, 1000000);
    n.collapsed = r.boolean("collapsed");
    n.side = r.enumVal("side", {"left", "right", "auto"});
    return n;
}

FlowNode readFlowNode(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    FlowNode n;
    readBaseNode(r, n, {"role", "position"});
    n.role = r.enumVal("role", {"start", "end", "process", "decision", "io"});
    const Reader pr(r.object("position"), path + ".position", es);
    pr.unknownKeys({"x", "y"});
    n.position = QPointF(pr.real("x", -100000.0, 100000.0), pr.real("y", -100000.0, 100000.0));
    return n;
}

Relation readRelation(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"id", "sourceId", "targetId", "label", "arrow", "style"});
    Relation rel;
    rel.id = r.uuid("id");
    rel.sourceId = r.uuid("sourceId");
    rel.targetId = r.uuid("targetId");
    rel.label = r.str("label");
    rel.arrow = r.enumVal("arrow", {"none", "end", "both"});
    rel.style = readEdgeStyle(r.object("style"), path + ".style", es);
    return rel;
}

FlowEdge readFlowEdge(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"id", "sourceId", "targetId", "label", "arrow", "style", "sourcePort",
                   "targetPort"});
    FlowEdge edge;
    edge.id = r.uuid("id");
    edge.sourceId = r.uuid("sourceId");
    edge.targetId = r.uuid("targetId");
    edge.label = r.str("label");
    edge.arrow = r.enumVal("arrow", {"none", "end", "both"});
    edge.style = readEdgeStyle(r.object("style"), path + ".style", es);
    edge.sourcePort = r.str("sourcePort");
    edge.targetPort = r.str("targetPort");
    return edge;
}

GroupAnnotation readAnnotation(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"id", "parentId", "kind", "memberIds", "text", "color"});
    GroupAnnotation ga;
    ga.id = r.uuid("id");
    ga.parentId = r.uuid("parentId");
    ga.kind = r.enumVal("kind", {"boundary", "summary"});
    ga.memberIds = r.strList("memberIds");
    ga.text = r.str("text");
    ga.color = r.hexColor("color");
    return ga;
}

Comment readComment(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"id", "nodeId", "author", "body", "createdAt", "updatedAt", "resolved"});
    Comment c;
    c.id = r.uuid("id");
    c.nodeId = r.uuid("nodeId");
    c.author = r.str("author");
    c.body = r.str("body");
    c.createdAt = r.dateTime("createdAt");
    c.updatedAt = r.dateTime("updatedAt");
    c.resolved = r.boolean("resolved");
    return c;
}

Task readTask(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"id", "title", "source", "startDate", "endDate", "progress", "owner",
                   "milestone", "predecessorIds"});
    Task t;
    t.id = r.uuid("id");
    t.title = r.str("title");
    if (r.has("source")) {
        const QJsonValue sv = r.val("source");
        if (sv.isObject()) {
            Reader sr(sv.toObject(), path + ".source", es);
            sr.unknownKeys({"sheetId", "nodeId"});
            TaskSource src;
            src.sheetId = sr.uuid("sheetId");
            src.nodeId = sr.uuid("nodeId");
            t.source = src;
        } else if (!sv.isNull()) {
            es->append(path + ".source：应为对象或null");
        }
    } else {
        es->append(path + "：缺少字段 source");
    }
    t.startDate = r.optDate("startDate");
    t.endDate = r.optDate("endDate");
    t.progress = r.int32("progress", 0, 100);
    t.owner = r.str("owner");
    t.milestone = r.boolean("milestone");
    t.predecessorIds = r.strList("predecessorIds");
    return t;
}

ReviewCard readCard(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"id", "sheetId", "nodeId", "front", "back", "status", "mode", "sourceHash",
                   "sourceState", "lastReviewedAt"});
    ReviewCard c;
    c.id = r.uuid("id");
    c.sheetId = r.uuid("sheetId");
    c.nodeId = r.uuid("nodeId");
    c.front = r.str("front");
    c.back = r.str("back");
    c.status = r.enumVal("status", {"new", "review", "mastered"});
    c.mode = r.enumVal("mode", {"linked", "custom"});
    c.sourceHash = r.str("sourceHash");
    c.sourceState = r.enumVal("sourceState", {"ok", "changed", "missing"});
    c.lastReviewedAt = r.has("lastReviewedAt") && !r.val("lastReviewedAt").isNull()
                            ? std::optional<QDateTime>(r.dateTime("lastReviewedAt"))
                            : std::nullopt;
    return c;
}

PresentationRoute readRoute(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"id", "sheetId", "title", "steps"});
    PresentationRoute route;
    route.id = r.uuid("id");
    route.sheetId = r.uuid("sheetId");
    route.title = r.str("title");
    int i = 0;
    for (const QJsonValue& e : r.array("steps")) {
        Reader sr(e.toObject(), path + ".steps[" + QString::number(i++) + "]", es);
        sr.unknownKeys({"id", "nodeId", "speakerNote"});
        PresentationStep step;
        step.id = sr.uuid("id");
        step.nodeId = sr.uuid("nodeId");
        step.speakerNote = sr.str("speakerNote");
        route.steps.push_back(step);
    }
    return route;
}

TextDraft readDraft(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"id", "sourceSheetId", "title", "sourceRevision", "purpose", "sections",
                   "createdAt"});
    TextDraft d;
    d.id = r.uuid("id");
    d.sourceSheetId = r.uuid("sourceSheetId");
    d.title = r.str("title");
    d.sourceRevision = r.integer("sourceRevision", 0, maxJsonInt);
    d.purpose = r.enumVal("purpose", {"summary", "explanation", "speech"});
    d.createdAt = r.dateTime("createdAt");
    int i = 0;
    for (const QJsonValue& e : r.array("sections")) {
        const QString sp = path + ".sections[" + QString::number(i++) + "]";
        Reader sr(e.toObject(), sp, es);
        sr.unknownKeys({"heading", "text", "sourceNodeIds", "origin"});
        DraftSection section;
        section.heading = sr.str("heading");
        section.text = sr.str("text");
        section.sourceNodeIds = sr.strList("sourceNodeIds");
        section.origin = sr.enumVal("origin", {"source", "supplement"});
        d.sections.push_back(section);
    }
    return d;
}

template<typename SheetType>
void readSheetBase(Reader& r, SheetType& s, const QString& path, QStringList* es) {
    s.id = r.uuid("id");
    s.name = r.str("name");
    s.themeId = r.enumVal("themeId", {"mint", "lavender", "projection"});
    const Reader vr(r.object("viewport"), path + ".viewport", es);
    vr.unknownKeys({"x", "y", "zoom"});
    s.viewport.x = vr.real("x", -100000.0, 100000.0);
    s.viewport.y = vr.real("y", -100000.0, 100000.0);
    s.viewport.zoom = vr.real("zoom", Limits::kZoomMin, Limits::kZoomMax);
    s.defaultNodeStyle = readNodeStyle(r.object("defaultNodeStyle"), path + ".defaultNodeStyle", es);
    s.defaultEdgeStyle = readEdgeStyle(r.object("defaultEdgeStyle"), path + ".defaultEdgeStyle", es);
}

Sheet readSheet(const QJsonObject& o, const QString& path, QStringList* es) {
    const QJsonValue kv = o.value("kind");
    QString kind;
    if (kv.isString()) kind = kv.toString();
    else es->append(path + ".kind：应为字符串");

    if (kind == "mindmap" || kind == "fishbone") {
        Reader r(o, path, es);
        r.unknownKeys({"kind", "id", "name", "themeId", "viewport", "defaultNodeStyle",
                       "defaultEdgeStyle", "layout", "rootId", "nodes", "relations", "annotations",
                       "comments"});
        TreeSheet t;
        t.kind = kind;
        readSheetBase(r, t, path, es);
        t.layout = r.enumVal("layout", {"bilateral", "right", "fishbone"});
        t.rootId = r.uuid("rootId");
        const QJsonObject nodes = r.object("nodes");
        for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
            const QString nodePath = path + ".nodes." + it.key();
            TreeNode n = readTreeNode(it.value().toObject(), nodePath, es);
            if (n.id != it.key()) es->append(nodePath + "：节点ID与对象键不一致");
            t.nodes.insert(it.key(), n);
        }
        int i = 0;
        for (const QJsonValue& e : r.array("relations"))
            t.relations.push_back(
                readRelation(e.toObject(), path + ".relations[" + QString::number(i++) + "]", es));
        i = 0;
        for (const QJsonValue& e : r.array("annotations"))
            t.annotations.push_back(readAnnotation(
                e.toObject(), path + ".annotations[" + QString::number(i++) + "]", es));
        i = 0;
        for (const QJsonValue& e : r.array("comments"))
            t.comments.push_back(
                readComment(e.toObject(), path + ".comments[" + QString::number(i++) + "]", es));
        return t;
    }
    if (kind == "flowchart") {
        Reader r(o, path, es);
        r.unknownKeys({"kind", "id", "name", "themeId", "viewport", "defaultNodeStyle",
                       "defaultEdgeStyle", "nodes", "edges", "comments"});
        FlowSheet f;
        f.kind = kind;
        readSheetBase(r, f, path, es);
        const QJsonObject nodes = r.object("nodes");
        for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
            const QString nodePath = path + ".nodes." + it.key();
            FlowNode n = readFlowNode(it.value().toObject(), nodePath, es);
            if (n.id != it.key()) es->append(nodePath + "：节点ID与对象键不一致");
            f.nodes.insert(it.key(), n);
        }
        int i = 0;
        for (const QJsonValue& e : r.array("edges"))
            f.edges.push_back(
                readFlowEdge(e.toObject(), path + ".edges[" + QString::number(i++) + "]", es));
        i = 0;
        for (const QJsonValue& e : r.array("comments"))
            f.comments.push_back(
                readComment(e.toObject(), path + ".comments[" + QString::number(i++) + "]", es));
        return f;
    }
    if (kind == "gantt") {
        Reader r(o, path, es);
        r.unknownKeys({"kind", "id", "name", "themeId", "viewport", "defaultNodeStyle",
                       "defaultEdgeStyle", "filter", "scale"});
        GanttSheet g;
        g.kind = kind;
        readSheetBase(r, g, path, es);
        const Reader fr(r.object("filter"), path + ".filter", es);
        fr.unknownKeys({"sourceSheetId", "includeStandalone"});
        g.filter.sourceSheetId = fr.hasValue("sourceSheetId")
                                     ? std::optional<QString>(fr.uuid("sourceSheetId"))
                                     : std::nullopt;
        g.filter.includeStandalone = fr.boolean("includeStandalone");
        g.scale = r.enumVal("scale", {"day", "week", "month"});
        return g;
    }
    es->append(path + ".kind：必须是 mindmap/fishbone/flowchart/gantt");
    return TreeSheet{};
}

Project readProject(const QJsonObject& o, const QString& path, QStringList* es) {
    Reader r(o, path, es);
    r.unknownKeys({"schemaVersion", "id", "activeSheetId", "title", "revision", "createdAt",
                   "updatedAt", "sheets", "tasks", "cards", "routes", "drafts"});
    Project p;
    p.schemaVersion = int(r.integer("schemaVersion", 1, 1));   // 只有1合法；更高版本拒绝（§8）
    p.id = r.uuid("id");
    p.activeSheetId = r.uuid("activeSheetId");
    p.title = r.str("title");
    p.revision = r.integer("revision", 0, maxJsonInt);
    p.createdAt = r.dateTime("createdAt");
    p.updatedAt = r.dateTime("updatedAt");
    int i = 0;
    for (const QJsonValue& e : r.array("sheets"))
        p.sheets.push_back(readSheet(e.toObject(), path + ".sheets[" + QString::number(i++) + "]", es));
    i = 0;
    for (const QJsonValue& e : r.array("tasks"))
        p.tasks.push_back(readTask(e.toObject(), path + ".tasks[" + QString::number(i++) + "]", es));
    i = 0;
    for (const QJsonValue& e : r.array("cards"))
        p.cards.push_back(readCard(e.toObject(), path + ".cards[" + QString::number(i++) + "]", es));
    i = 0;
    for (const QJsonValue& e : r.array("routes"))
        p.routes.push_back(readRoute(e.toObject(), path + ".routes[" + QString::number(i++) + "]", es));
    i = 0;
    for (const QJsonValue& e : r.array("drafts"))
        p.drafts.push_back(readDraft(e.toObject(), path + ".drafts[" + QString::number(i++) + "]", es));
    return p;
}

}  // namespace
}  // namespace ProjectJson

namespace ProjectJson {

QJsonObject toJson(const Project& project) {
    return writeProject(project);
}

QByteArray toBytes(const Project& project) {
    return QJsonDocument(writeProject(project)).toJson(QJsonDocument::Compact);
}

std::variant<Project, AppError> fromObject(const QJsonObject& object) {
    // 未知更高版本拒绝（原设计§8），错误码见协作规范§10.3：UNSUPPORTED_SCHEMA
    const QJsonValue sv = object.value("schemaVersion");
    if (sv.isDouble() && std::isfinite(sv.toDouble()) && sv.toDouble() != 1.0)
        return AppError{"UNSUPPORTED_SCHEMA",
                        QString("文件schemaVersion为%1，当前仅支持版本1").arg(sv.toDouble())};
    QStringList es;
    const Project project = readProject(object, "project", &es);
    if (!es.isEmpty()) {
        const int shown = static_cast<int>(qMin<qsizetype>(es.size(), 5));
        QString message = es.mid(0, shown).join("；");
        if (es.size() > shown)
            message += QString("；等共%1处问题").arg(es.size());
        return AppError{"INVALID_PROJECT", message};
    }
    return project;
}

std::variant<Project, AppError> fromBytes(const QByteArray& data) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return AppError{"INVALID_PROJECT", "JSON解析失败：" + parseError.errorString()};
    if (!doc.isObject())
        return AppError{"INVALID_PROJECT", "顶层必须是JSON对象"};
    return fromObject(doc.object());
}

}  // namespace ProjectJson

}  // namespace lingya
