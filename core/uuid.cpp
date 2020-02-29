#include "uuid.h"

#include "core/string.h"

#include <QtCore/QUuid>

se::UUID::UUID(const String &uuid)
{
    QUuid from=QUuid::fromString(QLatin1String(uuid.begin(),uuid.end()));
    memcpy(m_data,&from.data1,sizeof(se::UUID));
}

se::UUID se::UUID::generate()
{
    QUuid from=QUuid::createUuid();
    se::UUID res;
    static_assert (sizeof(QUuid)==sizeof(se::UUID));
    memcpy(res.m_data,&from.data1,sizeof(se::UUID));
    return res;
}
