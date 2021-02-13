#include "reflection_walker.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QBuffer>
#include <functional>


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

    initContext();

    QString default_ns = parser.value("namespace");
    if(default_ns.isEmpty()) {
        default_ns = "Godot";
    }
    ModuleConfig config;
    config.default_ns = default_ns;
    setConfig(config);
    //NOTE: Simplified parser doesn't handle '{' and '}' embedded within strings, check if input contains such

    for(QString arg : args ) {
        if(arg.endsWith("json")) {
            if(!processModuleDef(arg))
                return -1;
            QFile tgtFile(QFileInfo(arg).baseName()+"_rfl.json");
            if (!tgtFile.open(QIODevice::WriteOnly | QIODevice::Text))
                return -1;
            exportJson(&tgtFile);
        }
        if(arg.endsWith(".h")) {
            if(!QFile::exists(arg))
                return -1;
            QFile src_file(arg);
            if (!src_file.open(QFile::ReadOnly)) {
                qCritical() << "Failed to open source file:" << arg;
                return -1;
            }

            QFile outFile("output.json");
            if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
                return -1;

            if(!processHeader(arg,&src_file))
                return -1;
            QFile tgtFile(QFileInfo(arg).baseName()+"_soc.cpp");
            if (!tgtFile.open(QIODevice::WriteOnly | QIODevice::Text))
                return -1;
            exportCpp(&tgtFile);
        }
    }
    return 0;
}
