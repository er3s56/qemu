/*
 * AD7792 Sigma-Delta ADC Emulation
 *
 * Copyright (c) 2025
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This emulates the Analog Devices AD7792 16/24-bit Sigma-Delta ADC.
 * The device is connected via GPIO bit-banging SPI.
 *
 * Data can be loaded from a CSV file with format: ch0,ch1,ch2
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/ssi/ad7792.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "migration/vmstate.h"

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(0, fmt, ## args)

/* Get register size in bytes */
static int ad7792_reg_size(uint8_t reg)
{
    switch (reg) {
    case AD7792_REG_STATUS:
    case AD7792_REG_ID:
    case AD7792_REG_IO:
        return 1;
    case AD7792_REG_MODE:
    case AD7792_REG_CONFIG:
    case AD7792_REG_OFFSET:
    case AD7792_REG_FS:
        return 2;
    case AD7792_REG_DATA:
        return 2;  /* AD7792 is 16-bit ADC */
    default:
        return 1;
    }
}

/* Read next line from CSV file and update ADC values */
static void ad7792_read_csv_line(AD7792State *s)
{
    char line[256];
    int ch0, ch1, ch2;

    if (!s->data_fp) {
        return;
    }

    if (fgets(line, sizeof(line), s->data_fp) == NULL) {
        /* End of file, rewind */
        rewind(s->data_fp);
        if (fgets(line, sizeof(line), s->data_fp) == NULL) {
            return;
        }
    }

    /* Skip comment lines */
    if (line[0] == '#') {
        ad7792_read_csv_line(s);
        return;
    }

    /* Parse CSV: ch0,ch1,ch2 */
    if (sscanf(line, "%d,%d,%d", &ch0, &ch1, &ch2) == 3) {
        s->adc_values[0] = ch0 & 0xFFFF;
        s->adc_values[1] = ch1 & 0xFFFF;
        s->adc_values[2] = ch2 & 0xFFFF;
    }
}

/* Read register value */
static uint32_t ad7792_read_reg(AD7792State *s, uint8_t reg)
{
    uint8_t channel;

    switch (reg) {
    case AD7792_REG_STATUS:
        /* Return status with current channel and RDY=0 (data ready) */
        channel = s->config & 0x7;
        return (channel & 0x7);  /* RDY=0 means data is ready */

    case AD7792_REG_MODE:
        return s->mode;

    case AD7792_REG_CONFIG:
        return s->config;

    case AD7792_REG_DATA:
        /* Read ADC data for selected channel */
        channel = s->config & 0x7;
        if (channel < AD7792_NUM_CHANNELS) {
            return s->adc_values[channel];
        }
        return 0;

    case AD7792_REG_ID:
        return s->id;

    case AD7792_REG_IO:
        return s->io;

    case AD7792_REG_OFFSET:
        return s->offset;

    case AD7792_REG_FS:
        return s->fs;

    default:
        qemu_log_mask(LOG_UNIMP, "ad7792: read from unknown reg 0x%x\n", reg);
        return 0;
    }
}

/* Write register value */
static void ad7792_write_reg(AD7792State *s, uint8_t reg, uint32_t value)
{
    switch (reg) {
    case AD7792_REG_MODE:
        s->mode = value & 0xFFFF;
        break;

    case AD7792_REG_CONFIG:
        s->config = value & 0xFFFF;
        break;

    case AD7792_REG_IO:
        s->io = value & 0xFF;
        break;

    case AD7792_REG_OFFSET:
        s->offset = value & 0xFFFF;
        break;

    case AD7792_REG_FS:
        s->fs = value & 0xFFFF;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "ad7792: write to unknown/readonly reg 0x%x\n",
                      reg);
        break;
    }
}

/* Process completed SPI byte */
static void ad7792_process_byte(AD7792State *s, uint8_t byte)
{
    uint32_t reg_value;

    switch (s->spi_state) {
    case AD7792_SPI_IDLE:
        /*
         * Check for continuation of previous read operation.
         * If byte is 0xFF and we have pending read data, this is
         * a data read phase (firmware uses separate write/read calls).
         */
        if (byte == 0xFF && s->pending_read) {
            /* Continue reading from the pending register.
             * At this point, byte pending_byte_count has just been read.
             * We need to prepare the NEXT byte (pending_byte_count + 1). */
            s->spi_state = AD7792_SPI_READ_DATA;
            reg_value = ad7792_read_reg(s, s->pending_reg);
            s->bytes_expected = ad7792_reg_size(s->pending_reg);

            /* Increment first - we just finished reading this byte */
            s->pending_byte_count++;
            s->byte_count = 0;  /* Reset bit counter for new byte */

            if (s->pending_byte_count < s->bytes_expected) {
                /* Prepare next byte */
                s->shift_out = (reg_value >> ((s->bytes_expected - 1 - s->pending_byte_count) * 8)) & 0xFF;
            } else {
                /* All bytes read */
                s->pending_read = false;
                s->pending_byte_count = 0;
                s->shift_out = 0xFF;
            }
            break;
        }

        /* This is the communication register byte */
        s->reg_addr = (byte >> 3) & 0x7;
        s->is_read = (byte >> 6) & 0x1;
        s->byte_count = 0;
        s->bytes_expected = ad7792_reg_size(s->reg_addr);

        if (s->is_read) {
            /* Mark pending read - actual data will be read in next SPI transaction */
            s->pending_read = true;
            s->pending_reg = s->reg_addr;
            s->pending_byte_count = 0;  /* Start from first byte */

            /* If reading DATA register, advance to next CSV line now */
            if (s->reg_addr == AD7792_REG_DATA) {
                ad7792_read_csv_line(s);
            }

            /* Also prepare data in case firmware reads in same transaction */
            s->spi_state = AD7792_SPI_READ_DATA;
            reg_value = ad7792_read_reg(s, s->reg_addr);
            s->shift_out = (reg_value >> ((s->bytes_expected - 1) * 8)) & 0xFF;
        } else {
            s->pending_read = false;
            s->pending_byte_count = 0;
            s->spi_state = AD7792_SPI_WRITE_DATA;
            s->data = 0;
        }
        break;

    case AD7792_SPI_READ_DATA:
        /* During read, we ignore incoming data but prepare next byte */
        s->byte_count++;
        if (s->byte_count < s->bytes_expected) {
            reg_value = ad7792_read_reg(s, s->reg_addr);
            s->shift_out = (reg_value >> ((s->bytes_expected - 1 - s->byte_count) * 8)) & 0xFF;
        } else {
            /* Read complete */
            s->spi_state = AD7792_SPI_IDLE;
            s->shift_out = 0xFF;
        }
        break;

    case AD7792_SPI_WRITE_DATA:
        /* Accumulate write data (MSB first) */
        s->data = (s->data << 8) | byte;
        s->byte_count++;
        if (s->byte_count >= s->bytes_expected) {
            /* Write complete */
            ad7792_write_reg(s, s->reg_addr, s->data);
            s->spi_state = AD7792_SPI_IDLE;
        }
        break;
    }
}

/* Handle CS (Chip Select) change */
static void ad7792_cs_set(void *opaque, int line, int level)
{
    AD7792State *s = AD7792(opaque);
    uint32_t reg_value;

    s->cs = level;

    if (level) {
        /* CS high: deselect, reset SPI state */
        s->spi_state = AD7792_SPI_IDLE;
        s->bit_count = 0;
        s->shift_in = 0;
        s->shift_out = 0xFF;
        /* Set MISO high when deselected */
        qemu_set_irq(s->miso_irq, 1);
    } else {
        /* CS low: select chip */
        s->bit_count = 0;
        s->shift_in = 0;

        /* If pending read, prepare MISO with data immediately */
        if (s->pending_read) {
            reg_value = ad7792_read_reg(s, s->pending_reg);
            s->bytes_expected = ad7792_reg_size(s->pending_reg);
            /* Use pending_byte_count to get correct byte */
            s->shift_out = (reg_value >> ((s->bytes_expected - 1 - s->pending_byte_count) * 8)) & 0xFF;
            /* Set first bit on MISO */
            qemu_set_irq(s->miso_irq, (s->shift_out >> 7) & 1);
        }
    }
}

/* Handle CLK change */
static void ad7792_clk_set(void *opaque, int line, int level)
{
    AD7792State *s = AD7792(opaque);
    int old_clk = s->clk;

    s->clk = level;

    /* Only process when CS is low (selected) */
    if (s->cs) {
        return;
    }

    if (!old_clk && level) {
        /* Rising edge: sample MOSI */
        s->shift_in = (s->shift_in << 1) | (s->mosi & 1);
        s->bit_count++;

        if (s->bit_count >= 8) {
            /* Complete byte received */
            ad7792_process_byte(s, s->shift_in);
            s->bit_count = 0;
            s->shift_in = 0;
        }
    } else if (old_clk && !level) {
        /* Falling edge: update MISO for next bit (firmware reads on next rising edge) */
        int miso_bit = (s->shift_out >> (7 - s->bit_count)) & 1;
        qemu_set_irq(s->miso_irq, miso_bit);
    }
}

/* Handle MOSI change */
static void ad7792_mosi_set(void *opaque, int line, int level)
{
    AD7792State *s = AD7792(opaque);
    s->mosi = level;
}

static void ad7792_reset(DeviceState *dev)
{
    AD7792State *s = AD7792(dev);

    /* Reset SPI state */
    s->cs = 1;
    s->clk = 0;
    s->mosi = 0;
    s->bit_count = 0;
    s->shift_in = 0;
    s->shift_out = 0xFF;
    s->spi_state = AD7792_SPI_IDLE;

    /* Reset registers to default values */
    s->status = 0;
    s->mode = 0x000A;       /* Single conversion, internal clock */
    s->config = 0x0710;     /* Gain=1, buffered, channel 0 */
    s->data = 0;
    s->id = AD7792_ID_VALUE;
    s->io = 0;
    s->offset = 0x8000;
    s->fs = 0x5540;

    /* Initialize ADC values */
    s->adc_values[0] = 0x8000;  /* Mid-scale for bipolar */
    s->adc_values[1] = 0x8000;
    s->adc_values[2] = 0x8000;

    /* Rewind data file if open */
    if (s->data_fp) {
        rewind(s->data_fp);
    }
}

static void ad7792_realize(DeviceState *dev, Error **errp)
{
    AD7792State *s = AD7792(dev);

    /* Initialize GPIO inputs */
    qdev_init_gpio_in_named(dev, ad7792_cs_set, "cs", 1);
    qdev_init_gpio_in_named(dev, ad7792_clk_set, "clk", 1);
    qdev_init_gpio_in_named(dev, ad7792_mosi_set, "mosi", 1);

    /* Initialize GPIO output for MISO */
    qdev_init_gpio_out_named(dev, &s->miso_irq, "miso", 1);

    /* Initialize registers to default values */
    s->cs = 1;
    s->clk = 0;
    s->mosi = 0;
    s->bit_count = 0;
    s->shift_in = 0;
    s->shift_out = 0xFF;
    s->spi_state = AD7792_SPI_IDLE;
    s->pending_read = false;
    s->pending_reg = 0;
    s->pending_byte_count = 0;
    s->status = 0;
    s->mode = 0x000A;
    s->config = 0x0710;
    s->data = 0;
    s->id = AD7792_ID_VALUE;
    s->io = 0;
    s->offset = 0x8000;
    s->fs = 0x5540;
    s->adc_values[0] = 0x8000;
    s->adc_values[1] = 0x8000;
    s->adc_values[2] = 0x8000;

    /* Open data file if specified */
    if (s->datafile && s->datafile[0]) {
        s->data_fp = fopen(s->datafile, "r");
        if (!s->data_fp) {
            error_setg(errp, "ad7792: cannot open data file '%s'", s->datafile);
            return;
        }
        /* Read initial values */
        ad7792_read_csv_line(s);
    }
}

static void ad7792_finalize(Object *obj)
{
    AD7792State *s = AD7792(obj);

    if (s->data_fp) {
        fclose(s->data_fp);
        s->data_fp = NULL;
    }
}

static Property ad7792_properties[] = {
    DEFINE_PROP_STRING("datafile", AD7792State, datafile),
    DEFINE_PROP_STRING("name", AD7792State, name),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_ad7792 = {
    .name = "ad7792",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(cs, AD7792State),
        VMSTATE_UINT8(clk, AD7792State),
        VMSTATE_UINT8(mosi, AD7792State),
        VMSTATE_UINT8(shift_in, AD7792State),
        VMSTATE_UINT8(shift_out, AD7792State),
        VMSTATE_UINT8(bit_count, AD7792State),
        VMSTATE_UINT8(reg_addr, AD7792State),
        VMSTATE_UINT8(is_read, AD7792State),
        VMSTATE_UINT8(byte_count, AD7792State),
        VMSTATE_UINT8(bytes_expected, AD7792State),
        VMSTATE_UINT8(status, AD7792State),
        VMSTATE_UINT16(mode, AD7792State),
        VMSTATE_UINT16(config, AD7792State),
        VMSTATE_UINT32(data, AD7792State),
        VMSTATE_UINT8(id, AD7792State),
        VMSTATE_UINT8(io, AD7792State),
        VMSTATE_UINT16(offset, AD7792State),
        VMSTATE_UINT16(fs, AD7792State),
        VMSTATE_UINT32_ARRAY(adc_values, AD7792State, AD7792_NUM_CHANNELS),
        VMSTATE_END_OF_LIST()
    }
};

static void ad7792_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ad7792_realize;
    device_class_set_legacy_reset(dc, ad7792_reset);
    dc->vmsd = &vmstate_ad7792;
    device_class_set_props(dc, ad7792_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo ad7792_info = {
    .name          = TYPE_AD7792,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(AD7792State),
    .instance_finalize = ad7792_finalize,
    .class_init    = ad7792_class_init,
};

static void ad7792_register_types(void)
{
    type_register_static(&ad7792_info);
}

type_init(ad7792_register_types)

