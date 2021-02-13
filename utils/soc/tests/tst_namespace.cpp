#include "reflection_walker.h"

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTest>
#include <QBuffer>
#include <QDirIterator>

class SocTest: public QObject
{
    Q_OBJECT

private:
    bool isValidJson(const QByteArray &dat) {
        QJsonParseError err;
        QJsonDocument::fromJson(dat,&err);
        if(err.error!=QJsonParseError::NoError)
            qCritical() << err.errorString();
        return err.error==QJsonParseError::NoError;
    }
private slots:
    void allTests_data() {
        QTest::addColumn<QByteArray>("source");
        QTest::addColumn<QByteArray>("expected");
        QDirIterator test_cases("test_cases",{ "*.h" }, QDir::NoFilter, QDirIterator::Subdirectories);
        while(test_cases.hasNext()) {
            QFileInfo fi(test_cases.next());
            QFile src_h(fi.filePath());
            QFile src_json(QString(fi.filePath()).replace(".h",".json"));
            if(!src_h.open(QFile::ReadOnly)) {
                qCritical() << "Failed to open test case header"<<fi.filePath();
            }
            if(!src_json.open(QFile::ReadOnly)) {
                qCritical() << "Failed to open test case target"<<src_json.fileName();
            }
            QByteArray src_json_content=src_json.readAll();
            QTest::newRow(qPrintable(fi.fileName())) << QByteArray(src_h.readAll()) << src_json_content;
        }
    }

    void allTests()
    {
        QFETCH(QByteArray, source);
        QFETCH(QByteArray, expected);

        initContext();
        ModuleConfig config;
        config.default_ns = "GodotCore";
        setConfig(config);

        QByteArray result;
        QBuffer buf(&source);
        QBuffer res(&result);

        res.open(QIODevice::WriteOnly);
        buf.open(QIODevice::ReadOnly);
        // Check for expected failures
        bool processing = processHeader(QTest::currentDataTag(),&buf);
        if(expected.size()==0) {
            QCOMPARE(processing,false);
            return;
        }
        QVERIFY(isValidJson(expected));

        exportJson(&res);

        QVERIFY(!result.isEmpty());
        QVERIFY(isValidJson(result));
        QString result_min = QJsonDocument::fromJson(result).toJson(QJsonDocument::Compact).simplified();
        QString expected_min = QJsonDocument::fromJson(expected).toJson(QJsonDocument::Compact).simplified();
        result_min.remove(' ');
        expected_min.remove(' ');
        if(result_min!=expected_min) {
            qDebug().noquote()<<result_min;
            qDebug().noquote()<<expected_min;
            // try to reduce to smaller diff
            int idx=0;
            while(idx<result_min.size() && idx<expected_min.size() && result_min[idx]==expected_min[idx])
                ++idx;
            result_min = result_min.mid(idx);
            expected_min = expected_min.mid(idx);
        }
        QCOMPARE(result_min,expected_min);
    }

};

QTEST_MAIN(SocTest)
#include "tst_namespace.moc"
