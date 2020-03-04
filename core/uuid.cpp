#include "uuid.h"

#include "core/string.h"

#include <QtCore/QUuid>
namespace  {
constexpr const char HEX_TO_LITERAL[16] =
        { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

} // end of anonymous namespace
namespace se {
UUID::UUID(const String &uuid)
{
    QUuid from=QUuid::fromString(QLatin1String(uuid.begin(),uuid.end()));
    memcpy(m_data,&from.data1,sizeof(se::UUID));
}

UUID UUID::generate()
{
    QUuid from=QUuid::createUuid();
    se::UUID res;
    static_assert (sizeof(QUuid)==sizeof(se::UUID));
    memcpy(res.m_data,&from.data1,sizeof(se::UUID));
    return res;
}
String UUID::to_string() const
{
    uint8_t output[36];
    uint32_t idx = 0;

    // First group: 8 digits
    for(uint32_t i = 7; i >= 0; --i)
    {
        uint32_t hexVal = (m_data[0] >> (i * 4)) & 0xF;
        output[idx++] = HEX_TO_LITERAL[hexVal];
    }

    output[idx++] = '-';

    // Second group: 4 digits
    for(int32_t i = 7; i >= 4; --i)
    {
        uint32_t hex_val = (m_data[1] >> (i * 4)) & 0xF;
        output[idx++] = HEX_TO_LITERAL[hex_val];
    }

    output[idx++] = '-';

    // Third group: 4 digits
    for(int32_t i = 3; i >= 0; --i)
    {
        uint32_t hexVal = (m_data[1] >> (i * 4)) & 0xF;
        output[idx++] = HEX_TO_LITERAL[hexVal];
    }

    output[idx++] = '-';

    // Fourth group: 4 digits
    for(int32_t i = 7; i >= 4; --i)
    {
        uint32_t hexVal = (m_data[2] >> (i * 4)) & 0xF;
        output[idx++] = HEX_TO_LITERAL[hexVal];
    }

    output[idx++] = '-';

    // Fifth group: 12 digits
    for(int32_t i = 3; i >= 0; --i)
    {
        uint32_t hexVal = (m_data[2] >> (i * 4)) & 0xF;
        output[idx++] = HEX_TO_LITERAL[hexVal];
    }

    for(int32_t i = 7; i >= 0; --i)
    {
        uint32_t hexVal = (m_data[3] >> (i * 4)) & 0xF;
        output[idx++] = HEX_TO_LITERAL[hexVal];
    }

    return String((const char*)output, 36);
}
}
