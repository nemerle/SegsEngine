#include "cpp_visitor.h"

#include "type_system.h"
#include <QSet>
namespace  {

struct CppVisitor : public VisitorInterface {
    QSet<QString> headers;
    QMap<QString, QString> class_binders;
    QVector<QString> class_stack; // for nested classes.
    // VisitorInterface interface
    void visit(const TS_Enum *entry) override {

    }
    void visit(const TS_Type *entry) override {
        QString name = entry->c_name();
        class_stack.push_back(name);
        assert(class_binders.contains(name) == false);
        QString &tgt(class_binders[name]);
        for (const auto *child : entry->m_children) {
            child->accept(this);
        }
        tgt = "void " + name + "::_bind_method() {\n";
        tgt += "}\n";
        class_stack.pop_back();
    }
    void visit(const TS_Namespace *entry) override {
        for (const auto *child : entry->m_children) {
            child->accept(this);
        }
    }
    void visit(const TS_Property *entry) override {}
    void visit(const TS_Signal *entry) override {}
    void visit(const TS_Function *entry) override {}
    void visit(const TS_Constant *entry) override {}
    void visit(const TypeReference *entry) override {}
    ~CppVisitor() override = default;
};

}

VisitorInterface *createCppVisitor() {
    return new CppVisitor;
}

void produceCppOutput(VisitorInterface *iface,QIODevice *tgt) {
    auto *cpp_iface=dynamic_cast<CppVisitor *>(iface);
    assert(cpp_iface);
    QString content;
    // TODO: extract all headers from associated cpp file
    QStringList parts = cpp_iface->headers.values();
    parts.sort();
    content += parts.join("\n");
    tgt->write(content.toUtf8());
}
