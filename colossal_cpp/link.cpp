#pragma once
#include "libmodbus/modbus.h"
#include "libplctag/libplctag.h"
#include "link.h"
#include "snap7/snap7.h"
#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <string.h>
#include <winnls.h>

static uint32_t get_udint_s7(byte buffer[], int pos)
{
    uint32_t res;
    res = buffer[pos];
    res <<= 8;
    res |= buffer[pos + 1];
    res <<= 8;
    res |= buffer[pos + 2];
    res <<= 8;
    res |= buffer[pos + 3];
    return res;
}

static float get_real_s7(byte buffer[], int pos)
{
    uint32_t pack = get_udint_s7(buffer, pos);
    float res = {};
    memcpy(&res, &pack, 4);
    return res;
}

static int16_t get_int_s7(byte buffer[], int pos)
{
    return (int16_t)((buffer[pos] << 8) | buffer[pos + 1]);
}

int cl_new_tag(Link *link, char const *name, int id, TagAddress tag_addr, int value_type, int protocol, bool enabled)
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
    link->tags[id].enabled = enabled;
    link->tags[id].logged = true;
    link->tags[id].value_type = value_type;
    link->tags[id].tag_value.real_value = 0.0;
    link->tags[id].tag_value.int_value = 0;
    link->tags[id].tag_value.bool_value = 0;

    return 0;
}

Link *cl_new_link(char const *name, int id, int protocol, LinkConfig config, size_t tag_count, bool active)
{
    Link link_init = {};

    Link *link = &link_init;

    link->id = id;
    link->active = active;
    strcpy_s(link->name, name);
    link->protocol = protocol;
    link->link_config = config;
    link->tag_count = tag_count;
    link->is_error = true;
    link->poll_delay = 1000;
    strcpy_s(link->err_msg, "The link is disconnected.");
    link->need_to_reconnect = true;
    link->timestamp = 0;
    link->logging_type = CL_LOCAL_LOGGING;
    link->logging_enabled = false;
    strcpy_s(link->url, "http://127.0.0.1:8181/api/v3/write_lp?db=colossal&precision=second");
    strcpy_s(link->token,
             "apiv3_VJSoTa7OaIRjYXweq0kNJY2gA2UlVcqh-knb8oOJoSpuU3QFfqrtlZk5NvJ-xviVJz8Pp0bSkjntbdBqGHFUKQ");

    link->tags = (Tag *)malloc(tag_count * sizeof(Tag));

    for (size_t i = 0; i < tag_count; i++)
    {
        char name_buf[TAG_NAME_BUF_LEN];
        sprintf_s(name_buf, "TAG%d", i);

        // Address initialization with default values.
        TagAddress tag_addr = {};
        tag_addr.mb_addr = (int)i * 2;
        sprintf_s(tag_addr.eip_tag_addr.tag_name, "Tag%d", i);
        sprintf_s(tag_addr.eip_tag_addr.eip_path, EIP_TAG_TEMPLATE, link->link_config.eip_config.ip,
                  tag_addr.eip_tag_addr.tag_name);
        tag_addr.eip_tag_addr.eip_tag_ptr = NULL;
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
            cl_new_tag(link, name_buf, i, tag_addr, value_type, MB_TCP, false);
            break;
        }
        case MB_SERIAL: {
            cl_new_tag(link, name_buf, i, tag_addr, value_type, MB_SERIAL, false);
            break;
        }
        case SIEMENS_S7: {
            cl_new_tag(link, name_buf, i, tag_addr, value_type, SIEMENS_S7, false);
            break;
        }
        case EIP: {

            link->tags[i].enabled = false;
            cl_new_tag(link, name_buf, i, tag_addr, value_type, EIP, false);
            break;
        }
        // TODO: switch to the other protocols as well.
        default: {
            cl_new_tag(link, name_buf, i, tag_addr, value_type, MB_TCP, false);
            break;
        }
        }
    }

    return link;
}

// TODO
/// Connect a link and get a connection context for the protocols that support it (e.g Modbus)
/// This function must be called after cl_link_new.
//
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
    case EIP: {
        // EIP has no notion of a global PLC. It only has the notion of a Tag.
        // The connect_link function should iterate over the link Tags and create them.

        for (int i = 0; i < link->tag_count; i++)
        {
            int32_t eip_tag = 0;
            int rc;

            if (link->tags[i].enabled)
            {
                eip_tag = plc_tag_create(link->tags[i].tag_addr.eip_tag_addr.eip_path, 5000);
                if (eip_tag < 0)
                {
                    link->is_error = true;
                    sprintf_s(link->err_msg, "Could not create EIP tag %d: %s", i, plc_tag_decode_error(eip_tag));
                    return -1;
                }

                if ((rc = plc_tag_status(eip_tag)) != PLCTAG_STATUS_OK)
                {
                    link->is_error = true;
                    sprintf_s(link->err_msg, "Could not setup EIP tag %d: %s", i, plc_tag_decode_error(rc));
                    return -1;
                }
                // If successful update our eip_tag_ptr to be used for reading the tag later.
                link->tags[i].tag_addr.eip_tag_addr.eip_tag_ptr = eip_tag;
            }
        }
        link->is_error = false;
        break;
    }
    case OPCUA: {
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
            byte data_buf[4] = {};
            int res = {};

            res = Cli_ReadArea(link->link_config.s7_config.client, S7AreaDB, tag->tag_addr.s7_tag_addr.db_number,
                               tag->tag_addr.s7_tag_addr.start, 4, S7WLByte, data_buf);

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
            tag->tag_value.real_value = get_real_s7(data_buf, 0);
            break;
        }
        case VALUE_INT: {

            byte data_buf[2] = {};
            int res;

            res = Cli_ReadArea(link->link_config.s7_config.client, S7AreaDB, tag->tag_addr.s7_tag_addr.db_number,
                               tag->tag_addr.s7_tag_addr.start, 2, S7WLByte, data_buf);

            if (res < 0)
            {
                char error_text_buf[SIEMENS_ERR_BUF_LEN];
                tag->is_error = true;
                Cli_ErrorText(res, error_text_buf, SIEMENS_ERR_BUF_LEN);
                sprintf_s(tag->err_msg, "Could not read tag: %s", error_text_buf);
                return -1;
            }

            tag->is_error = false;
            tag->tag_value.int_value = get_int_s7(data_buf, 0);
            break;
        }
        case VALUE_BOOL: {

            byte data_buf[1] = {};
            int res = {};

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
    // Allen Bradley EIP
    // ====================================================================
    case EIP: {

        if (tag->tag_addr.eip_tag_addr.eip_tag_ptr == NULL)
        {
            tag->is_error = true;
            sprintf_s(tag->err_msg, "Could not read tag: The EIP tag has not been properly created.");
            return -1;
        }

        int rc;
        switch (tag->value_type)
        {

        case VALUE_REAL: {

            rc = plc_tag_read(tag->tag_addr.eip_tag_addr.eip_tag_ptr, 5000);

            if (rc != PLCTAG_STATUS_OK)
            {
                sprintf_s(tag->err_msg, "Could not read tag: %s", plc_tag_decode_error(rc));
                tag->is_error = true;
                return -1;
            }

            // Reset the error flag.
            tag->is_error = false;
            tag->tag_value.real_value = plc_tag_get_float32(tag->tag_addr.eip_tag_addr.eip_tag_ptr, 0);
            break;
        }
        case VALUE_INT: {
            rc = plc_tag_read(tag->tag_addr.eip_tag_addr.eip_tag_ptr, 5000);

            if (rc != PLCTAG_STATUS_OK)
            {
                sprintf_s(tag->err_msg, "Could not read tag: %s", plc_tag_decode_error(rc));
                tag->is_error = true;
                return -1;
            }

            // Reset the error flag.
            tag->is_error = false;
            tag->tag_value.int_value = plc_tag_get_int16(tag->tag_addr.eip_tag_addr.eip_tag_ptr, 0);
            break;
        }
        case VALUE_BOOL: {
            rc = plc_tag_read(tag->tag_addr.eip_tag_addr.eip_tag_ptr, 5000);

            if (rc != PLCTAG_STATUS_OK)
            {
                sprintf_s(tag->err_msg, "Could not read tag: %s", plc_tag_decode_error(rc));
                tag->is_error = true;
                return -1;
            }

            // Reset the error flag.
            tag->is_error = false;
            tag->tag_value.bool_value = plc_tag_get_bit(tag->tag_addr.eip_tag_addr.eip_tag_ptr, 0);
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
