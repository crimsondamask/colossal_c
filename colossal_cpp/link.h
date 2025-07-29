#pragma once
#include "libmodbus/modbus.h"
#include "libplctag/libplctag.h"
#include "snap7/snap7.h"
// #include <open62541/client_config_default.h>
// #include <open62541/client_highlevel.h>
// #include <open62541/client_subscriptions.h>
// #include <open62541/plugin/log_stdout.h>

#include <cstddef>
#include <cstdint>

#define TAG_NAME_BUF_LEN 128
#define SIEMENS_ERR_BUF_LEN 1024
#define TAG_DESC_BUF_LEN 64
#define TAG_UNIT_BUF_LEN 8
#define LINK_NAME_BUF_LEN 1024
#define COM_PORT_BUF_LEN 16
#define IP_BUF_LEN 32
#define ERR_MSG_BUF_LEN 1024
#define URL_BUF_LEN 256
#define TOKEN_BUF_LEN 256

#define N_CHANNELS 200
#define N_DEVICES 2
#define N_FRAMES_UNTIL_CONS 60
#define EIP_TAG_TEMPLATE "protocol=ab_eip&gateway=%s&path=1,0&cpu=LGX&elem_count=1&name=%s"

#define CL_SERIAL_PARITY_NONE 'N'
#define CL_SERIAL_PARITY_EVEN 'E'
#define CL_SERIAL_PARITY_ODD 'O'
#define CL_VALUE_INT 0
#define CL_VALUE_REAL 1
#define CL_VALUE_BOOL 2

typedef enum LinkProtocol
{
    MB_TCP = 0,
    MB_SERIAL,
    EIP,
    SIEMENS_S7,
    OPCUA,
} LinkProtocol;

typedef enum MbFunction
{
    MB_HOLDING,
    MB_INPUT,
    MB_COIL,
} MbFunction;

typedef enum ValueType
{
    VALUE_INT = 0,
    VALUE_REAL,
    VALUE_BOOL,
} ValueType;

typedef enum BaudRate
{
    BR_9600 = 9600,
    BR_19200 = 19200,
    BR_38400 = 38400,
    BR_115200 = 115200,
} BaudRate;

typedef enum LoggingType
{
    CL_LOCAL_LOGGING = 0,
    CL_REMOTE_LOGGING = 1,
} LoggingType;

typedef struct MbTcpConfig
{
    char ip[IP_BUF_LEN];
    int port;
    modbus_t *ctx;

} MbTcpConfig;

typedef struct MbSerialConfig
{
    char com_port[COM_PORT_BUF_LEN];
    long baudrate;
    char parity;
    modbus_t *ctx;

} MbSerialConfig;

typedef struct EipConfig
{
    char ip[IP_BUF_LEN];
    char path[16];
} EipConfig;

typedef struct S7Config
{
    char ip[IP_BUF_LEN];
    TS7CpuInfo cpu_info;
    int rack;
    int slot;
    S7Object client; // This is just an int value used for the negotiated handle.
} S7Config;

typedef struct OpcUaConfig
{
    char url[IP_BUF_LEN];
    // UA_Client *client;
} OpcUaConfig;

typedef struct LinkConfig
{
    MbTcpConfig mb_tcp_config;
    MbSerialConfig mb_serial_config;
    S7Config s7_config;
    EipConfig eip_config;
    OpcUaConfig opcua_config;

} LinkConfig;

typedef struct S7TagAddress
{
    int s7_area;
    int db_number;
    int start;
    int start_bit;
    int length;
    int amount;
} S7TagAddress;

typedef struct EipTagAddress
{
    char eip_path[TAG_NAME_BUF_LEN];
    char tag_name[TAG_NAME_BUF_LEN];
    int32_t eip_tag_ptr;
} EipTagAddress;
typedef struct TagAddress
{
    int mb_addr;
    EipTagAddress eip_tag_addr;
    S7TagAddress s7_tag_addr;
} TagAddress;

typedef struct TagValue
{
    int int_value;
    float real_value;
    bool bool_value;
} TagValue;

typedef struct Tag
{
    char name[TAG_NAME_BUF_LEN];
    char description[TAG_DESC_BUF_LEN];
    char unit[TAG_DESC_BUF_LEN];
    int id;
    bool enabled;
    bool logged;
    TagAddress tag_addr;
    int value_type;
    int protocol;
    TagValue tag_value;
    bool is_error;
    char err_msg[ERR_MSG_BUF_LEN];

} Tag;

typedef struct Link
{
    char name[LINK_NAME_BUF_LEN];
    int id;
    bool active;
    int protocol;
    LinkConfig link_config;
    bool need_to_reconnect;
    bool is_error;
    char err_msg[ERR_MSG_BUF_LEN];
    unsigned long timestamp;
    size_t tag_count;
    Tag *tags;
    // 0 for Local logging and 1 for remote
    // TODO an enum would be better but int works more easily with GUI Combobox
    int logging_type;
    char url[URL_BUF_LEN];
    char token[TOKEN_BUF_LEN];
    unsigned long log_count;
} Link;

Link *cl_new_link(char const *name, int id, int protocol, LinkConfig config, size_t tag_count, bool active);
int cl_connect_link(Link *link);
void cl_destroy_link(Link *link);

int cl_new_tag(Link *link, char const *name, int id, TagAddress tag_addr, int value_type, int protocol, bool enabled);
int cl_read_tag(Link *link, int tag_id);
int cl_write_tag(Link *link, Tag *tag);
