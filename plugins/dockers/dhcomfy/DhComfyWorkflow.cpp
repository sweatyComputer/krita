/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhComfyWorkflow.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace DhComfy {

namespace {

// ---------------------------------------------------------------------------
// object_info helpers

QJsonObject nodeDef(const QJsonObject &info, const QString &type)
{
    return info.value(type).toObject();
}

QJsonArray fieldSpec(const QJsonObject &def, const QString &name)
{
    const QJsonObject in = def.value("input").toObject();
    QJsonValue v = in.value("required").toObject().value(name);
    if (v.isUndefined()) v = in.value("optional").toObject().value(name);
    return v.toArray();
}

QStringList inputOrder(const QJsonObject &def)
{
    QStringList names;
    const QJsonObject order = def.value("input_order").toObject();
    if (!order.isEmpty()) {
        for (const char *cat : {"required", "optional"}) {
            for (const QJsonValue &n : order.value(cat).toArray()) names << n.toString();
        }
        return names;
    }
    // Old servers: QJsonObject sorts keys, so this order is only a guess.
    const QJsonObject in = def.value("input").toObject();
    names << in.value("required").toObject().keys();
    names << in.value("optional").toObject().keys();
    return names;
}

QString specType(const QJsonArray &spec)
{
    if (spec.isEmpty()) return QString();
    if (spec.at(0).isArray()) return QStringLiteral("COMBO");
    return spec.at(0).toString();
}

QJsonObject specOptions(const QJsonArray &spec)
{
    return spec.size() > 1 ? spec.at(1).toObject() : QJsonObject();
}

bool isWidgetSpec(const QJsonArray &spec)
{
    const QString t = specType(spec);
    static const QSet<QString> widgetTypes {"INT", "FLOAT", "BOOLEAN", "BOOL", "STRING", "COMBO"};
    const QJsonObject o = specOptions(spec);
    if (o.value("socketless").toBool(false)) return true; // custom widgets (painter, bboxes...)
    if (!widgetTypes.contains(t)) return false;
    return !o.value("forceInput").toBool(false);
}

QStringList comboOptions(const QJsonArray &spec)
{
    QStringList out;
    QJsonArray arr;
    if (!spec.isEmpty() && spec.at(0).isArray()) arr = spec.at(0).toArray();
    else arr = specOptions(spec).value("options").toArray();
    for (const QJsonValue &v : arr) out << v.toVariant().toString();
    return out;
}

QString idString(const QJsonValue &v)
{
    if (v.isDouble()) return QString::number(qint64(v.toDouble()));
    return v.toString();
}

bool isLink(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    return v.isArray() && a.size() == 2 && (a.at(0).isString() || a.at(0).isDouble()) && a.at(1).isDouble();
}

const QSet<QString> &controlWords()
{
    static const QSet<QString> w {"fixed", "increment", "decrement", "randomize"};
    return w;
}

// ---------------------------------------------------------------------------
// flat UI graph (subgraphs expanded)

struct UNode {
    QString id;
    QString type;
    QString title;
    int mode {0};
    QJsonArray inputs;
    QJsonArray outputs;
    QJsonValue widgets;
    QJsonObject properties;
    QHash<QString, QJsonValue> overrides; ///< promoted subgraph widgets
    QJsonValue literal;                   ///< "__SubgraphInput": its value, once known
};

struct ULink {
    QString origin;
    int originSlot {0};
    QString target;
    int targetSlot {0};
    bool dead {false};
};

ULink parseLink(const QJsonValue &v, const QString &prefix)
{
    ULink l;
    auto pid = [&](const QJsonValue &x) -> QString {
        const QString s = idString(x);
        // -10 / -20 are a subgraph's own input / output pseudo-nodes
        if (s == "-10" || s == "-20") return s;
        return prefix + s;
    };
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        l.origin = pid(a.at(1));
        l.originSlot = a.at(2).toInt();
        l.target = pid(a.at(3));
        l.targetSlot = a.at(4).toInt();
    } else {
        const QJsonObject o = v.toObject();
        l.origin = pid(o.value("origin_id"));
        l.originSlot = o.value("origin_slot").toInt();
        l.target = pid(o.value("target_id"));
        l.targetSlot = o.value("target_slot").toInt();
    }
    return l;
}

UNode parseNode(const QJsonObject &n, const QString &prefix)
{
    UNode u;
    u.id = prefix + idString(n.value("id"));
    u.type = n.value("type").toString();
    u.title = n.value("title").toString();
    u.mode = n.value("mode").toInt(0);
    u.inputs = n.value("inputs").toArray();
    u.outputs = n.value("outputs").toArray();
    u.widgets = n.value("widgets_values");
    u.properties = n.value("properties").toObject();
    return u;
}

class FlatGraph
{
public:
    QVector<UNode> nodes;
    QHash<QString, int> index; ///< node id -> position in nodes
    QVector<ULink> links;

    void addNode(const UNode &n)
    {
        index.insert(n.id, nodes.size());
        nodes.append(n);
    }

    void removeNode(const QString &id)
    {
        const int i = index.value(id, -1);
        if (i < 0) return;
        nodes[i].type.clear(); // tombstone, keeps indices stable
        index.remove(id);
    }

    const UNode *node(const QString &id) const
    {
        const int i = index.value(id, -1);
        return i < 0 ? nullptr : &nodes[i];
    }

    const ULink *linkInto(const QString &target, int slot) const
    {
        for (const ULink &l : links) {
            if (!l.dead && l.target == target && l.targetSlot == slot) return &l;
        }
        return nullptr;
    }

    bool hasLinksFrom(const QString &origin) const
    {
        for (const ULink &l : links) {
            if (!l.dead && l.origin == origin) return true;
        }
        return false;
    }
};

bool isWidgetTypeName(const QString &t)
{
    static const QSet<QString> w {"INT", "FLOAT", "BOOLEAN", "BOOL", "STRING", "COMBO"};
    return w.contains(t);
}

bool expandSubgraphs(FlatGraph &g, const QHash<QString, QJsonObject> &defs, QString *error)
{
    for (int guard = 0; guard < 1000; guard++) {
        int found = -1;
        for (int i = 0; i < g.nodes.size(); i++) {
            if (!g.nodes[i].type.isEmpty() && defs.contains(g.nodes[i].type)) { found = i; break; }
        }
        if (found < 0) return true;

        const UNode inst = g.nodes[found];
        const QJsonObject def = defs.value(inst.type);
        const QJsonArray defInputs = def.value("inputs").toArray();
        const QString prefix = inst.id + ":";

        // The instance lists only some subgraph inputs, so match them by name.
        QHash<QString, int> instSlot;
        for (int k = 0; k < inst.inputs.size(); k++) {
            instSlot.insert(inst.inputs.at(k).toObject().value("name").toString(), k);
        }
        // Its widgets_values hold one value per widget-type subgraph input, in order.
        QHash<int, QJsonValue> inputValue; // subgraph input index -> value
        const QJsonArray instWidgets = inst.widgets.toArray();
        const bool proxied = inst.properties.contains("proxyWidgets");
        if (!proxied) {
            int cursor = 0;
            for (int i = 0; i < defInputs.size() && cursor < instWidgets.size(); i++) {
                if (!isWidgetTypeName(defInputs.at(i).toObject().value("type").toString())) continue;
                const QJsonValue v = instWidgets.at(cursor++);
                if (!v.isNull() && !v.isUndefined()) inputValue.insert(i, v);
                if (cursor < instWidgets.size() && instWidgets.at(cursor).isString()
                    && controlWords().contains(instWidgets.at(cursor).toString())) cursor++;
            }
        }

        // newer files: promoted widgets name the inner node + widget directly
        QHash<QString, QHash<QString, QJsonValue>> promoted;
        const QJsonArray proxy = inst.properties.value("proxyWidgets").toArray();
        for (int i = 0; i < proxy.size() && i < instWidgets.size(); i++) {
            const QJsonArray p = proxy.at(i).toArray();
            if (p.size() < 2) continue;
            const QString innerId = idString(p.at(0));
            if (innerId.startsWith('-')) continue;
            promoted[prefix + innerId].insert(p.at(1).toString(), instWidgets.at(i));
        }

        QHash<QString, int> innerIndex;
        for (const QJsonValue &nv : def.value("nodes").toArray()) {
            UNode n = parseNode(nv.toObject(), prefix);
            if (inst.mode == 2 || inst.mode == 4) n.mode = inst.mode;
            n.overrides = promoted.value(n.id);
            innerIndex.insert(n.id, g.nodes.size());
            g.addNode(n);
        }

        // inner links, wired to whatever feeds / reads the instance
        QHash<int, QPair<QString, int>> outMap; // subgraph output slot -> inner origin
        for (const QJsonValue &lv : def.value("links").toArray()) {
            ULink l = parseLink(lv, prefix);
            if (l.origin == "-10") {
                const QString name = defInputs.at(l.originSlot).toObject().value("name").toString();
                const ULink *ext = instSlot.contains(name) ? g.linkInto(inst.id, instSlot.value(name)) : nullptr;
                if (ext) {
                    l.origin = ext->origin;
                    l.originSlot = ext->originSlot;
                } else {
                    // Not wired from outside: the input is a widget on the instance. Its value is
                    // the instance's (older files) or that of the first inner widget it drives,
                    // and it feeds every inner target, sockets included.
                    const QString vid = prefix + "in" + QString::number(l.originSlot);
                    if (!g.node(vid)) {
                        UNode v;
                        v.id = vid;
                        v.type = QStringLiteral("__SubgraphInput");
                        v.mode = inst.mode;
                        if (inputValue.contains(l.originSlot)) v.literal = inputValue.value(l.originSlot);
                        g.addNode(v);
                    }
                    l.origin = vid;
                    l.originSlot = 0;
                }
            }
            if (l.target == "-20") {
                outMap.insert(l.targetSlot, qMakePair(l.origin, l.originSlot));
                continue;
            }
            g.links.append(l);
        }
        for (ULink &l : g.links) {
            if (l.dead) continue;
            if (l.origin == inst.id) {
                if (outMap.contains(l.originSlot)) {
                    const auto src = outMap.value(l.originSlot);
                    l.origin = src.first;
                    l.originSlot = src.second;
                } else {
                    l.dead = true;
                }
            }
            if (l.target == inst.id) l.dead = true;
        }
        g.removeNode(inst.id);
    }
    if (error) *error = QStringLiteral("Subgraphs nest too deeply.");
    return false;
}

struct Source {
    enum Kind { None, Link, Literal } kind {None};
    QString id;
    int slot {0};
    QJsonValue literal;
};

Source resolve(const FlatGraph &g, const QString &origin, int slot, int depth = 0)
{
    Source s;
    if (depth > 64) return s;
    const UNode *n = g.node(origin);
    if (!n) return s;
    if (n->type == "Reroute") {
        const ULink *l = g.linkInto(origin, 0);
        return l ? resolve(g, l->origin, l->originSlot, depth + 1) : s;
    }
    if (n->type == "__SubgraphInput") {
        if (!n->literal.isUndefined() && !n->literal.isNull()) { s.kind = Source::Literal; s.literal = n->literal; }
        return s;
    }
    if (n->type == "PrimitiveNode") {
        const QJsonArray w = n->widgets.toArray();
        if (!w.isEmpty()) { s.kind = Source::Literal; s.literal = w.at(0); }
        return s;
    }
    if (n->mode == 2) return s; // muted: nothing comes out
    if (n->mode == 4) {         // bypassed: pass the matching input through
        const QString outType = n->outputs.at(slot).toObject().value("type").toString();
        for (int k = 0; k < n->inputs.size(); k++) {
            const QString inType = n->inputs.at(k).toObject().value("type").toString();
            if (inType == outType || outType == "*") {
                const ULink *l = g.linkInto(origin, k);
                if (l) return resolve(g, l->origin, l->originSlot, depth + 1);
            }
        }
        return s;
    }
    s.kind = Source::Link;
    s.id = origin;
    s.slot = slot;
    return s;
}

QJsonValue widgetValue(const QJsonValue &widgets, int &cursor, const QString &name)
{
    if (widgets.isObject()) return widgets.toObject().value(name);
    const QJsonArray a = widgets.toArray();
    if (cursor < a.size()) return a.at(cursor++);
    return QJsonValue(QJsonValue::Undefined);
}

// natural sort for API node ids ("2" < "10" < "10:3")
bool idLess(const QString &a, const QString &b)
{
    const QStringList pa = a.split(':'), pb = b.split(':');
    for (int i = 0; i < qMin(pa.size(), pb.size()); i++) {
        bool oka, okb;
        const qint64 na = pa[i].toLongLong(&oka), nb = pb[i].toLongLong(&okb);
        if (oka && okb && na != nb) return na < nb;
        if (!(oka && okb) && pa[i] != pb[i]) return pa[i] < pb[i];
    }
    return pa.size() < pb.size();
}

QStringList sortedIds(const QJsonObject &api)
{
    QStringList ids = api.keys();
    std::sort(ids.begin(), ids.end(), idLess);
    return ids;
}

QString prettyName(const QString &input)
{
    QString s = input;
    s.replace('_', ' ');
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

QString nodeTitle(const QJsonObject &node)
{
    const QString t = node.value("_meta").toObject().value("title").toString();
    return t.isEmpty() ? node.value("class_type").toString() : t;
}

using Consumers = QHash<QString, QVector<QPair<QString, QString>>>; // "id#slot" -> (node, input)

Consumers buildConsumers(const QJsonObject &api)
{
    Consumers c;
    for (auto it = api.begin(); it != api.end(); ++it) {
        const QJsonObject inputs = it.value().toObject().value("inputs").toObject();
        for (auto in = inputs.begin(); in != inputs.end(); ++in) {
            if (!isLink(in.value())) continue;
            const QJsonArray a = in.value().toArray();
            c[idString(a.at(0)) + "#" + QString::number(a.at(1).toInt())].append(qMakePair(it.key(), in.key()));
        }
    }
    return c;
}

/// Does anything downstream of node id plug into an input called "negative"?
int conditioningRole(const QJsonObject &api, const Consumers &cons, const QString &id)
{
    QSet<QString> seen;
    QVector<QPair<QString, int>> queue {qMakePair(id, 0)};
    while (!queue.isEmpty()) {
        const auto cur = queue.takeFirst();
        if (cur.second > 10 || seen.contains(cur.first)) continue;
        seen.insert(cur.first);
        const QJsonObject node = api.value(cur.first).toObject();
        Q_UNUSED(node);
        for (int slot = 0; slot < 4; slot++) {
            for (const auto &t : cons.value(cur.first + "#" + QString::number(slot))) {
                if (t.second == "negative") return -1;
                if (t.second == "positive") return 1;
                queue.append(qMakePair(t.first, cur.second + 1));
            }
        }
    }
    return 0;
}

const QStringList kGroupOrder {"Parameters", "Prompts", "Models", "Sampling", "Size", "Inputs"};

int groupRank(const QString &g)
{
    const int i = kGroupOrder.indexOf(g);
    return i < 0 ? kGroupOrder.size() : i;
}

bool isPlaceholder(const QString &cls)
{
    return cls.startsWith("ETN_Krita") || cls == "ETN_Parameter";
}

void setInput(QJsonObject &api, const QString &id, const QString &input, const QJsonValue &v)
{
    QJsonObject node = api.value(id).toObject();
    QJsonObject inputs = node.value("inputs").toObject();
    if (isLink(inputs.value(input))) return; // wired in the graph: leave it
    inputs.insert(input, v);
    node.insert("inputs", inputs);
    api.insert(id, node);
}

QJsonObject makeNode(const QString &cls, const QJsonObject &inputs, const QString &title)
{
    QJsonObject n;
    n.insert("class_type", cls);
    n.insert("inputs", inputs);
    n.insert("_meta", QJsonObject {{"title", title}});
    return n;
}

int roundTo(int v, int m)
{
    return qMax(m * 4, int(std::lround(double(v) / m)) * m);
}

QJsonValue typedParamValue(const Control &c, const QJsonValue &v)
{
    switch (c.kind) {
    case Control::Int:
    case Control::Seed:
        return QJsonValue(qint64(v.toVariant().toLongLong()));
    case Control::Float:
        return QJsonValue(v.toVariant().toDouble());
    case Control::Bool:
        return QJsonValue(v.toVariant().toBool());
    default:
        return QJsonValue(v.toVariant().toString());
    }
}

} // namespace

// ---------------------------------------------------------------------------

bool isUiFormat(const QJsonObject &w)
{
    return w.value("nodes").isArray() && w.contains("links");
}

bool isApiFormat(const QJsonObject &w)
{
    if (w.isEmpty() || isUiFormat(w)) return false;
    for (auto it = w.begin(); it != w.end(); ++it) {
        if (!it.value().toObject().contains("class_type")) return false;
    }
    return true;
}

namespace {

// ---------------------------------------------------------------------------
// key order of every object in a JSON document (QJsonObject sorts its keys)

class KeyOrderScanner
{
public:
    KeyOrderScanner(const QByteArray &raw, QHash<QString, QStringList> &out)
        : m_p(raw.constData()), m_end(raw.constData() + raw.size()), m_out(out) {}

    void run() { value(QString(), 0); }

private:
    void ws() { while (m_p < m_end && (*m_p == ' ' || *m_p == '\n' || *m_p == '\r' || *m_p == '\t')) m_p++; }

    QString str()
    {
        // m_p at the opening quote
        QByteArray buf;
        m_p++;
        while (m_p < m_end && *m_p != '"') {
            if (*m_p == '\\' && m_p + 1 < m_end) {
                const char e = m_p[1];
                if (e == 'u' && m_p + 5 < m_end) {
                    const ushort cp = QByteArray(m_p + 2, 4).toUShort(nullptr, 16);
                    buf += QString(QChar(cp)).toUtf8();
                    m_p += 6;
                    continue;
                }
                buf += (e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'b' ? '\b' : e == 'f' ? '\f' : e);
                m_p += 2;
                continue;
            }
            buf += *m_p++;
        }
        m_p++;
        return QString::fromUtf8(buf);
    }

    void value(const QString &path, int depth)
    {
        ws();
        if (m_p >= m_end) return;
        if (*m_p == '{') {
            m_p++;
            QStringList keys;
            for (;;) {
                ws();
                if (m_p >= m_end) return;
                if (*m_p == '}') { m_p++; break; }
                if (*m_p == ',') { m_p++; continue; }
                if (*m_p != '"') { m_p++; continue; }
                const QString key = str();
                keys << key;
                ws();
                if (m_p < m_end && *m_p == ':') m_p++;
                value(path + QChar(0x1f) + key, depth + 1);
            }
            if (depth < 14) m_out.insert(path, keys);
        } else if (*m_p == '[') {
            m_p++;
            int i = 0;
            for (;;) {
                ws();
                if (m_p >= m_end) return;
                if (*m_p == ']') { m_p++; break; }
                if (*m_p == ',') { m_p++; continue; }
                value(path + QChar(0x1f) + QString::number(i++), depth + 1);
            }
        } else if (*m_p == '"') {
            str();
        } else {
            while (m_p < m_end && *m_p != ',' && *m_p != '}' && *m_p != ']') m_p++;
        }
    }

    const char *m_p;
    const char *m_end;
    QHash<QString, QStringList> &m_out;
};

QString joinPath(std::initializer_list<QString> parts)
{
    QString s;
    for (const QString &p : parts) s += QChar(0x1f) + p;
    return s;
}

/// one input of a node class, with where its spec lives in object_info
struct Field {
    QString name;
    QJsonArray spec;
    QString path;
};

/// inputs of a {"required": {...}, "optional": {...}} dict, in file order
QVector<Field> fieldsOf(const QJsonObject &dict, const QString &dictPath, const NodeInfo &ni)
{
    QVector<Field> out;
    for (const char *cat : {"required", "optional"}) {
        const QJsonObject d = dict.value(cat).toObject();
        const QString catPath = dictPath + QChar(0x1f) + cat;
        QStringList keys = ni.keyOrder.value(catPath);
        if (keys.isEmpty()) keys = d.keys();
        for (const QString &k : keys) {
            if (d.contains(k)) out.append({k, d.value(k).toArray(), catPath + QChar(0x1f) + k});
        }
    }
    return out;
}

QVector<Field> classFields(const QString &cls, const NodeInfo &ni)
{
    const QJsonObject def = ni.info.value(cls).toObject();
    const QJsonObject in = def.value("input").toObject();
    const QString inPath = joinPath({cls, QStringLiteral("input")});
    // input_order is authoritative when the server sends it
    const QJsonObject order = def.value("input_order").toObject();
    if (!order.isEmpty()) {
        QVector<Field> out;
        for (const char *cat : {"required", "optional"}) {
            for (const QJsonValue &n : order.value(cat).toArray()) {
                const QString name = n.toString();
                const QJsonValue spec = in.value(cat).toObject().value(name);
                if (spec.isArray()) out.append({name, spec.toArray(), inPath + QChar(0x1f) + cat + QChar(0x1f) + name});
            }
        }
        return out;
    }
    return fieldsOf(in, inPath, ni);
}

} // namespace

NodeInfo NodeInfo::fromJson(const QByteArray &raw)
{
    NodeInfo ni;
    ni.info = QJsonDocument::fromJson(raw).object();
    KeyOrderScanner(raw, ni.keyOrder).run();
    return ni;
}

namespace {

struct ConvertContext {
    const FlatGraph &g;
    const NodeInfo &ni;
    const UNode &n;
    QJsonObject inputs;
    int cursor {0};
    QSet<QString> handled; ///< UI input names already wired
    bool wire {true};      ///< false: widget values only
};

void wireInput(ConvertContext &c, const QString &apiName)
{
    c.handled.insert(apiName);
    if (!c.wire) return;
    int slot = -1;
    for (int k = 0; k < c.n.inputs.size(); k++) {
        if (c.n.inputs.at(k).toObject().value("name").toString() == apiName) { slot = k; break; }
    }
    if (slot < 0) return;
    const ULink *l = c.g.linkInto(c.n.id, slot);
    if (!l) return;
    const Source s = resolve(c.g, l->origin, l->originSlot);
    if (s.kind == Source::Link) c.inputs.insert(apiName, QJsonArray {s.id, s.slot});
    else if (s.kind == Source::Literal) c.inputs.insert(apiName, s.literal);
    // None (muted source): keep the widget value, if any
}

void convertFields(ConvertContext &c, const QVector<Field> &fields, const QString &prefix)
{
    const QJsonArray w = c.n.widgets.toArray();
    for (const Field &f : fields) {
        const QString apiName = prefix + f.name;
        const QString t = specType(f.spec);
        const QJsonObject o = specOptions(f.spec);

        if (t == "COMFY_DYNAMICCOMBO_V3") {
            // the chosen option, then that option's own inputs right after it
            const QJsonValue v = widgetValue(c.n.widgets, c.cursor, apiName);
            if (!v.isUndefined()) c.inputs.insert(apiName, v);
            else if (o.contains("default")) c.inputs.insert(apiName, o.value("default"));
            const QString key = c.inputs.value(apiName).toVariant().toString();
            const QJsonArray opts = o.value("options").toArray();
            for (int i = 0; i < opts.size(); i++) {
                const QJsonObject opt = opts.at(i).toObject();
                if (opt.value("key").toVariant().toString() != key) continue;
                const QString optPath = f.path + QChar(0x1f) + "1" + QChar(0x1f) + "options" + QChar(0x1f)
                                        + QString::number(i) + QChar(0x1f) + "inputs";
                convertFields(c, fieldsOf(opt.value("inputs").toObject(), optPath, c.ni), apiName + ".");
                break;
            }
            wireInput(c, apiName);
            continue;
        }
        if (t == "COMFY_AUTOGROW_V3" || t == "COMFY_DYNAMICSLOT_V3") {
            continue; // their sockets show up as "name.sub" UI inputs, wired below
        }

        if (isWidgetSpec(f.spec)) {
            const QJsonValue v = widgetValue(c.n.widgets, c.cursor, apiName);
            if (!v.isUndefined() && !v.isNull()) c.inputs.insert(apiName, v);
            else if (o.contains("default")) c.inputs.insert(apiName, o.value("default")); // saved before the input existed
            // the "control after generate" combo sits right after seeds
            if (c.n.widgets.isArray() && c.cursor < w.size() && w.at(c.cursor).isString()
                && controlWords().contains(w.at(c.cursor).toString())
                && (t == "INT" || t == "FLOAT" || t == "COMBO" || o.value("control_after_generate").toBool())) {
                c.cursor++;
            }
        }
        if (c.n.overrides.contains(f.name)) c.inputs.insert(apiName, c.n.overrides.value(f.name));
        if (c.n.overrides.contains(apiName)) c.inputs.insert(apiName, c.n.overrides.value(apiName));
        wireInput(c, apiName);
    }
}

} // namespace

QJsonObject convertUiToApi(const QJsonObject &ui, const NodeInfo &ni, QString *error)
{
    if (ni.isEmpty()) {
        if (error) *error = QStringLiteral("Not connected: converting a workflow needs the server's node list.");
        return QJsonObject();
    }

    FlatGraph g;
    for (const QJsonValue &nv : ui.value("nodes").toArray()) g.addNode(parseNode(nv.toObject(), QString()));
    for (const QJsonValue &lv : ui.value("links").toArray()) g.links.append(parseLink(lv, QString()));

    QHash<QString, QJsonObject> defs;
    for (const QJsonValue &d : ui.value("definitions").toObject().value("subgraphs").toArray()) {
        const QJsonObject o = d.toObject();
        defs.insert(o.value("id").toString(), o);
    }
    if (!defs.isEmpty() && !expandSubgraphs(g, defs, error)) return QJsonObject();

    static const QSet<QString> uiOnly {"Reroute", "PrimitiveNode", "Note", "MarkdownNote", "__SubgraphInput"};

    // subgraph widget inputs without a value of their own take the value of
    // the first inner widget they drive
    for (UNode &v : g.nodes) {
        if (v.type != "__SubgraphInput" || !(v.literal.isUndefined() || v.literal.isNull())) continue;
        for (const ULink &l : g.links) {
            if (l.dead || l.origin != v.id) continue;
            const UNode *t = g.node(l.target);
            if (!t || !ni.info.contains(t->type)) continue;
            const QString name = t->inputs.at(l.targetSlot).toObject().value("name").toString();
            ConvertContext c {g, ni, *t, QJsonObject(), 0, QSet<QString>(), false};
            convertFields(c, classFields(t->type, ni), QString());
            const QJsonValue val = c.inputs.value(name);
            if (!val.isUndefined() && !val.isNull()) { v.literal = val; break; }
        }
    }

    QJsonObject api;
    for (const UNode &n : g.nodes) {
        if (n.type.isEmpty() || uiOnly.contains(n.type) || n.mode == 2 || n.mode == 4) continue;

        const QJsonObject def = ni.info.value(n.type).toObject();
        if (def.isEmpty()) {
            // UI-only helpers (group mutters, labels...) have nothing wired out
            if (!g.hasLinksFrom(n.id)) continue;
            if (error) *error = QStringLiteral("The workflow uses \"%1\", which isn't installed on the ComfyUI server.").arg(n.type);
            return QJsonObject();
        }

        ConvertContext c {g, ni, n, QJsonObject(), 0, QSet<QString>(), true};
        convertFields(c, classFields(n.type, ni), QString());
        // dynamic sockets ("values.a", "images.image0") aren't in the class spec
        for (int k = 0; k < n.inputs.size(); k++) {
            const QString name = n.inputs.at(k).toObject().value("name").toString();
            if (!name.isEmpty() && !c.handled.contains(name) && g.linkInto(n.id, k)) wireInput(c, name);
        }

        QJsonObject out;
        out.insert("class_type", n.type);
        out.insert("inputs", c.inputs);
        out.insert("_meta", QJsonObject {{"title", n.title.isEmpty() ? def.value("display_name").toString(n.type) : n.title}});
        api.insert(n.id, out);
    }

    if (api.isEmpty() && error) *error = QStringLiteral("The workflow has no nodes that can run.");
    return api;
}

QJsonObject toApi(const QJsonObject &workflow, const NodeInfo &nodes, QString *error)
{
    if (isUiFormat(workflow)) return convertUiToApi(workflow, nodes, error);
    if (isApiFormat(workflow)) return workflow;
    if (error) *error = QStringLiteral("This doesn't look like a ComfyUI workflow.");
    return QJsonObject();
}

// ---------------------------------------------------------------------------

QVector<Control> analyse(const QJsonObject &api, const NodeInfo &nodes)
{
    const QJsonObject &objectInfo = nodes.info;
    QVector<Control> out;
    const Consumers cons = buildConsumers(api);
    int order = 0;
    // with a Krita Canvas node the canvas already goes in that way: extra
    // Load Image nodes are references, best picked on the device
    bool hasCanvasNode = false;
    for (auto it = api.begin(); it != api.end(); ++it) {
        if (it.value().toObject().value("class_type").toString() == "ETN_KritaCanvas") hasCanvasNode = true;
    }
    bool firstImage = !hasCanvasNode;

    static const QSet<QString> classicModelInputs {"ckpt_name", "unet_name", "lora_name", "model_name", "vae_name",
                                                   "clip_name", "control_net_name"};
    // all-in-one nodes (e.g. the DH ones) name their model pickers differently
    static const QSet<QString> modelInputs = classicModelInputs + QSet<QString> {
        "clip_name1", "clip_name2", "clip_name3", "diffusion_model", "text_encoder", "vae", "clip", "model",
        "checkpoint", "upscale_model", "lora", "lora_1", "lora_2", "lora_3"};
    static const QSet<QString> samplingNumbers {"steps", "cfg", "denoise", "guidance", "shift"};
    static const QSet<QString> samplingChoices {"sampler_name", "scheduler"};
    static const QRegularExpression orderPrefix("^\\s*(\\d+)\\.\\s*");

    for (const QString &id : sortedIds(api)) {
        const QJsonObject node = api.value(id).toObject();
        const QString cls = node.value("class_type").toString();
        const QJsonObject inputs = node.value("inputs").toObject();
        const QString title = nodeTitle(node);

        if (cls == "ETN_Parameter") {
            Control c;
            QString name = inputs.value("name").toString("Parameter");
            c.key = "param:" + name;
            c.order = 1000 + order++;
            const int slash = name.lastIndexOf('/');
            if (slash > 0) { c.group = name.left(slash); name = name.mid(slash + 1); }
            else c.group = "Parameters";
            const auto m = orderPrefix.match(name);
            if (m.hasMatch()) { c.order = m.captured(1).toInt(); name = name.mid(m.capturedLength()); }
            c.label = name;

            // what does it feed? (gives the type for "auto" and combo choices)
            QJsonArray target;
            const auto targets = cons.value(id + "#0");
            if (!targets.isEmpty()) {
                const QJsonObject tn = api.value(targets.first().first).toObject();
                target = fieldSpec(nodeDef(objectInfo, tn.value("class_type").toString()), targets.first().second);
            }
            const QString tt = specType(target);
            const bool multiline = specOptions(target).value("multiline").toBool();
            const QString type = inputs.value("type").toString("auto");
            if (type == "prompt (positive)") c.kind = Control::Prompt;
            else if (type == "prompt (negative)") c.kind = Control::NegativePrompt;
            else if (type == "toggle") c.kind = Control::Bool;
            else if (type == "number") c.kind = Control::Float;
            else if (type == "number (integer)") c.kind = Control::Int;
            else if (type == "choice") c.kind = Control::Choice;
            else if (type == "text") c.kind = multiline ? Control::Prompt : Control::Text;
            else if (tt == "INT") c.kind = Control::Int;
            else if (tt == "FLOAT") c.kind = Control::Float;
            else if (tt == "BOOLEAN" || tt == "BOOL") c.kind = Control::Bool;
            else if (tt == "COMBO") c.kind = Control::Choice;
            else c.kind = multiline ? Control::Prompt : Control::Text;
            if (c.kind == Control::Choice) c.options = comboOptions(target);

            const QString def = inputs.value("default").toVariant().toString();
            switch (c.kind) {
            case Control::Int: c.value = QJsonValue(qint64(def.toLongLong())); break;
            case Control::Float: c.value = def.toDouble(); break;
            case Control::Bool: c.value = (def == "true" || def == "1" || def == "True"); break;
            case Control::Choice: c.value = (def.isEmpty() && !c.options.isEmpty()) ? c.options.first() : def; break;
            default: c.value = def;
            }
            const double mn = inputs.value("min").toDouble(-1e10), mx = inputs.value("max").toDouble(1e10);
            const QJsonObject to = specOptions(target);
            if (mn > -1e9 && mx < 1e9) { c.min = mn; c.max = mx; c.hasRange = true; }
            else if (to.contains("min") && to.contains("max")) { c.min = to.value("min").toDouble(); c.max = to.value("max").toDouble(); c.hasRange = true; }
            c.step = to.value("step").toDouble(0.0);
            out.append(c);
            continue;
        }
        if (isPlaceholder(cls)) continue;

        const QJsonObject def = nodeDef(objectInfo, cls);
        bool sizeDone = false;
        for (const QString &name : inputOrder(def)) {
            const QJsonValue v = inputs.value(name);
            if (v.isUndefined() || isLink(v)) continue;
            const QJsonArray spec = fieldSpec(def, name);
            const QString t = specType(spec);
            const QJsonObject o = specOptions(spec);

            Control c;
            c.nodeId = id;
            c.input = name;
            c.key = id + "/" + name;
            c.value = v;
            c.order = order++;

            if (cls == "LoadImage" && name == "image") {
                c.kind = Control::ImageInput;
                c.group = "Inputs";
                c.label = title;
                c.options = QStringList {QStringLiteral("Canvas"), QStringLiteral("Active layer"),
                                         QStringLiteral("Image from device…"), QStringLiteral("File: %1").arg(v.toString())};
                c.value = firstImage ? 0 : (hasCanvasNode ? 2 : 3);
                firstImage = false;
            } else if (cls == "LoadImageMask" && name == "image") {
                c.kind = Control::MaskInput;
                c.group = "Inputs";
                c.label = title;
                c.options = QStringList {QStringLiteral("Selection"), QStringLiteral("File: %1").arg(v.toString())};
                c.value = 0;
            } else if (t == "STRING" && (name == "negative_prompt" || name == "negative")) {
                c.kind = Control::NegativePrompt;
                c.group = "Prompts";
                c.label = QStringLiteral("Negative");
                c.order -= 50000;
            } else if (t == "STRING" && (name == "text" || name == "prompt" || name == "positive_prompt")) {
                const int role = name == "text" ? conditioningRole(api, cons, id) : 1;
                c.kind = role < 0 ? Control::NegativePrompt : Control::Prompt;
                c.group = "Prompts";
                c.label = role < 0 ? QStringLiteral("Negative") : (role > 0 ? QStringLiteral("Prompt") : title);
                c.order -= 100000; // prompts first within the group
            } else if (t == "INT" && (name == "seed" || name == "noise_seed")) {
                c.kind = Control::Seed;
                c.group = "Sampling";
                c.label = QStringLiteral("Seed");
                c.value = QJsonObject {{"value", v}, {"random", true}};
            } else if ((t == "INT" || t == "FLOAT") && (samplingNumbers.contains(name) || name.startsWith("strength"))) {
                c.kind = t == "INT" ? Control::Int : Control::Float;
                c.group = name.startsWith("strength") ? "Models" : "Sampling";
                c.label = name.startsWith("strength") ? title + " · " + prettyName(name) : prettyName(name);
                if (o.contains("min") && o.contains("max")) {
                    c.min = o.value("min").toDouble();
                    c.max = o.value("max").toDouble();
                    c.hasRange = true;
                }
                c.step = o.value("step").toDouble(0.0);
            } else if (t == "COMBO" && modelInputs.contains(name)) {
                c.kind = Control::Choice;
                c.group = "Models";
                c.label = classicModelInputs.contains(name) ? title : title + " · " + prettyName(name);
                c.options = comboOptions(spec);
            } else if (t == "COMBO" && samplingChoices.contains(name)) {
                c.kind = Control::Choice;
                c.group = "Sampling";
                c.label = prettyName(name);
                c.options = comboOptions(spec);
            } else if (!sizeDone && t == "INT" && (name == "width" || name == "height")
                       && inputs.value("width").isDouble() && inputs.value("height").isDouble()) {
                sizeDone = true;
                c.kind = Control::CanvasSize;
                c.group = "Size";
                c.input.clear();
                c.key = id + "/size";
                c.label = QStringLiteral("Match canvas size (%1)").arg(title);
                // empty latents: on by default; other sized nodes (resizers, AIO generators): opt in
                c.value = cls.contains("Latent");
            } else {
                continue;
            }
            out.append(c);
        }
    }

    std::stable_sort(out.begin(), out.end(), [](const Control &a, const Control &b) {
        const int ga = groupRank(a.group), gb = groupRank(b.group);
        if (ga != gb) return ga < gb;
        if (a.group != b.group) return a.group < b.group;
        return a.order < b.order;
    });
    return out;
}

static QJsonValue valueOf(const Control &c, const QHash<QString, QJsonValue> &values)
{
    return values.contains(c.key) ? values.value(c.key) : c.value;
}

bool needsCanvas(const QJsonObject &api, const QVector<Control> &controls, const QHash<QString, QJsonValue> &values)
{
    for (auto it = api.begin(); it != api.end(); ++it) {
        const QString cls = it.value().toObject().value("class_type").toString();
        if (cls == "ETN_KritaCanvas" || cls == "ETN_KritaImageLayer") return true;
    }
    for (const Control &c : controls) {
        if (c.kind == Control::ImageInput && valueOf(c, values).toInt() == 0) return true;
    }
    return false;
}

bool needsSelectionMask(const QJsonObject &api, const QVector<Control> &controls, const QHash<QString, QJsonValue> &values)
{
    for (auto it = api.begin(); it != api.end(); ++it) {
        const QString cls = it.value().toObject().value("class_type").toString();
        if (cls == "ETN_KritaSelection" || cls == "ETN_KritaMaskLayer") return true;
    }
    for (const Control &c : controls) {
        if (c.kind == Control::MaskInput && valueOf(c, values).toInt() == 0) return true;
    }
    return false;
}

Prepared prepare(const QJsonObject &apiIn, const QVector<Control> &controls,
                 const QHash<QString, QJsonValue> &values, const KritaInputs &k)
{
    Prepared r;
    QJsonObject api = apiIn;
    qint64 seedSalt = 0;

    // 1. knobs
    for (const Control &c : controls) {
        const QJsonValue v = valueOf(c, values);
        if (c.nodeId.isEmpty()) continue; // placeholder parameters: step 2
        switch (c.kind) {
        case Control::Seed: {
            const QJsonObject s = v.toObject();
            const qint64 seed = s.value("random").toBool(true) ? (k.seed + 7919 * seedSalt++) & 0x3FFFFFFFFFFFLL
                                                               : qint64(s.value("value").toVariant().toLongLong());
            setInput(api, c.nodeId, c.input, QJsonValue(seed));
            break;
        }
        case Control::ImageInput:
            if (v.toInt() == 0) {
                if (k.canvasFile.isEmpty()) { r.error = QStringLiteral("No canvas to send."); return r; }
                setInput(api, c.nodeId, c.input, k.canvasFile);
            } else if (v.toInt() == 1 || v.toInt() == 2) {
                const QString f = k.imageFiles.value(c.key);
                if (f.isEmpty()) {
                    r.error = v.toInt() == 1 ? QStringLiteral("%1: no active layer to send.").arg(c.label)
                                             : QStringLiteral("%1: pick an image first.").arg(c.label);
                    return r;
                }
                setInput(api, c.nodeId, c.input, f);
            }
            break;
        case Control::MaskInput:
            if (v.toInt() == 0) {
                setInput(api, c.nodeId, c.input, k.selectionMaskFile);
                setInput(api, c.nodeId, QStringLiteral("channel"), QStringLiteral("red"));
            }
            break;
        case Control::CanvasSize:
            if (v.toBool() && k.rect.isValid()) {
                setInput(api, c.nodeId, QStringLiteral("width"), roundTo(k.rect.width(), 16));
                setInput(api, c.nodeId, QStringLiteral("height"), roundTo(k.rect.height(), 16));
            }
            break;
        default:
            setInput(api, c.nodeId, c.input, typedParamValue(c, v));
        }
    }

    // 2. Krita placeholders -> core nodes / literal values
    QHash<QString, QJsonValue> remap; // "id#slot" -> replacement (link or literal)
    QHash<QString, Control> params;
    for (const Control &c : controls) {
        if (c.nodeId.isEmpty()) params.insert(c.key, c);
    }
    bool kritaOutput = false;
    QSet<QString> kritaOutputIds;
    const QStringList ids = sortedIds(api);
    for (const QString &id : ids) {
        const QJsonObject node = api.value(id).toObject();
        const QString cls = node.value("class_type").toString();
        const QJsonObject in = node.value("inputs").toObject();
        auto ref = [](const QString &nid, int slot) { return QJsonValue(QJsonArray {nid, slot}); };

        if (cls == "ETN_KritaCanvas" || cls == "ETN_KritaImageLayer") {
            if (k.canvasFile.isEmpty()) { r.error = QStringLiteral("No canvas to send."); return r; }
            api.insert(id, makeNode("LoadImage", {{"image", k.canvasFile}}, "Krita canvas"));
            api.insert(id + "_mask", makeNode("LoadImageMask", {{"image", k.canvasMaskFile}, {"channel", "red"}}, "Krita canvas alpha"));
            remap.insert(id + "#0", ref(id, 0));
            if (cls == "ETN_KritaCanvas") {
                remap.insert(id + "#1", k.rect.width());
                remap.insert(id + "#2", k.rect.height());
                remap.insert(id + "#3", QJsonValue(k.seed));
                remap.insert(id + "#4", ref(id + "_mask", 0));
            } else {
                remap.insert(id + "#1", ref(id + "_mask", 0));
            }
        } else if (cls == "ETN_KritaSelection" || cls == "ETN_KritaMaskLayer") {
            api.insert(id, makeNode("LoadImageMask", {{"image", k.selectionMaskFile}, {"channel", "red"}}, "Krita selection"));
            remap.insert(id + "#0", ref(id, 0));
            remap.insert(id + "#1", k.hasSelection);
            remap.insert(id + "#2", k.rect.x());
            remap.insert(id + "#3", k.rect.y());
        } else if (cls == "ETN_Parameter") {
            const QString key = "param:" + in.value("name").toString("Parameter");
            const Control c = params.value(key);
            remap.insert(id + "#0", typedParamValue(c, valueOf(c, values)));
            api.remove(id);
        } else if (cls == "ETN_KritaSendText") {
            api.remove(id);
        } else if (cls == "ETN_KritaOutput" || cls == "ETN_SendImageWebSocket" || cls == "SaveImageWebsocket") {
            OutputNode o;
            o.nodeId = id;
            if (cls == "ETN_KritaOutput") {
                kritaOutput = true;
                kritaOutputIds.insert(id);
                o.offsetX = in.value("x").isDouble() ? in.value("x").toInt() : 0;
                o.offsetY = in.value("y").isDouble() ? in.value("y").toInt() : 0;
                o.name = in.value("name").toString();
                o.keepSize = in.value("resize_canvas").toBool(false);
            }
            api.insert(id, makeNode("PreviewImage", {{"images", in.value("images")}}, "Krita output"));
            r.outputs.append(o);
        }
    }

    // rewire everything that pointed at a placeholder
    for (auto it = api.begin(); it != api.end(); ++it) {
        QJsonObject node = it.value().toObject();
        QJsonObject in = node.value("inputs").toObject();
        bool changed = false;
        for (auto i = in.begin(); i != in.end(); ++i) {
            if (!isLink(i.value())) continue;
            const QJsonArray a = i.value().toArray();
            const QString key = idString(a.at(0)) + "#" + QString::number(a.at(1).toInt());
            if (remap.contains(key)) {
                const QJsonValue rep = remap.value(key);
                // a link that pointed at the placeholder itself must not loop back
                if (!(rep.isArray() && idString(rep.toArray().at(0)) == idString(a.at(0)) && rep.toArray().at(1).toInt() == a.at(1).toInt())) {
                    i.value() = rep;
                    changed = true;
                }
            }
        }
        if (changed) { node.insert("inputs", in); it.value() = node; }
    }

    // 3. outputs: Krita Output wins; otherwise every Save / Preview Image
    if (!kritaOutput) {
        for (const QString &id : sortedIds(api)) {
            const QString cls = api.value(id).toObject().value("class_type").toString();
            const bool saves = cls == "SaveImage" || cls == "PreviewImage" || cls == "SaveImageAdvanced"
                || ((cls.contains("SaveImage") || cls.contains("PreviewImage") || cls.contains("Image Save"))
                    && !cls.contains("Websocket") && !cls.contains("DataSet") && !cls.startsWith("ETN_"));
            if (saves &&
                std::none_of(r.outputs.begin(), r.outputs.end(), [&](const OutputNode &o) { return o.nodeId == id; })) {
                OutputNode o;
                o.nodeId = id;
                r.outputs.append(o);
            }
        }
    } else {
        r.outputs.erase(std::remove_if(r.outputs.begin(), r.outputs.end(), [&](const OutputNode &o) {
            return !kritaOutputIds.contains(o.nodeId);
        }), r.outputs.end());
    }
    if (r.outputs.isEmpty()) {
        r.error = QStringLiteral("This workflow has no Save Image or Preview Image node, so there's nothing to bring back.");
        return r;
    }

    r.prompt = api;
    return r;
}

static bool isImageOutputClass(const QString &cls)
{
    return cls == "SaveImage" || cls == "PreviewImage" || cls == "SaveImageAdvanced" || cls == "ETN_KritaOutput"
        || cls == "ETN_SendImageWebSocket" || cls == "SaveImageWebsocket"
        || ((cls.contains("SaveImage") || cls.contains("PreviewImage") || cls.contains("Image Save"))
            && !cls.contains("DataSet"));
}

QString unsupportedReason(const QJsonObject &api)
{
    bool images = false;
    for (auto it = api.begin(); it != api.end(); ++it) {
        const QString cls = it.value().toObject().value("class_type").toString();
        if (cls.contains("3D") || cls.contains("Mesh") || cls.contains("Splat") || cls.contains("Trellis")
            || cls.contains("Hunyuan3D", Qt::CaseInsensitive)) {
            return QStringLiteral("This is a 3D workflow (%1), so there's nothing to put on the canvas.").arg(cls);
        }
        if (isImageOutputClass(cls)) images = true;
    }
    if (!images) return QStringLiteral("This workflow doesn't output images (no Save Image / Preview Image).");
    return QString();
}

QString describePromptError(const QJsonObject &resp)
{
    QStringList lines;
    const QJsonObject err = resp.value("error").toObject();
    if (!err.isEmpty()) {
        QString line = err.value("message").toString();
        const QString details = err.value("details").toString();
        if (!details.isEmpty()) line += ": " + details;
        lines << line;
    }
    const QJsonObject ne = resp.value("node_errors").toObject();
    for (auto it = ne.begin(); it != ne.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        for (const QJsonValue &e : o.value("errors").toArray()) {
            const QJsonObject eo = e.toObject();
            QString details = eo.value("details").toString();
            if (details.size() > 240) details = details.left(240) + "…";
            lines << QStringLiteral("%1 (#%2): %3%4").arg(o.value("class_type").toString(), it.key(),
                                                         eo.value("message").toString(),
                                                         details.isEmpty() ? QString() : " - " + details);
        }
    }
    if (lines.isEmpty()) lines << QStringLiteral("ComfyUI rejected the workflow.");
    return lines.join('\n');
}

QString describeHistoryError(const QJsonObject &entry)
{
    for (const QJsonValue &m : entry.value("status").toObject().value("messages").toArray()) {
        const QJsonArray a = m.toArray();
        if (a.size() < 2 || a.at(0).toString() != "execution_error") continue;
        const QJsonObject d = a.at(1).toObject();
        return QStringLiteral("%1 (#%2) failed: %3").arg(d.value("node_type").toString(),
                                                        idString(d.value("node_id")),
                                                        d.value("exception_message").toString().trimmed());
    }
    return QStringLiteral("The workflow failed on the server.");
}

} // namespace DhComfy
