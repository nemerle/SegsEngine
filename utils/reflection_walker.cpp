
#include <QCoreApplication>
#include <QDebug>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <functional>

enum class APIType {
    Invalid = -1,
    Common = 0,
    Editor,
    Client,
    Server,
};

enum class TypeRefKind : int8_t {
    Simple, //
    Enum,
    Array,
};
enum class TypePassBy : int8_t {
    Value = 0, // T
    Reference, // T &
    ConstReference, // const T &
    RefValue, // Ref<T>
    ConstRefReference, // const Ref<T> &
    Move, // T &&
    Pointer,
    MAX_PASS_BY
};

struct TS_Base;
struct TS_TypeLike;

struct TS_Enum;
struct TS_Type;
struct TS_Namespace;
struct TS_Property;
struct TS_Signal;
struct TS_Function;
struct TS_Constant;
struct TypeReference;


namespace {
struct VisitorInterface {
    virtual void visit(const TS_Enum *)=0;
    virtual void visit(const TS_Type *)=0;
    virtual void visit(const TS_Namespace *)=0;
    virtual void visit(const TS_Property *)=0;
    virtual void visit(const TS_Signal *)=0;
    virtual void visit(const TS_Function *)=0;
    virtual void visit(const TS_Constant *)=0;
    virtual void visit(const TypeReference *)=0;
};

template <class T> T valFromJson(const QJsonValue &v);

template <> QString valFromJson<QString>(const QJsonValue &v) {
    return v.toString().toUtf8().data();
}
template <> bool valFromJson<bool>(const QJsonValue &v) {
    return v.toBool();
}
template <> TypeRefKind valFromJson<TypeRefKind>(const QJsonValue &v) {
    return TypeRefKind(v.toInt());
}
template <class T> static void setJsonIfNonDefault(QJsonObject &obj, const char *field, const T &v) {
    if (v != T())
        obj[field] = v;
}

template <> void setJsonIfNonDefault<QString>(QJsonObject &obj, const char *field, const QString &v) {
    if (!v.isEmpty())
        obj[field] = v;
}

template <class T> void getJsonOrDefault(const QJsonObject &obj, const char *field, T &v) {
    if (obj.contains(field))
        v = valFromJson<T>(obj[field]);
    else
        v = T();
}

template <> void getJsonOrDefault<QString>(const QJsonObject &obj, const char *field, QString &v) {
    if (obj.contains(field))
        v = valFromJson<QString>(obj[field]);
    else
        v = QString();
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

} // namespace

enum class CSAccessLevel { Public, Internal, Protected, Private };

struct TS_Base {
    enum TypeKind {
        NAMESPACE,
        CLASS,
        ENUM,
        FUNCTION,
        PROPERTY,
        SIGNAL,
        CONSTANT,
        TYPE_REFERENCE, // reference to enum/class/etc.
    };
    const TS_TypeLike *enclosing_type = nullptr;
    QString name;
    TS_Base(QString n) : name(n) {}
    virtual TypeKind kind() const = 0;
    virtual QString c_name() const { return name; }
    virtual void accept(VisitorInterface *) const = 0;
};

struct TypeReference : TS_Base {
    TypeRefKind is_enum = TypeRefKind::Simple;
    TypePassBy pass_by = TypePassBy::Value;
    TS_Base *resolved = nullptr;
    TypeKind kind() const override {return TYPE_REFERENCE;}

    TypeReference(QString n, TypeRefKind en = TypeRefKind::Simple, TS_Base *b = nullptr) : TS_Base(n), is_enum(en) {}
    TypeReference() : TS_Base("") {}
    void accept(VisitorInterface *vi) const override {
        vi->visit(this);
    }
};

struct TS_Constant : public TS_Base {
    TypeReference const_type{ "int32_t", TypeRefKind::Simple };
    QString value;
    CSAccessLevel access_level = CSAccessLevel::Public;
    bool m_imported = false; //!< if set to true, this constant is an imported one and should not be generated

    static TS_Constant *get_instance_for(const TS_TypeLike *tl);
    QString relative_path(const TS_TypeLike *rel_to = nullptr) const;

    TS_Constant(const QString &p_name, int p_value) : TS_Base(p_name), value(QString::number(p_value)) {}
    TS_Constant(const QString &p_name, QString p_value) :
            const_type({ "String", TypeRefKind::Simple }), TS_Base(p_name), value(p_value) {}

    TypeKind kind() const override { return CONSTANT; }
    void accept(VisitorInterface *vi) const override {
        vi->visit(this);
    }
};

struct TS_Function : public TS_Base {
    TypeReference return_type;
    QVector<TypeReference> arg_types;
    QVector<QString> arg_values; // name of variable or a value.
    QVector<bool>
            nullable_ref; // true if given parameter is nullable reference, and we need to always pass a valid pointer.
    QMap<int, QString> arg_defaults;
    bool m_imported = false; // if true, the methods is imported and should not be processed by generators etc.
    TypeKind kind() const override { return FUNCTION; }
    TS_Function(QString n) : TS_Base(n) {}
    void accept(VisitorInterface *vi) const override {
        vi->visit(this);
    }

};

struct TS_Signal : public TS_Function {
    TS_Signal(QString n) : TS_Function(n) { return_type = { "void", TypeRefKind::Simple }; }
    TypeKind kind() const override { return SIGNAL; }
    void accept(VisitorInterface *vi) const override {
        vi->visit(this);
    }

};

struct TS_TypeLike : public TS_Base {
public:
    QString required_header;
    TS_TypeLike(QString n) : TS_Base(n) {}

    // Nested types - (enum,type) in type, (namespace,enum,type) in namespace, () in enum
    QVector<TS_Base *> m_children;
    bool m_imported = false;
    bool m_skip_special_functions = false; // modules extending imported class should not generate special functions.

    // find a common base type for this and with
    virtual const TS_TypeLike *common_base(const TS_TypeLike *with) const;

    virtual bool enum_name_would_clash_with_property(QString cs_enum_name) const { return false; }
    virtual bool needs_instance() const { return false; }

    void visit_kind(TypeKind to_visit, std::function<void(const TS_Base *)> visitor) const {
        for (const TS_Base *tl : m_children) {
            if (tl->kind() == to_visit) {
                visitor(tl);
            }
        }
    }
    virtual void add_child(TS_Namespace *) { assert(!"cannot add Namespace to this type"); }
    virtual void add_child(TS_Type *);
    virtual void add_child(TS_Enum *);
    virtual void add_child(TS_Constant *);

    //    TS_TypeLike* find_typelike_by_cpp_name(QString name) const;
    //    TS_Enum *find_enum_by_cpp_name(QString name) const;
    //    TS_Constant *find_constant_by_cpp_name(QString name) const;

    //    TS_Type *find_by_cs_name(const QString &name) const;
    //    TS_Type *find_type_by_cpp_name(QString name) const;
    QString relative_path(const TS_TypeLike *rel_to = nullptr) const;

    TS_Constant *add_constant(QString name, QString value);
    void add_enum(TS_Enum *enm) {
        // TODO: add sanity checks here
        m_children.push_back((TS_TypeLike *)enm);
    }
};

struct TS_Namespace : public TS_TypeLike {
    friend struct TS_Module;

public:
    // static TS_Namespace *get_instance_for(const String &access_path, const NamespaceInterface &src);

    static TS_Namespace *from_path(QStringView path);
    TS_Namespace(QString n) : TS_TypeLike(n) {}

    TypeKind kind() const override { return NAMESPACE; }
    void fromJson(const QJsonObject &obj);
    void add_child(TS_Namespace *ns) override {
        ns->enclosing_type = this;
        m_children.push_back(ns);
    }
    void accept(VisitorInterface *vi) const override {
        vi->visit(this);
    }

};

struct TS_Enum : public TS_TypeLike {
    QString static_wrapper_class;
    TypeReference underlying_val_type;

    TypeKind kind() const override { return ENUM; }
    QString c_name() const override {
        if (!static_wrapper_class.isEmpty()) { // for synthetic enums - those that don't actually have mapped struct but
                                               // their name refer to it by `StructName::` syntax
            if (name.startsWith(static_wrapper_class))
                return name.mid(static_wrapper_class.size() + 2); // static classname + "::"
        }
        return name;
    }
    TS_Enum(QString n) : TS_TypeLike(n) {}
    void add_child(TS_Constant *t) override;
    void accept(VisitorInterface *vi) const override {
        vi->visit(this);
    }

};

struct TS_Type : public TS_TypeLike {
    TypeReference base_type;

    mutable int pass = 0;
    bool m_value_type = false; // right now used to mark struct types
    bool is_singleton = false;

    static TS_Type *create_type(const TS_TypeLike *owning_type);

    TypeKind kind() const override { return CLASS; }
    // If this object is not a singleton, it needs the instance pointer.
    bool needs_instance() const override { return !is_singleton; }

    QString get_property_path_by_func(const TS_Function *f) const;

    TS_Type(QString n) : TS_TypeLike(n) {}
    void accept(VisitorInterface *vi) const override {
        vi->visit(this);
    }

};



struct ArgumentInterface {
    enum DefaultParamMode { CONSTANT, NULLABLE_VAL, NULLABLE_REF };

    TypeReference type;

    QString name;
    QString default_argument;
    DefaultParamMode def_param_mode = CONSTANT;
};

struct SignalInterface {
    QString name;

    QVector<ArgumentInterface> arguments;

    bool is_deprecated = false;
    QString deprecation_message;

    void add_argument(const ArgumentInterface &argument) { arguments.push_back(argument); }

    void fromJson(const QJsonObject &obj);
};

struct MethodInterface {
    QString name;
    TypeReference return_type;

    /**
     * Determines if the method has a variable number of arguments (VarArg)
     */
    bool is_vararg = false;

    /**
     * Virtual methods ("virtual" as defined by the Godot API) are methods that by default do nothing,
     * but can be overridden by the user to add custom functionality.
     * e.g.: _ready, _process, etc.
     */
    bool is_virtual = false;

    /**
     * Determines if the call should fallback to Godot's object.Call(string, params) in C#.
     */
    bool requires_object_call = false;

    /**
     * Determines if the method visibility is 'internal' (visible only to files in the same assembly).
     * Currently, we only use this for methods that are not meant to be exposed,
     * but are required by properties as getters or setters.
     * Methods that are not meant to be exposed are those that begin with underscore and are not virtual.
     */
    bool is_internal = false;

    QVector<ArgumentInterface> arguments;

    bool is_deprecated = false;
    bool implements_property = false; // Set true on functions implementing a property.
    QString deprecation_message;

    void add_argument(const ArgumentInterface &argument) { arguments.push_back(argument); }

    void fromJson(const QJsonObject &obj);
};

struct PropertyInterface {
    QString cname;
    QString hint_str;
    int max_property_index; // -1 for plain properties, -2 for indexed properties, >0 for arrays of multiple properties
                            // it's the maximum number.
    struct TypedEntry {
        QString subfield_name;
        TypeReference entry_type;
        int index;
        QString setter;
        QString getter;
    };
    QVector<TypedEntry> indexed_entries;

    void fromJson(const QJsonObject &obj);
};

//!
//! \brief Returns the type access path relative to \a rel_to,
//! if rel_to is nullptr this will return full access path
//! \param tgt
//! \param rel_to
//! \return the string representing the type path
//!
QString TS_TypeLike::relative_path(const TS_TypeLike *rel_to) const {
    QStringList parts;
    QSet<const TS_TypeLike *> rel_path;
    const TS_TypeLike *rel_iter = rel_to;
    while (rel_iter) {
        rel_path.insert(rel_iter);
        rel_iter = rel_iter->enclosing_type;
    }

    const TS_TypeLike *ns_iter = this;
    while (ns_iter && !rel_path.contains(ns_iter)) {
        parts.push_front(ns_iter->name);
        // FIXME: this is a hack to handle Variant.Operator correctly.
        if (kind() == ENUM && ns_iter->name == "Variant" && parts[0] != "Variant") {
            parts[0] = "Variant";
        }
        ns_iter = ns_iter->enclosing_type;
    }
    return parts.join("::");
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
        }
        QJsonArray arr;
        tl->visit_kind(kind, [&](const TS_Base *e) {
            result.push_back(QJsonObject());
            e->accept(this);
            arr.push_back(result.takeLast());
        });
        if (!arr.empty())
            tgt[entry_name] = arr;
    }
    void commonVisit(const TS_TypeLike *self) {
        QJsonObject &current(result.back());

        const char *tgt;
        switch (self->kind()) {
            case TS_Base::NAMESPACE:
                tgt = "NAMESPACE";
                break;
            case TS_Base::CLASS:
                tgt = "CLASS";
                break;
            case TS_Base::ENUM:
                tgt = "ENUM";
                break;
            case TS_Base::FUNCTION:
                tgt = "FUNCTION";
                break;
            case TS_Base::CONSTANT:
                tgt = "CONSTANT";
                break;
        }

        current["name"] = self->name;
        current["kind"] = tgt;
        if(!self->required_header.isEmpty())
            current["required_header"] = self->required_header;

    }
public:
    void visit(const TS_Enum *vs) override
    {
        commonVisit(vs);
        QJsonObject &current(result.back());

        entryToJSON(vs, TS_Base::CONSTANT, current);

        if (vs->underlying_val_type.name != "int32_t") {
            current["underlying_type"] = vs->underlying_val_type.name;
        }
    }
    void visit(const TS_Type *vs) override
    {
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
    }
    void visit(const TS_Namespace *vs) override
    {
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
    void visit(const TS_Property *) override
    {
    }
    void visit(const TS_Signal *) override
    {
    }
    void visit(const TS_Function *) override
    {
    }
    void visit(const TS_Constant *cn) override
    {
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
    void visit(const TypeReference *tr) override
    {
        QJsonObject &current(result.back());
        current["name"] = tr->name;
        setJsonIfNonDefault(current, "is_enum", (int8_t)tr->is_enum);
        if (tr->pass_by != TypePassBy::Value)
            current["pass_by"] = (int8_t)tr->pass_by;
    }
};


//void ArgumentInterface::toJson(QJsonObject &obj) const {
//    QJsonObject sertype;
//    type.toJson(sertype);

//    obj["type"] = sertype;
//    obj["name"] = name;
//    if (!default_argument.isEmpty())
//        obj["default_argument"] = default_argument;
//    if (def_param_mode != CONSTANT)
//        obj["def_param_mode"] = def_param_mode;
//}

//void SignalInterface::toJson(QJsonObject &obj) const {
//    obj["name"] = name;
//    ::toJson(obj, "arguments", arguments);
//    setJsonIfNonDefault(obj, "is_deprecated", is_deprecated);
//    setJsonIfNonDefault(obj, "deprecation_message", deprecation_message);
//}

//void MethodInterface::toJson(QJsonObject &obj) const {
//    QJsonObject sertype;
//    return_type.toJson(sertype);

//    obj["name"] = name;
//    obj["return_type"] = sertype;

//    setJsonIfNonDefault(obj, "is_vararg", is_vararg);
//    setJsonIfNonDefault(obj, "is_virtual", is_virtual);
//    setJsonIfNonDefault(obj, "requires_object_call", requires_object_call);
//    setJsonIfNonDefault(obj, "is_internal", is_internal);

//    ::toJson(obj, "arguments", arguments);

//    setJsonIfNonDefault(obj, "is_deprecated", is_deprecated);
//    setJsonIfNonDefault(obj, "implements_property", implements_property);
//    setJsonIfNonDefault(obj, "deprecation_message", deprecation_message);
//}

//void PropertyInterface::toJson(QJsonObject &obj) const {
//    obj["name"] = cname;
//    if (!hint_str.isEmpty())
//        obj["hint_string"] = hint_str;
//    obj["max_property_index"] = max_property_index;
//    QJsonArray prop_infos;
//    if (max_property_index != -1) {
//        for (const auto &entry : indexed_entries) {
//            QJsonObject entry_enc;
//            entry_enc["name"] = entry.subfield_name;
//            if (max_property_index == -2) // enum based properties -> BlendMode(val) ->  set((PropKind)1,val)
//            {
//                if (entry.index != -1)
//                    entry_enc["index"] = entry.index;
//            }
//            QJsonObject enc_type;
//            entry.entry_type.toJson(enc_type);
//            entry_enc["type"] = enc_type;
//            setJsonIfNonDefault(entry_enc, "setter", entry.setter);
//            setJsonIfNonDefault(entry_enc, "getter", entry.getter);

//            prop_infos.push_back(entry_enc);
//        }
//    } else {
//        QJsonObject entry_enc;
//        QJsonObject enc_type;
//        indexed_entries.front().entry_type.toJson(enc_type);
//        entry_enc["type"] = enc_type;
//        setJsonIfNonDefault(entry_enc, "setter", indexed_entries.front().setter);
//        setJsonIfNonDefault(entry_enc, "getter", indexed_entries.front().getter);
//        prop_infos.push_back(entry_enc);
//    }
//    obj["subfields"] = prop_infos;
//}

const TS_TypeLike *TS_TypeLike::common_base(const TS_TypeLike *with) const {
    const TS_TypeLike *lh = this;
    const TS_TypeLike *rh = with;
    if (!lh || !rh)
        return nullptr;

    // NOTE: this assumes that no type path will be longer than 16, should be enough though ?
    QVector<const TS_TypeLike *> lh_path;
    QVector<const TS_TypeLike *> rh_path;

    // collect paths to root for both types

    while (lh->enclosing_type) {
        lh_path.push_back(lh);
        lh = lh->enclosing_type;
    }
    while (rh->enclosing_type) {
        rh_path.push_back(rh);
        rh = rh->enclosing_type;
    }
    if (lh != rh)
        return nullptr; // no common base

    auto rb_lh = lh_path.rbegin();
    auto rb_rh = rh_path.rbegin();

    // walk backwards on both paths
    while (rb_lh != lh_path.rend() && rb_rh != rh_path.rend()) {
        if (*rb_lh != *rb_rh) {
            // encountered non-common type, take a step back and return
            --rb_lh;
            return *rb_lh;
        }
        ++rb_lh;
        ++rb_rh;
    }
    return nullptr;
}

void TS_TypeLike::add_child(TS_Type *t) {
    t->enclosing_type = this;
    m_children.push_back(t);
}

void TS_TypeLike::add_child(TS_Enum *t) {
    t->enclosing_type = this;
    m_children.push_back(t);
}

void TS_TypeLike::add_child(TS_Constant *t) {
    t->enclosing_type = this;
    m_children.push_back(t);
}

void TS_Enum::add_child(TS_Constant *t) {
    t->enclosing_type = this;
    t->const_type = underlying_val_type;
    m_children.push_back(t);
}

struct ReflectionData {
    QString module_name;
    //! default namespace used when one is needed and was not available - a crutch to reduce amount of SE_NAMESPACE
    //! usages
    QString default_ns;
    //! full reflection data version, should be >= api_version
    QString version;
    //! supported api version.
    QString api_version;
    //! Hash of the sourced reflection data.
    QString api_hash;
    struct ImportedData {
        QString module_name;
        QString api_version;
    };
    // Contains imports required to process this ReflectionData.
    QVector<ImportedData> imports;
    QVector<TS_Namespace *> namespaces;
    QHash<QString, TS_Base *> created_types;
};

bool save_to_file(ReflectionData &data, QString os_path) {
    QFile inFile(os_path);
    if (!inFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QJsonObject root;
    root["module_name"] = data.module_name;
    root["api_version"] = data.api_version;
    root["api_hash"] = data.api_hash;
    root["version"] = data.version;

    QJsonArray dependencies;
    for (const auto &v : data.imports) {
        QJsonObject dependency;
        dependency["name"] = v.module_name;
        dependency["api_version"] = v.api_version;
        dependencies.push_back(dependency);
    }
    root["dependencies"] = dependencies;

    QJsonArray j_namespaces;
    for (const auto *v : data.namespaces) {
        JSonVisitor visitor;
        visitor.result.push_back(QJsonObject());
        v->accept(&visitor);
        assert(visitor.result.size()==1);
        j_namespaces.push_back(visitor.result.takeLast());
    }
    root["namespaces"] = j_namespaces;

    QString content = QJsonDocument(root).toJson();
    inFile.write(content.toUtf8());
    return true;
}



struct ModuleDefinition {
    QString name;
    QString version;
    QString api_version;
    QStringList top_directories;
};

bool loadModuleDefinition(ModuleDefinition &tgt, QString srcfile) {
    QJsonDocument doc;
    QFile src(srcfile);
    if (!src.open(QFile::ReadOnly))
        return false;
    QByteArray data(src.readAll());
    doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;
    QJsonObject root(doc.object());
    tgt.name = root["name"].toString().toUtf8();
    tgt.version = root["version"].toString().toUtf8();
    tgt.api_version = root["api_version"].toString().toUtf8();
    QJsonArray dirs = root["directories"].toArray();
    for (int i = 0, fin = dirs.size(); i < fin; ++i) {
        tgt.top_directories.append(dirs.at(i).toString().toUtf8());
    }
    return true;
}
struct ProcessingBlock {};

ReflectionData g_rd;

struct ProcessingUnit {
    QString filename;
    QVector<TS_TypeLike *> nesting_stack;
};
QString currentTypePath(ProcessingUnit &pu, QStringView name) {
    QString type_path;
    if (!pu.nesting_stack.empty()) {
        type_path = pu.nesting_stack.back()->relative_path();
        type_path += "::";
    }
    if (!name.isEmpty())
        type_path += name;
    return type_path;
}
void endBlock(ProcessingUnit &pu) {
    assert(!pu.nesting_stack.empty());
    pu.nesting_stack.pop_back();
}
void startNamespace(ProcessingUnit &pu, QStringView name) {
    QString type_path = currentTypePath(pu, name);
    TS_Namespace *ns;
    if (!g_rd.created_types.contains(type_path)) {
        ns = new TS_Namespace(name.toString());
        g_rd.created_types[type_path] = ns;
        if (pu.nesting_stack.empty()) {
            g_rd.namespaces.push_back(ns);
        }
    } else {
        TS_Base *entry = g_rd.created_types[type_path];
        assert(entry->kind() == TS_Base::NAMESPACE);
        ns = (TS_Namespace *)entry;
    }

    if (!pu.nesting_stack.empty()) {
        pu.nesting_stack.back()->add_child(ns);
    }
    pu.nesting_stack.push_back(ns);
}

/*
    Const processing:

    constexpr (type) NAME [(= value)| {}]
    NAME (= value)?,
*/

void addConstant(ProcessingUnit &pu, QStringView name, const QString &src) {
    assert(!pu.nesting_stack.empty());
    QString type_path = currentTypePath(pu, name);
    assert(!g_rd.created_types.contains(type_path));
    TS_TypeLike *tl = pu.nesting_stack.back();

    bool ok = false;
    QString re_text(QString("^\\s*%1\\s*=\\s*([^,\\n]+),?").arg(name));
    QString regexp(re_text);
    QRegularExpression constexpr_re(regexp, QRegularExpression::MultilineOption);
    auto res = constexpr_re.match(src);
    QString value = res.captured(1);
    int cnt = res.lastCapturedIndex();

    TS_Constant *cn = new TS_Constant(name.toString(), value);
    if (tl->kind() == TS_Base::ENUM) {
        // TODO: Verify constant type ( simple int expression )
    } else if (!value.startsWith('"')) {
        cn->const_type.name = "int32_t";
    }
    tl->add_child(cn);
    g_rd.created_types[type_path] = cn;
}

QStringList extractDelimitedBlock(QStringRef dat, QChar lbrack, QChar rbrack, bool remove_comments = true) {
    bool in_block_comment = false;
    int nest_level = 0;
    int idx = 0;
    QStringList res;
    QString line;
    res.reserve(dat.size());
    while (idx < dat.size()) {
        if (dat.mid(idx, 2) == "/*") {
            int next_eol = dat.indexOf("*/", idx + 2);
            if (next_eol == -1)
                break;
            idx = next_eol + 2;
            continue;
        }
        if (dat.mid(idx, 2) == "//") {
            int next_eol = dat.indexOf('\n', idx + 2);
            if (next_eol != -1)
                idx = next_eol;
            idx++;
            continue;
        }
        if (!in_block_comment) {
            QChar c = dat[idx];
            if (c == lbrack) {
                nest_level++;
                if (nest_level > 1)
                    line.push_back(c);
            } else if (c == rbrack) {
                nest_level--;
                if (nest_level >= 1)
                    line.push_back(c);
                else
                    break;
            } else {
                line.push_back(c);
            }
            if (c == '\n') {
                line = line.trimmed();
                if (!line.isEmpty()) {
                    res.push_back(line);
                }
                line.clear();
            }
        }
        idx++;
    }
    return res;
}
void addEnum(ProcessingUnit &pu, QStringView name, QString &src) {
    assert(!pu.nesting_stack.empty());
    QString type_path = currentTypePath(pu, name);
    assert(!g_rd.created_types.contains(type_path));
    TS_TypeLike *tl = pu.nesting_stack.back();

    QString regexp(QString("enum\\s+(class)?\\s*%1\\s*:?\\s*([\\w_]+)?").arg(name));
    QRegularExpression constexpr_re(regexp, QRegularExpression::MultilineOption);
    auto res = constexpr_re.match(src);
    if (!res.hasMatch()) {
        assert(!"Cannot find start of enum definition in this file");
    }
    int num_captchures = res.lastCapturedIndex();

    int offset = res.capturedStart();
    int next_offset = res.capturedLength();
    QStringRef nextfew = src.midRef(res.capturedEnd());
    QStringList enum_def = extractDelimitedBlock(nextfew, '{', '}');
    if (enum_def.isEmpty()) {
        qCritical() << "Enum definition is empty!";
    }
    QString type = "int32_t";
    if (num_captchures == 1) {
        type = res.captured(1).trimmed();
    } else if (num_captchures == 2) {
        type = res.captured(2).trimmed();
    }
    TS_Enum *en = new TS_Enum(name.toString());
    en->underlying_val_type = { type };
    g_rd.created_types[type_path] = en;

    pu.nesting_stack.push_back(en);
    int idx = 0;
    for (QString v : enum_def) {
        if (v.endsWith(',')) {
            v = v.mid(0, v.size() - 1);
        }
        auto parts = v.splitRef('=');
        if (parts.size() == 1) {
            assert(idx >= 0);
            v += "=" + QString::number(idx++);
            parts = v.splitRef('=');
        } else {
            bool isok = false;
            int new_idx = parts[1].toInt(&isok);
            if (!isok)
                idx = -1; // can't calculate non-'=' entries after that
            else
                idx = new_idx + 1; // next entry will have this
        }

        addConstant(pu, parts[0].trimmed(), v);
    }
    pu.nesting_stack.pop_back();

    tl->add_child(en);
}
struct ClassDecl {
    QString name;
    QString base;
    bool is_singleton;
};

ClassDecl extractClassName(QStringRef decl,QStringView options) {
    ClassDecl res;
    decl = decl.trimmed();
    QRegularExpression splitter("class\\s+(GODOT_EXPORT\\s+)?(\\w+)\\s*(:\\s*.*)?");
    auto result = splitter.match(decl);
    assert(result.hasMatch());
    QStringList pp=result.capturedTexts();
    res.name = result.captured(2);
    if(result.lastCapturedIndex()==3)
        res.base = result.captured(3).mid(1).trimmed(); // skip :
    return res;
}
void processSEClass(ProcessingUnit &pu,QStringView params,int64_t offset,QString &data) {
    // Step 1. find enclosing class
    int bracket_nesting=1;
    while(offset>=0 && bracket_nesting!=0) {
        if(data[(int)offset]=='}') {
            bracket_nesting++;
        }
        else if(data[(int)offset]=='{') {
            bracket_nesting--;
        }
        offset--;
    }
    int class_kw_idx = data.lastIndexOf("class",offset);
    assert(class_kw_idx!=-1);

    QStringRef class_decl=data.midRef(class_kw_idx,offset-class_kw_idx);
    qDebug() << "Class decl:"<<class_decl;
    // Step 2. parse the decl.
    ClassDecl parsed_decl = extractClassName(class_decl,params);
    // TODO: find actual base-class based on parsed bases
    if(!parsed_decl.base.isEmpty()) {
        parsed_decl.base = parsed_decl.base.split(',').first().split(' ')[1];
    }

    // TODO: this needs a global setting on the commandline ??
    bool auto_wrapped_ns = false;
    if (pu.nesting_stack.empty()) {
        auto_wrapped_ns = true;
        startNamespace(pu, g_rd.default_ns);
    }

    assert(!pu.nesting_stack.empty());
    QString type_path = currentTypePath(pu, parsed_decl.name);
    assert(!g_rd.created_types.contains(type_path));
    TS_TypeLike *tl = pu.nesting_stack.back();

    TS_Type *tp = new TS_Type(parsed_decl.name);
    tp->required_header = QString(pu.filename).replace(".cpp",".h");
    if(!parsed_decl.base.isEmpty()) {
        tp->base_type = {parsed_decl.base};
    }

    tl->add_child(tp);
    if (auto_wrapped_ns) {
        endBlock(pu);
    }

}
void addClass(ProcessingUnit &pu, QStringView name, QStringView parent, QString &src) {
    // TODO: this needs a global setting on the commandline ??
    bool auto_wrapped_ns = false;
    if (pu.nesting_stack.empty()) {
        auto_wrapped_ns = true;
        startNamespace(pu, g_rd.default_ns);
    }
    QString type_path = currentTypePath(pu, name);
    assert(!g_rd.created_types.contains(type_path));
    TS_TypeLike *tl = pu.nesting_stack.back();

    TS_Type *tp = new TS_Type(name.toString());
    tp->required_header = QString(pu.filename).replace(".cpp",".h");
    pu.nesting_stack.push_back(tp);

    pu.nesting_stack.pop_back();

    tl->add_child(tp);
    if (auto_wrapped_ns) {
        endBlock(pu);
    }
}

void processFile(QString file) {
    ProcessingUnit pu;
    pu.filename = file;
    QFile src_file(file);
    if (!src_file.open(QFile::ReadOnly)) {
        qCritical() << "Failed to open source file:" << file;
        return;
    }
    QString contents(QString::fromUtf8(src_file.readAll()));
    QTextStream ts(&contents);
    QString line;

    while (!ts.atEnd()) {
        line.clear();
        ts.readLineInto(&line);
        QStringView line_view(line);
        line_view = line_view.trimmed();
        if (line_view.startsWith(QLatin1String("SE_NAMESPACE("))) {
            QStringView nsname = line_view.mid(13);
            nsname = nsname.mid(0, nsname.lastIndexOf(')'));
            qDebug() << "NS:" << nsname << " in" << file;
            startNamespace(pu, nsname);
            continue;
        }
        // BIND_GLOBAL_CONSTANT(SPKEY)
        if (line_view.startsWith(QLatin1String("SE_CONSTANT("))) {
            QStringView nsname = line_view.mid(12);
            nsname = nsname.mid(0, nsname.lastIndexOf(')'));
            qDebug() << "CONSTANT:" << nsname << " in" << file;
            addConstant(pu, nsname, contents);
            continue;
        }
        if (line_view.startsWith(QLatin1String("SE_ENUM("))) {
            QStringView nsname = line_view.mid(8);
            nsname = nsname.mid(0, nsname.lastIndexOf(')'));
            qDebug() << "ENUM:" << nsname << " in" << file;
            addEnum(pu, nsname, contents);
            continue;
        }
//        if (line_view.startsWith(QLatin1String("GDCLASS("))) {
//            QStringView nsname = line_view.mid(8);
//            nsname = nsname.mid(0, nsname.lastIndexOf(')'));
//            auto parts = nsname.split(',');
//            qDebug() << "CLASS:" << nsname << " in" << file;
//            assert(parts.size() == 2);
//            addClass(pu, parts[0], parts[1], contents);
//            continue;
//        }
        if (line_view.startsWith(QLatin1String("SE_CLASS("))) {
            QStringView class_settings = line_view.mid(8);
            class_settings = class_settings.mid(0, class_settings.lastIndexOf(')'));
            processSEClass(pu,class_settings,ts.pos(),contents);
            continue;
        }
        if (line_view == QLatin1String("SE_END();")) {
            endBlock(pu);
        }
    }
}
bool processModuleDef(QString path) {
    ModuleDefinition mod;
    if (!loadModuleDefinition(mod, path)) {
        return false;
    }
    for (QString root : mod.top_directories) {
        QDirIterator iter(root, QStringList({ "*.cpp", "*.h" }), QDir::NoFilter, QDirIterator::Subdirectories);
        while (iter.hasNext()) {
            processFile(iter.next());
        }
    }

    g_rd.module_name = mod.name;
    g_rd.version = mod.version;
    g_rd.api_version = mod.api_version;

    QJsonObject obj;
    save_to_file(g_rd, "output.json");
    return true;
}
struct CppVisitor : public VisitorInterface {
    QSet<QString> headers;
    QMap<QString,QString> class_binders;
    QVector<QString> class_stack; // for nested classes.
    // VisitorInterface interface
public:
    void visit(const TS_Enum *entry) override
    {
    }
    void visit(const TS_Type *entry) override
    {
        QString name= entry->c_name();
        class_stack.push_back(name);
        assert(class_binders.contains(name)==false);
        QString &tgt(class_binders[name]);
        for(const auto *child : entry->m_children) {
            child->accept(this);
        }
        tgt  = "void "+name+"::_bind_method() {\n";
        tgt += "}\n";
        class_stack.pop_back();
    }
    void visit(const TS_Namespace *entry) override
    {
        for(const auto *child : entry->m_children) {
            child->accept(this);
        }
    }
    void visit(const TS_Property *entry) override
    {
    }
    void visit(const TS_Signal *entry) override
    {
    }
    void visit(const TS_Function *entry) override
    {
    }
    void visit(const TS_Constant *entry) override
    {
    }
    void visit(const TypeReference *entry) override
    {
    }
};

bool save_cpp(ReflectionData &data, QString os_path) {
    QFile inFile(os_path);
    if (!inFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    CppVisitor visitor;
    for (const auto *v : data.namespaces) {
        v->accept(&visitor);
    }
    QString content;
    //TODO: extract all headers from associated cpp file
    QStringList parts = visitor.headers.toList();
    parts.sort();
    content+=parts.join("\n");
    inFile.write(content.toUtf8());
    return true;
}
bool processHeader(QString path) {
    if(!QFile::exists(path))
        return false;
    processFile(path);
    save_cpp(g_rd, "output.json");
    return true;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("soc");
    QCoreApplication::setApplicationVersion("0.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("Segs Object Compiler");
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument("source", "Module definition file or a single header.");


    parser.addOptions({
        {{"n", "namespace"},
            "Use the provided namespace as default when no other is provided/defined.",
         "namespace"},
    });

    // Process the actual command line arguments given by the user
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if(args.empty()) {
        return 0;
    }
    QString default_ns = parser.value("namespace");
    if(default_ns.isEmpty()) {
        default_ns = "Godot";
    }
    g_rd.default_ns = default_ns;

    for(QString arg : args ) {
        if(arg.endsWith("json")) {
            if(!processModuleDef(arg))
                return -1;
        }
        if(arg.endsWith(".h")) {
            if(!processHeader(arg))
                return -1;
        }
    }
    return 0;
}
