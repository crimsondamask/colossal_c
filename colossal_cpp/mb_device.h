#pragma once

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
    Coil,
    /// 16bit value.
    Int,
    /// Needs 2 congruent 16bit registers.
    Real,
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
    char const *name;

    enum MbChannelType value_type;
    /// A channel can only hold float values even when its type is Int or Coil.
    float value;
} MbChannel;

/**
** cl_mb_device is the root type for the Modbus device.
** It holds all device configuration and channels data.
**/
typedef struct MbDevice
{
    int id;
    char const *name;
    /// Used for TCP device.
    char const *ip;
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
} MbDevice;

MbDevice cl_device_init_tcp(char const *name, size_t n_channels);
MbDevice *cl_device_init_rtu(char const *name, size_t n_channels);
int cl_device_destroy(MbDevice *device);
