#pragma once
#include "libmodbus/modbus.h"
#include "link.h"
#include "snap7/snap7.h"
#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <string.h>
#include <winnls.h>

int cl_new_tag(Link *link, char const *name, int id, TagAddress tag_addr, int value_type, int protocol)
{

    if (!link)
        return -1;

    if (link->protocol != protocol)
        return -1;

    strcpy_s(link->tags[id].name, name);
    sprintf_s(link->tags[id].description, "No Description for %s", name);
    sprintf_s(link->tags[id].unit, "--");
    link->tags[id].id = id;
    link->tags[id].protocol = protocol;
    link->tags[id].tag_addr = tag_addr;
    link->tags[id].is_error = false;
    link->tags[id].enabled = true;
    link->tags[id].logged = true;
    link->tags[id].value_type = value_type;
    link->tags[id].tag_value.real_value = 0.0;
    link->tags[id].tag_value.int_value = 0;
    link->tags[id].tag_value.bool_value = 0;

    return 0;
}

Link *cl_new_link(char const *name, int id, int protocol, LinkConfig config, size_t tag_count)
{
    Link link_init = {};

    Link *link = &link_init;

    link->id = id;
    strcpy_s(link->name, name);
    link->protocol = protocol;
    link->link_config = config;
    link->tag_count = tag_count;
    link->is_error = true;
    strcpy_s(link->err_msg, "The link is disconnected.");
    link->need_to_reconnect = true;
    link->active = true;
    link->timestamp = 0;
    link->logging_type = CL_REMOTE_LOGGING;
    strcpy_s(link->url, "https://eu-central-1-1.aws.cloud2.influxdata.com/api/v2/write?bucket=mydb&precision=s");
    strcpy_s(link->token, "z2nNGctKjM3B8q7v5ZkAzwY2A8G7oJgO4nTTZQacUhhfOi_6eAqQN91tcmu5H_5TlrDiqxSyILBqwcrAc6vhXA==");
    link->tags = (Tag *)malloc(tag_count * sizeof(Tag));

    for (size_t i = 0; i < tag_count; i++)
    {
        char name_buf[TAG_NAME_BUF_LEN];
        sprintf_s(name_buf, "TAG%d", i);

        // Address initialization with default values.
        TagAddress tag_addr = {};
        tag_addr.mb_addr = (int)i * 2;
        sprintf_s(tag_addr.eip_tag_addr, "Tag%d", i);
        tag_addr.s7_tag_addr.s7_area = S7AreaDB;
        tag_addr.s7_tag_addr.length = S7WLWord;
        tag_addr.s7_tag_addr.db_number = 1;
        tag_addr.s7_tag_addr.start = i * 2;
        tag_addr.s7_tag_addr.start_bit = 0;
        tag_addr.s7_tag_addr.amount = 1;
        int value_type = VALUE_REAL;

        switch (link->protocol)
        {
        case MB_TCP: {
            cl_new_tag(link, name_buf, i, tag_addr, value_type, MB_TCP);
            break;
        }
        case MB_SERIAL: {
            cl_new_tag(link, name_buf, i, tag_addr, value_type, MB_SERIAL);
            break;
        }
        case SIEMENS_S7: {
            cl_new_tag(link, name_buf, i, tag_addr, value_type, SIEMENS_S7);
            break;
        }
        // TODO: switch to the other protocols as well.
        default: {
            cl_new_tag(link, name_buf, i, tag_addr, value_type, MB_TCP);
            break;
        }
        }
    }

    return link;
}

// TODO
/// Connect a link and get a connection context for the protocols that support it (e.g Modbus)
/// This function must be called after cl_link_new.
int cl_connect_link(Link *link)
{
    // Make sure the link is initialized.
    if (!link)
        return -1;

    switch (link->protocol)
    {
    case MB_TCP: {

        link->link_config.mb_tcp_config.ctx =
            modbus_new_tcp(link->link_config.mb_tcp_config.ip, link->link_config.mb_tcp_config.port);

        if (modbus_connect(link->link_config.mb_tcp_config.ctx) == -1)
        {
            link->is_error = true;
            sprintf_s(link->err_msg, "Could not connect to device.");
            return -1;
        }

        // Reset the error flag.
        link->is_error = false;
        break;
    }
    case MB_SERIAL: {
        link->link_config.mb_tcp_config.ctx =
            modbus_new_rtu(link->link_config.mb_serial_config.com_port, link->link_config.mb_serial_config.baudrate,
                           link->link_config.mb_serial_config.parity, 8, 1);

        if (modbus_connect(link->link_config.mb_tcp_config.ctx) == -1)
        {
            link->is_error = true;
            sprintf_s(link->err_msg, "Could not connect to device.");
            return -1;
        }

        // Reset the error flag.
        link->is_error = false;
        break;
    }
    case SIEMENS_S7: {

        int res = {};

        S7Object client = Cli_Create();

        link->link_config.s7_config.client = client;
        res = Cli_ConnectTo(link->link_config.s7_config.client, link->link_config.s7_config.ip,
                            link->link_config.s7_config.rack, link->link_config.s7_config.slot);
        if (res < 0)
        {
            char error_text_buf[1024];
            link->is_error = true;
            Cli_ErrorText(res, error_text_buf, 1024);
            sprintf_s(link->err_msg, "Could not connect to S7 controller: %s", error_text_buf);
            return -1;
        }

        // Reset the error flag.
        link->is_error = false;

        int cpu_info_res = {};

        cpu_info_res = Cli_GetCpuInfo(link->link_config.s7_config.client, &link->link_config.s7_config.cpu_info);

        break;
    }
    default: {
        return -1;
    }
    }

    return 0;
}

int cl_read_tag(Link *link, int tag_id)
{
    if (!link || (tag_id >= link->tag_count))
    {
        return -1;
    }

    Tag *tag = &link->tags[tag_id];

    if (!tag->enabled)
    {
        return -1;
    }

    switch (link->protocol)
    {
    // Modbus Serial ================================================================
    case MB_SERIAL:
        // We do not break to jump to the next case (MB_TCP) as both MB_TCP and MB_SERIAL
        // share the same read functions.

    // Modbus TCP
    // ====================================================================
    case MB_TCP: {

        if ((tag->protocol != MB_TCP) && (tag->protocol != MB_SERIAL))
        {
            tag->is_error = true;
            sprintf_s(tag->err_msg, "The Tag protocol doesn't match the Link protocol");
            return -1;
        }

        int rc;

        switch (tag->value_type)
        {
        case VALUE_REAL: {
            uint16_t read_buf[2] = {};
            rc = modbus_read_registers(link->link_config.mb_tcp_config.ctx, tag->tag_addr.mb_addr, 2, read_buf);

            if (rc == -1)
            {
                tag->is_error = true;
                sprintf_s(tag->err_msg, "Could not read tag.");
                return -1;
            }

            tag->is_error = false;
            tag->tag_value.real_value = modbus_get_float_abcd(read_buf);
            break;
        }
        case VALUE_INT: {
            uint16_t read_buf[2] = {};
            rc = modbus_read_registers(link->link_config.mb_tcp_config.ctx, tag->tag_addr.mb_addr, 1, read_buf);

            if (rc == -1)
            {
                tag->is_error = true;
                sprintf_s(tag->err_msg, "Could not read tag.");
                return -1;
            }

            tag->is_error = false;
            tag->tag_value.int_value = (int)read_buf[0];
            break;
        }
        // TODO
        // Add the ability to get the value of singular bits.
        case VALUE_BOOL: {
            uint8_t read_buf[1] = {};
            rc = modbus_read_bits(link->link_config.mb_tcp_config.ctx, tag->tag_addr.mb_addr, 1, read_buf);

            if (rc == -1)
            {
                tag->is_error = true;
                sprintf_s(tag->err_msg, "Could not read tag.");
                return -1;
            }

            tag->is_error = false;
            // We get an int value with the first 8 bits representing 8 coils.
            tag->tag_value.int_value = (int)read_buf[0];
            break;
        }
        }
        break;
    }
    // SIEMENS S7
    // ====================================================================
    case SIEMENS_S7: {

        switch (tag->value_type)
        {
        case VALUE_REAL: {
            float data_buf[1] = {};
            int res;

            res = Cli_ReadArea(link->link_config.s7_config.client, S7AreaDB, tag->tag_addr.s7_tag_addr.db_number,
                               tag->tag_addr.s7_tag_addr.start, 1, S7WLReal, data_buf);

            if (res < 0)
            {
                char error_text_buf[SIEMENS_ERR_BUF_LEN];
                tag->is_error = true;
                Cli_ErrorText(res, error_text_buf, SIEMENS_ERR_BUF_LEN);
                sprintf_s(tag->err_msg, "Could not read tag: %s", error_text_buf);
                return -1;
            }

            // Reset the error flag.
            tag->is_error = false;
            tag->tag_value.real_value = data_buf[0];
            break;
        }
        case VALUE_INT: {

            int data_buf[1] = {};
            int res;

            res = Cli_ReadArea(link->link_config.s7_config.client, S7AreaDB, tag->tag_addr.s7_tag_addr.db_number,
                               tag->tag_addr.s7_tag_addr.start, 1, S7WLWord, data_buf);

            if (res < 0)
            {
                char error_text_buf[SIEMENS_ERR_BUF_LEN];
                tag->is_error = true;
                Cli_ErrorText(res, error_text_buf, SIEMENS_ERR_BUF_LEN);
                sprintf_s(tag->err_msg, "Could not read tag: %s", error_text_buf);
                return -1;
            }

            tag->is_error = false;
            tag->tag_value.int_value = data_buf[0];
            break;
        }
        case VALUE_BOOL: {

            byte data_buf[1] = {};
            int res;

            res = Cli_ReadArea(link->link_config.s7_config.client, S7AreaDB, tag->tag_addr.s7_tag_addr.db_number,
                               // Offset must be expressed in number of bits (start * 8) + offset_bits.
                               (tag->tag_addr.s7_tag_addr.start * 8) + tag->tag_addr.s7_tag_addr.start_bit, 1, S7WLBit,
                               data_buf);

            if (res < 0)
            {
                char error_text_buf[SIEMENS_ERR_BUF_LEN];
                tag->is_error = true;
                Cli_ErrorText(res, error_text_buf, SIEMENS_ERR_BUF_LEN);
                sprintf_s(tag->err_msg, "Could not read tag: %s", error_text_buf);
                return -1;
            }

            tag->is_error = false;
            // TODO
            // Get the actual bit
            // This is a hack.
            tag->tag_value.bool_value = data_buf[0];
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
    return 0;
}
