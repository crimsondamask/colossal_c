#pragma once

#define CONFIG_EDIT_DEVICE_CONFIG 1
#define CONFIG_EDIT_CHANNEL_CONFIG 2

#include "device.h"

enum ClDeviceError
{
    MbSocketError,
};
/// Standard baudrate values
enum DeviceBaudRate
{
    /// Default.
    Baud9600 = 9600,
    Baud38400 = 38400,
};
/// Parity types.
enum DeviceParity
{
    /// Default.
    None = 0,
    Odd = 1,
    Even = 2,
};
/// A Modbus register can hold 3 types of data.
enum MbChannelType
{
    /// A boolean value.
    Coil = 0,
    /// 16bit value.
    Int = 1,
    /// Needs 2 congruent 16bit registers.
    Real = 2,
};

/// Modbus device type: TCP/Serial.
enum MbDeviceType
{
    /// Connect over a socket using IP address and a port.
    Tcp,
    /// Connect over a serial port using the COM identifier.
    Serial,
};
/// A type to hold address, type and data of a Modbus register.
typedef struct MbChannel
{
    int id;
    bool enabled;
    bool logged;
    char tag[16];
    int address;

    int channel_type;
    enum MbChannelType value_type;
    /// A channel can only hold float values even when its type is Int or Coil.
    float value;
    char description[48];
    char unit[48];
} MbChannel;

/**
** cl_mb_device is the root type for the Modbus device.
** It holds all device configuration and channels data.
**/
typedef struct MbDevice
{
    int id;
    char name[32];
    /// Used for TCP device.
    char ip[32];
    /// Used for TCP device.
    int port;
    /// Used for serial device.
    char const *com_port;
    /// Used for serial device.
    enum DeviceBaudRate baudrate;
    /// Used for serial device.
    enum DeviceParity parity;
    /// Used to select which device configuration to use.
    enum MbDeviceType device_type;
    /// A special string that identifies the device in calculations .i.e "MB" or
    /// "AI".
    char const *name_identifier;
    /// A list of all the MB channels in the device.
    MbChannel *channels;
    /// Keeps count of the number of channels.
    size_t channel_count;
    /// Timestamp of the data acquired.
    unsigned long timestamp;
    /// Error flag. This is set in case of error.
    bool is_error;
    /// Error message.
    const char *error_msg;
    /// Database url
    char url[256];
    /// Logging type 0: Local. 1: Remote.
    int logging_type;
    char db_table[32];
    char token[256];
    unsigned long log_count;
} MbDevice;

MbDevice cl_device_init_tcp(char const *name, int id, size_t n_channels);
MbDevice *cl_device_init_rtu(char const *name, size_t n_channels);
int cl_device_destroy(MbDevice *device);
