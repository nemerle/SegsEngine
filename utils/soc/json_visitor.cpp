#include "json_visitor.h"

#include "type_system.h"

#include <QList>
#include <QJsonObject>
#include <QJsonArray>
namespace {

template <class T> T valFromJson(const QJsonValue &v);

template <class T> void setJsonIfNonDefault(QJsonObject &obj, const char *field, const T &v) {
    if (v != T()) {
        obj[field] = v;
    }
}

template <class T> void toJson(QJsonObject &tgt, const char *name, const QVector<T> &src) {
    if (src.isEmpty())
        return;
    QJsonArray entries;
    for (const T &c : src) {
        QJsonObject field;
        c.toJson(field);
        entries.push_back(field);
    }
    tgt[name] = entries;
}
template <class T> void fromJson(const QJsonObject &src, const char *name, QVector<T> &tgt) {
    if (!src.contains(name)) {
        tgt.clear();
        return;
    }
    assert(src[name].isArray());
    QJsonArray arr = src[name].toArray();
    tgt.reserve(arr.size());
    for (int i = 0; i < arr.size(); ++i) {
        T ci;
        ci.fromJson(arr[i].toObject());
        tgt.emplace_back(ci);
    }
}

struct JSonVisitor : public VisitorInterface {
    QList<QJsonObject> result;
    // VisitorInterface interface
    void entryToJSON(const TS_TypeLike *tl, TS_Base::TypeKind kind, QJsonObject &tgt) {
        QString entry_name;
        switch (kind) {
            case TS_Base::NAMESPACE:
                entry_name = "namespaces";
                break;
            case TS_Base::CLASS:
                entry_name = "subtypes";
                break;
            case TS_Base::ENUM:
                entry_name = "enums";
                break;
            case TS_Base::FUNCTION:
                entry_name = "functions";
                break;
            case TS_Base::CONSTANT:
                entry_name = "constants";
                break;
            case TS_Base::SIGNAL:
                entry_name = "signals";
                break;
            case TS_Base::PROPERTY:
                entry_name = "properties";
                break;
        }
        QJsonArray arr;
        tl->visit_kind(kind, [&](const TS_Base *e) {
            result.push_back(QJsonObject());
            e->accept(this);
            arr.push_back(result.takeLast());
        });
        if (!arr.empty()) {
            tgt[entry_name] = arr;
        }
    }
    void commonVisit(const TS_Base *self) {
        QJsonObject &current(result.back());

//        const char *tgt;
//        switch (self->kind()) {
//            case TS_Base::NAMESPACE:
//                tgt = "NAMESPACE";
//                break;
//            case TS_Base::CLASS:
//                tgt = "CLASS";
//                break;
//            case TS_Base::ENUM:
//                tgt = "ENUM";
//                break;
//            case TS_Base::FUNCTION:
//                tgt = "FUNCTION";
//                break;
//            case TS_Base::CONSTANT:
//                tgt = "CONSTANT";
//                break;
//            case TS_Base::PROPERTY:
//                tgt = "PROPERTY";
//                break;
//            case TS_Base::TYPE_REFERENCE:
//                tgt = "TYPE_REFERENCE";
//                break;
//        }

        current["name"] = self->name;
        //current["kind"] = tgt;
    }

    void commonVisit(const TS_TypeLike *self) {
        commonVisit((const TS_Base *)self);
        QJsonObject &current(result.back());
        if (!self->required_header.isEmpty())
            current["required_header"] = self->required_header;
    }
public:
    JSonVisitor() {
        // add first root
        result.push_back(QJsonObject());
    }
    void visit(const TS_Enum *vs) override {
        commonVisit(vs);
        QJsonObject &current(result.back());

        entryToJSON(vs, TS_Base::CONSTANT, current);

        if (vs->underlying_val_type.name != "int32_t") {
            current["underlying_type"] = vs->underlying_val_type.name;
        }
        if(vs->is_strict) {
            current["is_strict"] = true;
        }
    }
    void visit(const TS_Type *vs) override {
        commonVisit(vs);
        QJsonObject &current(result.back());
        QJsonObject root_obj;

        entryToJSON(vs, TS_Base::ENUM, root_obj);
        entryToJSON(vs, TS_Base::CONSTANT, root_obj);
        entryToJSON(vs, TS_Base::CLASS, root_obj);
        entryToJSON(vs, TS_Base::FUNCTION, root_obj);
        entryToJSON(vs, TS_Base::PROPERTY, root_obj);
        entryToJSON(vs, TS_Base::SIGNAL, root_obj);

        current["contents"] = root_obj;
        if(!vs->base_type.name.isEmpty()) {

            result.push_back(QJsonObject());
            visit(&vs->base_type);
            QJsonObject base_tp = result.takeLast();
            current["base_type"] = base_tp;
        }
    }
    void visit(const TS_Namespace *vs) override {
        commonVisit(vs);
        QJsonObject &current(result.back());
        QJsonObject root_obj;

        entryToJSON(vs, TS_Base::ENUM, root_obj);
        entryToJSON(vs, TS_Base::CONSTANT, root_obj);
        entryToJSON(vs, TS_Base::CLASS, root_obj);
        entryToJSON(vs, TS_Base::FUNCTION, root_obj);
        entryToJSON(vs, TS_Base::NAMESPACE, root_obj);

        current["contents"] = root_obj;
    }
    void visit(const TS_Property *) override {

    }
    void visit(const TS_Signal *fs) override {
        commonVisit(fs);

        QJsonObject &current(result.back());

        if(fs->arg_types.empty())
            return;
        QJsonArray array;
        for(int idx=0; idx<fs->arg_types.size(); ++idx) {
            QJsonObject arg_def;
            result.push_back(QJsonObject());
            visit(&fs->arg_types[idx]);
            arg_def["type"]=result.takeLast();
            arg_def["name"]=fs->arg_values[idx];
            auto iter=fs->arg_defaults.find(idx);
            if(iter!=fs->arg_defaults.end()) {
                arg_def["default_argument"] = *iter;
            }
            array.append(arg_def);
        }
        current["arguments"] = array;

    }
    void visit(const TS_Function *fs) override {
        commonVisit(fs);

        QJsonObject &current(result.back());

        QJsonObject return_type;
        result.push_back(return_type);
        visit(&fs->return_type);

        current["return_type"] = result.takeLast();

        if(fs->m_virtual) {
            current["is_virtual"] = fs->m_virtual;
        }
        if(fs->m_static) {
            current["is_static"] = fs->m_static;
        }

        if(fs->arg_types.empty())
            return;

        QJsonArray array;
        for(int idx=0; idx<fs->arg_types.size(); ++idx) {
            QJsonObject arg_def;
            result.push_back(QJsonObject());
            visit(&fs->arg_types[idx]);
            arg_def["type"]=result.takeLast();
            arg_def["name"]=fs->arg_values[idx];
            auto iter=fs->arg_defaults.find(idx);
            if(iter!=fs->arg_defaults.end()) {
                arg_def["default_argument"] = *iter;
            }
            array.append(arg_def);
        }
        current["arguments"] = array;
    }
    void visit(const TS_Constant *cn) override {
        QJsonObject &current(result.back());
        current["name"] = cn->name;
        current["value"] = cn->value;
        if (cn->enclosing_type->kind() != TS_Base::ENUM) {
            // only write out non-redundant info
            result.push_back(QJsonObject());
            cn->const_type.accept(this);
            current["type"] = result.takeLast();
        }
    }
    void visit(const TypeReference *tr) override {
        QJsonObject &current(result.back());
        current["name"] = tr->name;
        setJsonIfNonDefault(current, "is_enum", (int8_t)tr->is_enum);
        if (tr->pass_by != TypePassBy::Value)
            current["pass_by"] = (int8_t)tr->pass_by;
    }
};
}

VisitorInterface *createJsonVisitor()
{
    return new JSonVisitor;
}

QJsonObject takeRootFromJsonVisitor(VisitorInterface *iface)
{
    auto *json_iface=dynamic_cast<JSonVisitor *>(iface);
    assert(json_iface);
    assert(json_iface->result.size() == 1);

    return json_iface->result.takeLast();
}
