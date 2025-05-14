#pragma once

/// Standard baudrate values
enum cl_mb_device_baudrate {
    /// Default.
    Baud9600 = 9600,
    Baud38400 = 38400,
};
/// Parity types.
enum cl_mb_device_parity {
    /// Default.
    None = 0,
    Odd = 1,
    Even = 2,
};
/// A Modbus register can hold 3 types of data.
enum cl_mb_channel_type {
    /// A boolean value.
    Coil,
    /// 16bit value.
    Int,
    /// Needs 2 congruent 16bit registers.
    Real,
};

/// Modbus device type: TCP/Serial.
enum cl_mb_device_type {
    /// Connect over a socket using IP address and a port.
    Tcp,
    /// Connect over a serial port using the COM identifier.
    Serial,
};
/// A type to hold address, type and data of a Modbus register.
typedef struct cl_mb_channel {
    int id;
    char const *name;

    enum cl_mb_channel_type value_type;
    /// A channel can only hold float values even when its type is Int or Coil.
    float value;
} cl_mb_channel;

/**
** cl_mb_device is the root type for the Modbus device.
** It holds all device configuration and channels data.
**/
typedef struct cl_mb_device {
    int id;
    char const *name;
    /// Used for TCP device.
    char *ip;
    /// Used for TCP device.
    int port;
    /// Used for serial device.
    char *com_port;
    /// Used for serial device.
    enum cl_mb_device_baudrate baudrate;
    /// Used for serial device.
    enum cl_mb_device_parity parity;
    /// Used to select which device configuration to use.
    enum cl_mb_device_type device_type;
    /// A special string that identifies the device in calculations .i.e "MB" or
    /// "AI".
    char const *name_identifier;
    /// A list of all the MB channels in the device.
    cl_mb_channel *channels;
    /// Keeps count of the number of channels.
    size_t channel_count;
} cl_mb_device;

cl_mb_device *cl_device_init_tcp(char const *name, size_t n_channels);
cl_mb_device *cl_device_init_rtu(char const *name, size_t n_channels);
int cl_device_destroy(cl_mb_device *device);
