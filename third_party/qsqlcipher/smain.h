#pragma once
#include "qsql_sqlite_p.h"
#include <QtSql/qsqldriverplugin.h>
#include <QtCore/qplugin.h>

class QSQLCipherDriverPlugin : public QSqlDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QSqlDriverFactoryInterface"
                      FILE "sqlcipher.json")
public:
    QSQLCipherDriverPlugin() : QSqlDriverPlugin() {}
    QSqlDriver *create(const QString &name) override;
};
