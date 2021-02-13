#include "reflection_walker.h"

#include "json_visitor.h"
#include "cpp_visitor.h"
#include "type_system.h"

#include <QBuffer>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <functional>

namespace {

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
template <class T> void setJsonIfNonDefault(QJsonObject &obj, const char *field, const T &v) {
    if (v != T()) {
        obj[field] = v;
    }
}

template <> void setJsonIfNonDefault<QString>(QJsonObject &obj, const char *field, const QString &v) {
    if (!v.isEmpty()) {
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

enum class BlockType {
    Class=0,
    Struct=1,
    Namespace=2
};

struct ProcessingUnit {
    QString filename;
    QString contents;
    QVector<TS_TypeLike *> nesting_stack;
    QVector<int> brace_nesting_stack;
    QVector<int> open_brace_indices;
    // namespace/class/struct nesting, used to verify proper nesting of registered types
    struct BlockName {
        QStringView name;
        QStringView full_def;
        int level;
        BlockType type;
    };

    QVector<BlockName> name_stack;
};

struct ParseHead {
    ProcessingUnit &tu;
    int start_offset;
    int end_offset;
    int offset;
    int bracket_nesting_level = -1;
    bool collecting_signals=false;
    QString error;
    explicit ParseHead(ProcessingUnit &p, int start = -1, int end = -1) : tu(p) {
        start = std::max(0, start);
        start = std::min(tu.contents.size(), start);
        if(end==-1)
            end = tu.contents.size();
        end = std::max(start, end);
        end = std::min(end, tu.contents.size());
        start_offset = start;
        end_offset = end;
        offset = 0;
    }
    ParseHead(const ParseHead &) = delete;
    explicit ParseHead(ParseHead &from, int start, int len = -1) : tu(from.tu) {
        start_offset = start;
        offset = 0;
        end_offset = start + len;
    }
    [[nodiscard]] const QChar *begin() const { return tu.contents.constData() + start_offset; }
    [[nodiscard]] const QChar *end() const { return tu.contents.constData() + end_offset; }
    [[nodiscard]] QStringView slice() const { return tu.contents.midRef(start_offset, end_offset - start_offset); }
    [[nodiscard]] QChar at(int idx) const { return tu.contents.at(start_offset + idx); }
    [[nodiscard]] QChar peek() const { return slice()[offset]; }
    [[nodiscard]] QChar take() { return slice()[offset++]; }
    [[nodiscard]] QStringView peek(int cnt) const { return slice().mid(offset, cnt); }
    void consume(int cnt = 1) {
        offset += cnt;
        offset = std::min(offset, end_offset - start_offset);
    }
    bool atEnd() const { return end_offset == start_offset || offset == (end_offset - start_offset); }

    [[nodiscard]] int searchForward(QChar c) const { return slice().indexOf(c, offset); }
    [[nodiscard]] int searchForward(const QVector<QChar> &chars) const {
        int min_pos = slice().size()+1;
        for(QChar c : chars) {
            int pos=searchForward(c);
            if(pos==-1)
                continue;
            min_pos = std::min(pos,min_pos);
        }
        return min_pos==(slice().size()+1) ? -1 : min_pos;
    }
    int searchBackward(QChar c) const { return slice().lastIndexOf(c, offset); }
    void skipWS() {
        while(!atEnd() && peek().isSpace())
            consume();
    }
    QStringView getIdent() {
        int start_idx = offset;
        skipWS();
        while(!atEnd()) {
            QChar c = take();
            if(!c.isLetterOrNumber() && c!='_') {
                offset--;
                break;
            }
        }
        return slice().mid(start_idx,offset-start_idx);
    }
};

static bool verifyNesting(ParseHead &pu, const char *name,QStringView var_name) {
    if(pu.tu.nesting_stack.empty()) {
        pu.error = QString("Incorrect block nesting detected when adding %1 named: %2").arg(name).arg(var_name);
        qCritical() << pu.error;
        return false; // empty
    }

    return true;
}

} // end of anonymous namespace




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



// void ArgumentInterface::toJson(QJsonObject &obj) const {
//    QJsonObject sertype;
//    type.toJson(sertype);

//    obj["type"] = sertype;
//    obj["name"] = name;
//    if (!default_argument.isEmpty())
//        obj["default_argument"] = default_argument;
//    if (def_param_mode != CONSTANT)
//        obj["def_param_mode"] = def_param_mode;
//}

// void SignalInterface::toJson(QJsonObject &obj) const {
//    obj["name"] = name;
//    ::toJson(obj, "arguments", arguments);
//    setJsonIfNonDefault(obj, "is_deprecated", is_deprecated);
//    setJsonIfNonDefault(obj, "deprecation_message", deprecation_message);
//}

// void MethodInterface::toJson(QJsonObject &obj) const {
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

// void PropertyInterface::toJson(QJsonObject &obj) const {
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


struct ReflectionData {
    ModuleConfig config;
    QVector<TS_Namespace *> namespaces;
    QHash<QString, TS_Base *> created_types;
};

bool save_to_file(ReflectionData &data, QIODevice *io_device) {
    QJsonObject root;
    root["module_name"] = data.config.module_name;
    root["api_version"] = data.config.api_version;
    root["api_hash"] = data.config.api_hash;
    root["version"] = data.config.version;

    QJsonArray dependencies;
    for (const auto &v : data.config.imports) {
        QJsonObject dependency;
        dependency["name"] = v.module_name;
        dependency["api_version"] = v.api_version;
        dependencies.push_back(dependency);
    }
    root["dependencies"] = dependencies;

    QJsonArray j_namespaces;
    for (const auto *v : data.namespaces) {
        VisitorInterface *visitor = createJsonVisitor();
        v->accept(visitor);
        j_namespaces.push_back(takeRootFromJsonVisitor(visitor));
        delete visitor;
    }
    root["namespaces"] = j_namespaces;

    QString content = QJsonDocument(root).toJson();
    io_device->write(content.toUtf8());
    return true;
}

struct ModuleDefinition {
    QString name;
    QString version;
    QString api_version;
    QStringList top_directories;
};

bool loadModuleDefinition(ModuleDefinition &tgt, QString srcfile) {
    QFile src(srcfile);
    if (!src.open(QFile::ReadOnly))
        return false;
    QByteArray data(src.readAll());
    QJsonDocument doc = QJsonDocument::fromJson(data);
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

ReflectionData g_rd;

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
void endBlock(ParseHead &pu) {
    assert(!pu.tu.nesting_stack.empty());
    int match_bracing=pu.tu.brace_nesting_stack.back();
    assert(match_bracing==-1 || match_bracing == pu.bracket_nesting_level);
    pu.tu.nesting_stack.pop_back();
    pu.tu.brace_nesting_stack.pop_back();
}
static QString getNestedBlockPath(ParseHead &pu) {
    QStringList parts;
    for(auto v : pu.tu.name_stack) {
        parts.push_back(v.name.toString());
    }
    return parts.join("::");
}

void startNamespace(ParseHead &pu, QStringView name) {
    bool skip_verify=false;
    bool in_ns_block = !pu.tu.name_stack.empty() && pu.tu.name_stack.back().type==BlockType::Namespace;
    if(pu.tu.nesting_stack.empty() && !in_ns_block) {
        skip_verify = true;
        pu.tu.name_stack.push_front({name,name,-1,BlockType::Namespace});
    }
    const auto &entry(pu.tu.name_stack.back());

    QString type_path = currentTypePath(pu.tu, name);
    QString nested_path = getNestedBlockPath(pu);
    if(!skip_verify) {
        if(entry.type!=BlockType::Namespace) {
            pu.error = QString("Macro SE_NAMESPACE was placed in non-namespace block (%1)").arg(nested_path);
            qCritical()<<pu.error;
            return;
        }
        if(entry.name!=name) {
            pu.error = QString("Macro SE_NAMESPACE name does not match enclosing namespace block '%1'!='%2'")
                               .arg(entry.name)
                               .arg(name);
            qCritical()<<pu.error;
            return;
        }
        if(type_path!=nested_path) {
            pu.error = QString("Macro SE_NAMESPACE nested in unregistered namespace '%1'!='%2'")
                               .arg(nested_path)
                               .arg(type_path);
            qCritical()<<pu.error;
            return;
        }
    }
    TS_Namespace *ns;
    if (!g_rd.created_types.contains(type_path)) {
        ns = new TS_Namespace(name.toString());
        g_rd.created_types[type_path] = ns;
        if (pu.tu.nesting_stack.empty()) {
            g_rd.namespaces.push_back(ns);
        }
    } else {
        TS_Base *entry = g_rd.created_types[type_path];
        assert(entry->kind() == TS_Base::NAMESPACE);
        ns = (TS_Namespace *)entry;
    }

    if (!pu.tu.nesting_stack.empty()) {
        pu.tu.nesting_stack.back()->add_child(ns);
    }
    pu.tu.nesting_stack.push_back(ns);
    pu.tu.brace_nesting_stack.push_back(pu.bracket_nesting_level);
}

/*
    Const processing:

    constexpr (type) NAME [(= value)| {}]
    NAME (= value)?,
*/
int processBlock(ParseHead &pu);

void addConstant(ParseHead &pu, QStringView name) {

    if(!verifyNesting(pu,"constant",name)) {
        return;
    }

    QString type_path = currentTypePath(pu.tu, name);
    assert(!g_rd.created_types.contains(type_path));
    TS_TypeLike *tl = pu.tu.nesting_stack.back();

    QString re_text(QString(R"(^\s*%1\s*=\s*([^,\r\n]+),?)").arg(name));
    QString regexp(re_text);
    QRegularExpression constexpr_re(regexp, QRegularExpression::MultilineOption);
    auto res = constexpr_re.match(pu.slice());
    QString value = res.captured(1);

    TS_Constant *cn = new TS_Constant(name.toString(), value);
    if (tl->kind() == TS_Base::ENUM) {
        // TODO: Verify constant type ( simple int expression )
    } else if (!value.startsWith('"')) {
        cn->const_type.name = "int32_t";
    }
    tl->add_child(cn);
    g_rd.created_types[type_path] = cn;
}

QByteArray removeComments(QByteArray dat) {
    bool in_block_comment = false;
    int idx = 0;
    QByteArray res;
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
                idx = next_eol - 1;
            idx++;
            continue;
        }
        if (!in_block_comment) {
            res.push_back(dat[idx]);
        }
        idx++;
    }
    return res.trimmed();
}

QPair<int, int> extractDelimitedBlock(QStringView dat, QChar lbrack, QChar rbrack) {
    // PRE_CONDITION, dat does not contain comments. ( block and line comments removed )
    int nest_level = 0;
    int idx = 0;
    QPair<int, int> res{ -1, -1 };

    while (idx < dat.size()) {
        QChar c = dat[idx];
        if (c == lbrack) {
            nest_level++;
        } else if (c == rbrack) {
            nest_level--;
            if (nest_level < 1)
                break;
        } else {
            if (nest_level > 0 && res.first == -1 && !c.isSpace())
                res.first = idx;
        }
        idx++;
    }
    res.second = idx;
    return res;
}
//TODO: enum scans backward, but does not take block nesting into account
void addEnum(ParseHead &pu, QStringView name) {
    if(!verifyNesting(pu,"enum",name)) {
        return;
    }

    assert(!pu.tu.nesting_stack.empty());
    QString type_path = currentTypePath(pu.tu, name);
    assert(!g_rd.created_types.contains(type_path));
    TS_TypeLike *tl = pu.tu.nesting_stack.back();

    QString regexp(QString(R"(enum\s+(class)?\s*%1\s*:?\s*([\w_]+)?)").arg(name));
    QRegularExpression constexpr_re(regexp, QRegularExpression::MultilineOption);
    auto res = constexpr_re.match(pu.slice());
    if (!res.hasMatch()) {
        assert(!"Cannot find start of enum definition in this file");
    }
    int num_captchures = res.lastCapturedIndex();

    int start_idx = res.capturedEnd();
    QStringView nextfew = pu.slice().mid(start_idx);
    QPair<int, int> enum_def = extractDelimitedBlock(nextfew, '{', '}');
    if (enum_def.first == enum_def.second || enum_def.second == -1) {
        qCritical() << "Enum definition is empty!";
    }
    QString type = "int32_t";
    QStringList allcaps = res.capturedTexts();
    bool is_strict=false;
    if (num_captchures == 1) {
        // only class marker
        is_strict = res.captured(1).trimmed()=="class";
    } else if (num_captchures == 2) {
        is_strict = res.captured(1).trimmed()=="class";
        type = res.captured(2).trimmed();
    }
    TS_Enum *en = new TS_Enum(name.toString());
    en->underlying_val_type = { type };
    en->is_strict = is_strict;
    g_rd.created_types[type_path] = en;

    pu.tu.nesting_stack.push_back(en);

    int idx = 0;
    QStringView enum_def_block(pu.slice().mid(enum_def.first + start_idx, enum_def.second - enum_def.first));
    for (QStringView v : enum_def_block.split('\n')) {
        v = v.trimmed();
        if (v.isEmpty())
            continue;
        if (v.endsWith(',')) {
            v = v.mid(0, v.size() - 1);
        }
        QString number;
        auto parts = v.split('=');
        if (parts.size() == 1) {
            assert(idx >= 0);
            number = QString::number(idx++);
            parts.push_back(number);
        } else {
            bool isok = false;
            number=parts[1].toString();
            int new_idx = parts[1].toInt(&isok);
            if (!isok)
                idx = -1; // can't calculate non-'=' entries after that
            else
                idx = new_idx + 1; // next entry will have this
        }
        QString const_name=parts[0].trimmed().toString();
        TS_Constant *constant = new TS_Constant(const_name,parts[1].toString());
        en->add_child(constant);
        g_rd.created_types[type_path+"::"+const_name] = constant;
    }

    tl->add_child(en);

    pu.tu.nesting_stack.pop_back();
}
struct ClassDecl {
    QString name;
    QString base;
    bool is_singleton;
};

static ClassDecl extractClassName(QStringView decl, QStringView options) {
    ClassDecl res;
    decl = decl.trimmed();

    QRegularExpression splitter(R"((\w+)\s*(:\s*.*)?)");
    auto result = splitter.match(decl);
    assert(result.hasMatch());

    res.name = result.captured(1);
    if (result.lastCapturedIndex() == 2)
        res.base = result.captured(2).mid(1).trimmed(); // skip :
    return res;
}

static QStringView getEnclosingClassDecl(ParseHead &pu)
{
    QStringView data(pu.slice());
    int offset = pu.tu.open_brace_indices.back(); // previous opening bracket.
    int class_kw_idx = data.lastIndexOf(QLatin1String("class"), offset);
    int advance_chars = 5;
    if (class_kw_idx == -1) {
        class_kw_idx = data.lastIndexOf(QLatin1String("struct"), offset);
        advance_chars = 6;
    }
    assert(class_kw_idx != -1);
    class_kw_idx += advance_chars; // skip keyword

    return data.mid(class_kw_idx, offset - class_kw_idx);
}

void processSEClass(ParseHead &pu, QStringView params) {
    assert(!pu.tu.nesting_stack.empty());
    if(pu.tu.name_stack.empty()) {
        pu.error = "SE_CLASS macro placed outside of a block";
        return;
    }
    const auto &namestack_entry(pu.tu.name_stack.back());
    auto blocktype =namestack_entry.type;
    if(blocktype!=BlockType::Class && blocktype!=BlockType::Struct) {
        pu.error = "SE_CLASS macro must be placed inside class or struct block";
        return;
    }


    QStringView class_decl = namestack_entry.full_def.trimmed();
    QString decl1 = class_decl.toString();

    // Step 2. parse the decl.
    ClassDecl parsed_decl = extractClassName(class_decl, params);
    // TODO: find actual base-class based on parsed bases
    if (!parsed_decl.base.isEmpty()) {
        QStringList bases=parsed_decl.base.split(',');
        QString first_base=bases.first();
        auto cc = first_base.split(' ');
        if(cc.size()==1) // no access specifier
            parsed_decl.base = first_base;
        else {
            parsed_decl.base = cc.mid(1).join(' '); // skip the access specifier and join the rest;
        }
    }

    QString type_path = currentTypePath(pu.tu, parsed_decl.name);
    QString nested_path = getNestedBlockPath(pu);
    if(type_path.size()!=nested_path.size()) {
        pu.error = "SE_CLASS macro placed in nested class that has no SE_CLASS macro, this is unsupported";
        return;
    }

    assert(!g_rd.created_types.contains(type_path));
    TS_TypeLike *tl = pu.tu.nesting_stack.back();

    TS_Type *tp = new TS_Type(parsed_decl.name);
    tp->required_header = QString(pu.tu.filename).replace(".cpp", ".h");
    if (!parsed_decl.base.isEmpty()) {
        tp->base_type = { parsed_decl.base };
    }

    pu.tu.nesting_stack.push_back(tp);
    pu.tu.brace_nesting_stack.push_back(pu.bracket_nesting_level);

    tl->add_child(tp);
}

int isEOL(QStringView content) {
    if (content.mid(0, 2) == QLatin1String("\r\n"))
        return 2;
    if (content.front() == '\n')
        return 1;
    return 0;
}
static bool ensureNS(ParseHead &pu) {
    // TODO: this needs a global setting on the commandline ??
    if (pu.tu.nesting_stack.empty()) {
        // Global namespace is always put at brace nesting level == -1;
        int current_level = pu.bracket_nesting_level;
        pu.bracket_nesting_level = -1;
        // this might be done after a block was already visited ->
        // class Foo {
        //   SE_CLASS()
        // will visit the block-open and record Foo, and after seeing SE_CLASS it will ensureNS
        startNamespace(pu, g_rd.config.default_ns);
        pu.bracket_nesting_level = current_level;
        return true;
    }

    return false;
}
struct ArgTypeMod {
    bool is_const=false;
    bool is_volatile=false;
    bool is_restrict=false;
    bool is_signed=false;
    bool is_unsigned=false;
};
struct ArgTypeDecl {
    QStringView arg_name;
    QStringView type_name;
    QStringView template_params; // if template

    ArgTypeMod mod;
    bool is_pointer = false;
    bool is_reference = false;
    bool is_move = false;
    QStringView default_value;
    TypePassBy pass_by;
    void calc_pass_by() {
        /*
            Value = 0, // T
            Reference, // T &
            ConstReference, // const T &
            RefValue, // Ref<T>
            ConstRefReference, // const Ref<T> &
            Move, // T &&
            Pointer,
        */
        if(is_pointer) {
            pass_by=TypePassBy::Pointer;
            if(mod.is_const) {
                pass_by = TypePassBy::ConstPointer;
            }
            if(mod.is_volatile||mod.is_restrict) {
                qDebug() << "Values passed by pointers do not carry their modifiers.";
            }
            return;
        }
        if(is_reference) {
            if(mod.is_const) {
                pass_by = TypePassBy::ConstReference;
            }
            else {
                pass_by = TypePassBy::Reference;
            }
            return;
        }
        if(is_move) {
            pass_by = TypePassBy::Move;
            return;
        }
        pass_by = TypePassBy::Value;
    }
};
struct MethodDecl {
    QVector<QStringView> storage_specifiers;
    QStringView name;
    bool is_virtual=false;
    bool is_static=false;
    bool is_constexpr=false;
    ArgTypeDecl return_type;
    QVector<ArgTypeDecl> args;
};

static void parseArgTypeMod(ParseHead &pu,ArgTypeMod &tgt) {
    // const,volatile,restrict,signed,unsigned  [ long double unhandled ]

    // mods* type_spec mods*
    bool mod_found=true;
    while(mod_found) {
        pu.skipWS();
        int offset=pu.offset;
        QStringView ident=pu.getIdent().trimmed();
        if(ident==QLatin1String("const")) {
            tgt.is_const=true;
        }
        else if(ident==QLatin1String("volatile")) {
            tgt.is_volatile=true;
        }
        else if(ident==QLatin1String("restrict")) {
            tgt.is_restrict=true;
        }
        else if(ident==QLatin1String("signed")) {
            tgt.is_signed=true;
        }
        else if(ident==QLatin1String("unsigned")) {
            tgt.is_unsigned=true;
        } else {
            mod_found = false;
            pu.offset = offset;
            continue;
        }
    }
}
// Handles only very simple types : TypeName | TemplateName<TypeName>
static void parseTypeSpec(ParseHead &pu,ArgTypeDecl &tgt) {
    // get typename
    pu.skipWS();
    tgt.type_name = pu.getIdent();
    assert(tgt.type_name.size()>1);
    pu.skipWS();
    // Find internal text of <.....>
    if(!pu.atEnd() && pu.peek()=='<') {
        pu.consume();
        int start_ab = pu.offset;
        int nesting_depth=1;
        while(!pu.atEnd()) {
            QChar c = pu.peek();
            pu.consume();
            if(c=='<') {
                nesting_depth++;
            }
            else if(c=='>') {
                nesting_depth--;
                if(nesting_depth==0)
                    break;
            }
        }
        tgt.template_params = pu.slice().mid(start_ab,pu.offset-start_ab-1);
    }
}
//NOTE: this only handles west-const style definitions.
static bool parseArgTypeDecl(ParseHead &pu,ArgTypeDecl &tgt) {
    // mods* type_spec [*|&]?
    parseArgTypeMod(pu,tgt.mod);
    parseTypeSpec(pu,tgt);
    pu.skipWS();

    if(pu.atEnd()) {
        tgt.calc_pass_by();
        return true;
    }
    int offset=pu.offset;
    QChar m = pu.take();
    pu.skipWS();
    QChar following = pu.atEnd() ? QChar(0) : pu.peek();
    // disallow ** *& &*
    if( (m=='*' && following=='*') || (m=='*' && following=='&') || (m=='&' && following=='*') ) {
        qCritical() << "Unhandled function return/argment type";
        return false;
    }
    if(m=='*' ) {
        tgt.is_pointer=true;
        offset++; // keep it
    }
    if(m=='&') {
        if(following=='&') {
            tgt.is_move=true;
            // no need to rewind
            return true;
        } else {
            tgt.is_reference=true;
            offset++; // keep it
        }
    }
    pu.offset = offset;
    tgt.calc_pass_by();
    return true;
}
static void parseArgumentDefault(ParseHead &pu,ArgTypeDecl &tgt) {
    static QString braced_default("{}");
    int bracket_nest_level=0;
    int paren_nest_level=0;

    pu.skipWS();

    int start_offset=pu.offset;
    bool in_string=false;
    while(!pu.atEnd()) {
        pu.skipWS();
        QChar c = pu.take();
        if(in_string) { // we search for ending '"'
            QChar next= pu.atEnd() ? '\0' : pu.peek();
            if(c=='\\' && next=='"') {
                // skip escaped
                pu.consume();
            } else if (c=='"') {
                in_string = false;
            }
            continue;
        } else if (c=='"') {
            in_string=true;
            continue;
        }
        if(bracket_nest_level==0 && paren_nest_level==0 && c==',') {
            // don't collect ','
            pu.offset-=1;
            break;
        }
        if(c=='{') {
            bracket_nest_level++;
        } else if (c=='}') {
            bracket_nest_level--;
        }
        if(c=='(') {
            paren_nest_level++;
        } else if (c==')') {
            paren_nest_level--;
        }
    }
    tgt.default_value = pu.slice().mid(start_offset,pu.offset-start_offset);
    if(tgt.default_value.startsWith(tgt.type_name)) {
        if(tgt.default_value.mid(tgt.type_name.size())==QLatin1String("()")) {
            qDebug() << "Replacing explicit constructor call with {}";
            tgt.default_value = braced_default;
        }
        else
            qWarning() << "Invocable function with default argument that uses type constructor directly, will likely not work";
    }
}
static void parseDeclArguments(ParseHead &pu,MethodDecl &tgt) {
    while(!pu.atEnd()) {
        pu.skipWS();
        ArgTypeDecl arg;
        parseArgTypeDecl(pu,arg);
        pu.skipWS();
        arg.arg_name = pu.getIdent();
        pu.skipWS();
        if(!pu.atEnd() && pu.peek()=='=') {
            pu.consume();
            parseArgumentDefault(pu,arg);
        }
        if(!pu.atEnd() && pu.peek()==',') {
            pu.consume();
        }
        tgt.args.push_back(arg);
    }

}
//static QString dumpArgType(const ArgTypeDecl &arg) {
//    QString fmt;
//    switch(arg.pass_by) {
//    case TypePassBy::Value:
//        fmt = "%1 ";
//        break;
//    case TypePassBy::Reference:
//        fmt = "%1 &";
//        break;
//    case TypePassBy::ConstReference:
//        fmt = "const %1 &";
//        break;
//    case TypePassBy::Move:
//        fmt = "%1 &&";
//        break;
//    case TypePassBy::Pointer:
//        fmt = "%1 *";
//        break;
//    case TypePassBy::ConstPointer:
//        fmt = "const %1 *";
//        break;
//    default:
//        assert(false);
//        break;
//    }
//    if(!arg.template_params.isEmpty()) {
//        return fmt.arg(arg.type_name.toString()+"<"+arg.template_params.toString()+ ">");
//    }
//    else {
//        return fmt.arg(arg.type_name);
//    }
//}
//static QString dumpArg(const ArgTypeDecl &arg) {
//    QString out;
//    out += dumpArgType(arg)+arg.arg_name.toString();
//    if(!arg.default_value.isEmpty()) {
//        out += "=" + arg.default_value.toString();
//    }
//    return out;
//}
//static void dumpFuncDecl(const MethodDecl &tgt) {
//    QString res = dumpArgType(tgt.return_type);
//    res+=tgt.name;
//    res.push_back('(');
//    QStringList parts;
//    for(const auto &e : tgt.args) {
//        parts.push_back(dumpArg(e));
//    }
//    res+=parts.join(',');
//    res.push_back(')');
//    qDebug().noquote()<<res;
//}

static void parseDeclAttrib(ParseHead &pu,MethodDecl &tgt) {
    while(true) {
        int offset = pu.offset;
        QStringView tok = pu.getIdent();
        if(tok.isEmpty())
            return;
        if(tok==QLatin1String("virtual")) {
            tgt.is_virtual=true;
            continue;
        }
        if(tok==QLatin1String("static")) {
            tgt.is_static=true;
            continue;
        }
        if(tok==QLatin1String("constexpr")) {
            tgt.is_constexpr=true;
            continue;
        }
        if(tok==QLatin1String("inline")) {
            continue;
        }
        // Not recognized as a decl attribute, rewind and return
        pu.offset = offset;
        break;
    }
}
TypeReference convertToTref(const ArgTypeDecl &from) {
    TypeReference res(from.type_name.toString());
    res.pass_by = from.pass_by;
    res.name = from.type_name.toString();
    res.template_argument = from.template_params.toString();
    return res;
}

void addMethod(const ParseHead &pu,const MethodDecl &mdecl)
{
    assert(!pu.tu.nesting_stack.empty());
    TS_TypeLike *tl = pu.tu.nesting_stack.back();
    TS_Function *func = new TS_Function(mdecl.name.toString());

    func->return_type = convertToTref(mdecl.return_type);
    int idx=0;
    for(const auto &arg : mdecl.args) {
        func->arg_values.append(arg.arg_name.toString());
        func->arg_types.append(convertToTref(arg));
        if(!arg.default_value.isEmpty())
            func->arg_defaults[idx] = arg.default_value.toString();
        ++idx;
    }
    func->m_static = mdecl.is_static;
    func->m_virtual = mdecl.is_virtual;

    tl->add_child(func);
}

void addSignal(const ParseHead &pu,const MethodDecl &mdecl)
{
    assert(!pu.tu.nesting_stack.empty());
    TS_TypeLike *tl = pu.tu.nesting_stack.back();
    TS_Signal *func = new TS_Signal(mdecl.name.toString());

    assert(mdecl.return_type.type_name==QLatin1String("void"));

    int idx=0;
    for(const auto &arg : mdecl.args) {
        func->arg_values.append(arg.arg_name.toString());
        func->arg_types.append(convertToTref(arg));
        if(!arg.default_value.isEmpty())
            func->arg_defaults[idx] = arg.default_value.toString();
        ++idx;
    }
    tl->add_child(func);
}


// Not a very smart function decl parser.
// handles a small-ish subset of all possible decls
MethodDecl parseMethod(ParseHead &pu) {
    MethodDecl mdecl;

    parseDeclAttrib(pu,mdecl);

    if(!parseArgTypeDecl(pu,mdecl.return_type) ) {
        mdecl.name = {};
        return mdecl;
    }
    mdecl.name = pu.getIdent();

    pu.skipWS();
    // Arguments
    assert(pu.peek()=='(');
    pu.consume();
    int start_args=pu.offset;
    int nesting_depth=1;
    while(!pu.atEnd() && nesting_depth!=0) {
        QChar c = pu.take();
        if(c=='(') {
            nesting_depth++;
        } else if(c==')') {
            nesting_depth--;
        }
    }
    ParseHead arg_block(pu,start_args,pu.offset-start_args-1); // -1 to account for the closing ')'
    parseDeclArguments(arg_block,mdecl);

    //dumpFuncDecl(mdecl);

    return mdecl;
}

void processParameterlessMacro(ParseHead &pu,QStringView macroname) {
    if(macroname==QLatin1String("INVOCABLE")) {
        auto mdecl = parseMethod(pu);
        if(!mdecl.name.empty()) {
            addMethod(pu,mdecl);
        }
    } else if(macroname==QLatin1String("SIGNAL")) {
        auto mdecl = parseMethod(pu);
        if(!mdecl.name.empty()) {
            addSignal(pu,mdecl);
        }

    } else if(macroname==QLatin1String("SIGNALS")) {
        qCritical() << "Support for SE_SIGNALS is not finished yet";
        //TODO: finish this, the main problem is detecting all valid function definitions that follow.
        pu.collecting_signals = true;
    } else {
        qDebug()<< "Found unhandled parameterless macro"<<macroname;

    }
}

static void recordBlockName(ParseHead &pu) {
    QStringView substr(pu.slice().mid(0,pu.offset));
    // search backwards for things that are 100% not a part of class/struct/namespace definition.
    //NOTE: This does not take into account some crazy things like class Foo : public Wow<";\"">
    QChar fin_chars[] = { ';','"','\'','{','}'};
    for(QChar c : fin_chars) {
        int prev_endchar_idx = substr.lastIndexOf(c);
        if(prev_endchar_idx!=-1) {
            // 'SOME_CHAR'.....END_OF_BUFFER
            substr=substr.mid(prev_endchar_idx+1);
        }
    }
    QString zxx1=substr.toString();
    const QLatin1String incorrect[]= {QLatin1String("if"),QLatin1String("enum class"),QLatin1String("enum"),QLatin1String("while")};

    for(const QLatin1String &s : incorrect) {
        int prev_kw_idx = substr.lastIndexOf(s);
        if(prev_kw_idx!=-1) {
            QChar c;
            // check for ;|WS before and WS after the keyword
            if(prev_kw_idx!=0) {
                c = substr[prev_kw_idx-1];
                if(c!=';' && !c.isSpace())
                    continue;
            }
            c = substr[prev_kw_idx+s.size()];
            if(!c.isSpace())
                continue;
            // 'SOME_CHAR'.....END_OF_BUFFER
            substr=substr.mid(prev_kw_idx+s.size());
        }
    }
    QString zxx=substr.toString();
    if(substr.size()<7) {
        // not enough chars for the simplest case of `class A`
        return;
    }
    const QLatin1String keywords[]= {QLatin1String("class"),QLatin1String("struct"),QLatin1String("namespace")};
    int kw_idx=-1;
    int idx=0;
    for(const QLatin1String &s : keywords) {
        int prev_class_kw_idx = substr.lastIndexOf(s);
        if(prev_class_kw_idx!=-1) {
            QChar c;
            // check for ;|WS before and WS after the keyword
            if(prev_class_kw_idx!=0) {
                c = substr[prev_class_kw_idx-1];
                if(c!=';' && !c.isSpace())
                    continue;
            }
            c = substr[prev_class_kw_idx+s.size()];
            if(!c.isSpace())
                continue;
            kw_idx = idx;
            // 'SOME_CHAR'.....END_OF_BUFFER
            substr=substr.mid(prev_class_kw_idx+s.size());
        }
        idx++;
    }
    if(kw_idx==-1) {
        return;
    }
    substr=substr.trimmed();
    QString full_dezf = substr.toString();
    QStringView full_def=substr;
    if(kw_idx==0 || kw_idx==1) {
        // processing class_name : base_class
        int offset=0;
        int angle_nesting=0;
        //NOTE: this will fail in case of lshift or rshift operator use in the class/struct name
        for(int fin=substr.size(); offset<fin; ++offset) {
            QChar c = substr[offset];
            if(c=='>') {
                angle_nesting--;
            } else if (c=='<') {
                angle_nesting++;
            }
            if(angle_nesting!=0)
                continue;
            if(!(c.isLetterOrNumber() || c=='_')) {
                if(c==':' && (offset+1)<fin && substr[offset+1]==':') {
                    // double :: namespace/class path
                    offset++;
                    continue;
                }
                break;
            }
        }
        substr = substr.mid(0,offset);
    } else {
        // processing namespace name
        int offset=0;
        for(int fin=substr.size(); offset<fin; ++offset) {
            QChar c = substr[offset];
            if(!(c.isLetterOrNumber() || c=='_' || c==':')) {
                break;
            }
        }
        substr = substr.mid(0,offset);
    }
    if(!substr.isEmpty()) {
        pu.tu.name_stack.push_back({substr,full_def,pu.bracket_nesting_level,(BlockType)kw_idx});
    }
}
static void startBlock(ParseHead &pu) {
    pu.tu.open_brace_indices.push_back(pu.offset);
    pu.bracket_nesting_level++;
    recordBlockName(pu);
}
/*
    simple tag grammar:
    SE_NAMESPACE can appear in two contexts:

    1. translation unit:
    `#pragma once
    SE_NAMESPACE(Foo)
    `
    The `Foo` namespace will be defined and apply to all following types

    2. block
    `
    namespace X {
        SE_NAMESPACE(X)
    }
    `
    The `X` will be defined and applied to all types defined in c++ namespace block.

    SE_ENUM(Name) must appear directly after enum definition
*/
int processBlock(ParseHead &pu) {
    bool added_ns=false;

    int line_counter = 0;
    bool valid_start=true; // set to false until we encounter [START,WS,EOL]

    while (!pu.atEnd()) {
        if(!pu.error.isEmpty()) {
            return -1;
        }

        QChar c = pu.peek();
        if(c=='{') {
            startBlock(pu);
            pu.consume();
            continue;
        } else if(c=='}') {
            pu.tu.open_brace_indices.pop_back();
            while(!pu.tu.brace_nesting_stack.isEmpty() && pu.bracket_nesting_level<=pu.tu.brace_nesting_stack.back()) {
                endBlock(pu);
            }
            if(!pu.tu.name_stack.empty() && pu.tu.name_stack.back().level==pu.bracket_nesting_level) {
                pu.tu.name_stack.pop_back();
            }
            pu.bracket_nesting_level--;
            pu.consume();
            continue;
        }

        int eol_chars = isEOL(pu.peek(2));
        if (eol_chars != 0 || c.isSpace()) {
            valid_start = true;
        }
        int consume_chars = 0;
        if (eol_chars != 0) {
            line_counter++;
            consume_chars = eol_chars;
        } else if (c.isSpace() || !valid_start) {
            consume_chars = 1;
        }

        if(consume_chars!=0) {
            pu.consume(consume_chars);
            continue;
        }

        // we search for start of on of the macro keywords
        QStringView partial(pu.peek(3));
        if (partial != QLatin1String("SE_")) {
            pu.consume();
            valid_start = false;
            continue;
        }
        // contents at idx are SE_...
        pu.consume(3);

        int end_macro_name = pu.searchForward(QVector<QChar>{'(',' ','\t','\n'});
        if (end_macro_name == -1) { // something strange going on, bail
            qDebug() << "Failed to parse macro name.";
            break;
        }

        bool non_parametric_token=false; // SE_INVOCABLE, SE_SIGNALS etc.
        QStringView macro_name(pu.peek(end_macro_name - pu.offset));
        if(pu.slice()[end_macro_name]==' ')
            non_parametric_token = true;
        pu.consume(macro_name.size() + 1); // consume '('|' ' as well

        if(non_parametric_token) {
            processParameterlessMacro(pu,macro_name);
            continue;
        }

        int end_of_macro = pu.searchForward(')');
        QStringView macro_params = pu.peek(end_of_macro - pu.offset).trimmed();

        pu.consume(end_of_macro - pu.offset);

        if (macro_name == QLatin1String("NAMESPACE")) {
            if(macro_params.isEmpty()) {
                qWarning() << "SE_NAMESPACE requires a parameter";
                continue;
            }
            //qDebug() << "NS:" << macro_params << " in" << file;
            startNamespace(pu, macro_params);
            continue;
        }
        // BIND_GLOBAL_CONSTANT(SPKEY)
        if (macro_name == QLatin1String("CONSTANT")) {
            if(macro_params.isEmpty()) {
                qWarning() << "SE_CONSTANT requires a parameter";
                continue;
            }
            added_ns = ensureNS(pu);
            //qDebug() << "CONSTANT:" << macro_params << " in" << file;
            addConstant(pu, macro_params);
            continue;
        }
        if (macro_name == QLatin1String("ENUM")) {
            if(macro_params.isEmpty()) {
                qWarning() << "SE_ENUM requires a parameter";
                continue;
            }
            added_ns = ensureNS(pu);
            //qDebug() << "ENUM:" << macro_params << " in" << file;
            addEnum(pu, macro_params);
            continue;
        }
        if (macro_name == QLatin1String("CLASS")) {
            added_ns = ensureNS(pu);
            processSEClass(pu, macro_params);
            continue;
        }
        if (macro_name == QLatin1String("END")) {
            endBlock(pu);
            if(!pu.tu.name_stack.empty() && pu.tu.name_stack.back().level==pu.bracket_nesting_level) {
                pu.tu.name_stack.pop_back();
            }
        }
    }
    if(added_ns) {
        endBlock(pu);
    }
    return 0;
}
void pseudoPreprocessor(QString &source) {
    // for now we only do a single thing: replace GODOT_EXPORT macros.
    QRegularExpression z("[\\s\\n]GODOT_EXPORT[\\s\\n]",QRegularExpression::MultilineOption);
    source.replace(z," ");
}
bool processFile(const QString &filename, QIODevice *dev) {
    ProcessingUnit pu;
    pu.filename = filename;
    pu.contents = QString::fromUtf8(removeComments(dev->readAll()));
    pseudoPreprocessor(pu.contents);

    ParseHead head(pu);
    return processBlock(head)==0;
}

static bool save_cpp(ReflectionData &data, QIODevice *io) {
    VisitorInterface *visitor = createCppVisitor();
    for (const auto *v : data.namespaces) {
        v->accept(visitor);
    }
    produceCppOutput(visitor,io);
    delete visitor;
    return true;
}

bool processModuleDef(QString path) {
    ModuleDefinition mod;
    if (!loadModuleDefinition(mod, path)) {
        return false;
    }
    QString file_path = QFileInfo(path).path();
    QDir::setCurrent(file_path);
    auto zz = QDir::currentPath();
    for (const QString &root : mod.top_directories) {
        QDirIterator iter(root, QStringList({ "*.cpp", "*.h" }), QDir::NoFilter, QDirIterator::Subdirectories);
        while (iter.hasNext()) {
            QFile fl(iter.next());
            if (!fl.open(QFile::ReadOnly)) {
                qCritical() << "Failed to open file" << fl;
                continue;
            }
            if(!processFile(fl.fileName(), &fl)) {
                qCritical() << "Error while processing file" << fl.fileName();
                return false;
            }
        }
    }

    g_rd.config.module_name = mod.name;
    g_rd.config.version = mod.version;
    g_rd.config.api_version = mod.api_version;
    return true;
}

bool processHeader(const QString &fname, QIODevice *src) {
    return processFile(fname, src);
}
bool exportJson(QIODevice *tgt) {
    return save_to_file(g_rd, tgt);
}
bool exportCpp(QIODevice *tgt) {
    return save_cpp(g_rd, tgt);
}

void initContext() {
    for(auto x : g_rd.created_types) {
        delete x;
    }
    g_rd = {};
}

void setConfig(const ModuleConfig &mc)
{
    g_rd.config = mc;
}
