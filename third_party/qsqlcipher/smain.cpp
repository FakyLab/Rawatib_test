#include "smain.h"

QSqlDriver *QSQLCipherDriverPlugin::create(const QString &name)
{
    if (name == QLatin1String("QSQLCIPHER"))
        return new QSQLiteDriver();
    return nullptr;
}
