/*
 * AD7792 Sigma-Delta ADC Emulation
 *
 * Copyright (c) 2025
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This emulates the Analog Devices AD7792 16/24-bit Sigma-Delta ADC
 * with SPI interface. The device is connected via GPIO bit-banging SPI.
 */

#ifndef HW_SSI_AD7792_H
#define HW_SSI_AD7792_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* AD7792 Register Addresses (from communication register) */
#define AD7792_REG_COMM     0x0     /* Communications Register (Write only) */
#define AD7792_REG_STATUS   0x0     /* Status Register (Read only, same addr) */
#define AD7792_REG_MODE     0x1     /* Mode Register */
#define AD7792_REG_CONFIG   0x2     /* Configuration Register */
#define AD7792_REG_DATA     0x3     /* Data Register (Read only) */
#define AD7792_REG_ID       0x4     /* ID Register (Read only) */
#define AD7792_REG_IO       0x5     /* IO Register */
#define AD7792_REG_OFFSET   0x6     /* Offset Register */
#define AD7792_REG_FS       0x7     /* Full-Scale Register */

/* Communication Register bits */
#define AD7792_COMM_WEN     (1 << 7)    /* Write Enable (must be 0) */
#define AD7792_COMM_RW      (1 << 6)    /* Read/Write: 1=Read, 0=Write */
#define AD7792_COMM_ADDR(x) (((x) & 0x7) << 3)  /* Register address */
#define AD7792_COMM_CREAD   (1 << 2)    /* Continuous Read */

/* Status Register bits */
#define AD7792_STATUS_RDY   (1 << 7)    /* Data Ready (active low) */
#define AD7792_STATUS_ERR   (1 << 6)    /* Error */
#define AD7792_STATUS_NOXREF (1 << 5)   /* No External Reference */
#define AD7792_STATUS_CH(x) ((x) & 0x7) /* Channel */

/* Mode Register bits */
#define AD7792_MODE_SEL(x)  (((x) & 0x7) << 13) /* Mode Select */
#define AD7792_MODE_CLKSRC(x) (((x) & 0x3) << 6) /* Clock Source */
#define AD7792_MODE_RATE(x) ((x) & 0xF)  /* Filter Update Rate */

/* Mode Select values */
#define AD7792_MODE_CONT    0   /* Continuous Conversion */
#define AD7792_MODE_SINGLE  1   /* Single Conversion */
#define AD7792_MODE_IDLE    2   /* Idle */
#define AD7792_MODE_PWRDN   3   /* Power-Down */

/* Configuration Register bits */
#define AD7792_CONFIG_VBIAS(x) (((x) & 0x3) << 14) /* Bias Voltage */
#define AD7792_CONFIG_BO    (1 << 13)   /* Burnout Current */
#define AD7792_CONFIG_UB    (1 << 12)   /* Unipolar/Bipolar */
#define AD7792_CONFIG_BOOST (1 << 11)   /* Boost */
#define AD7792_CONFIG_GAIN(x) (((x) & 0x7) << 8) /* Gain Select */
#define AD7792_CONFIG_REFSEL (1 << 7)   /* Reference Select */
#define AD7792_CONFIG_BUF   (1 << 4)    /* Buffered Mode */
#define AD7792_CONFIG_CH(x) ((x) & 0x7) /* Channel Select */

/* Channel definitions */
#define AD7792_CH_AIN1      0   /* AIN1(+) - AIN1(-) */
#define AD7792_CH_AIN2      1   /* AIN2(+) - AIN2(-) */
#define AD7792_CH_AIN3      2   /* AIN3(+) - AIN3(-) */
#define AD7792_CH_TEMP      6   /* Temperature Sensor */
#define AD7792_CH_AVDD      7   /* AVDD Monitor */

/* ID Register value */
#define AD7792_ID_VALUE     0x4A    /* AD7792 ID (16-bit mode) */

/* Number of ADC channels */
#define AD7792_NUM_CHANNELS 3

/* SPI state machine states */
typedef enum {
    AD7792_SPI_IDLE,        /* Waiting for command byte */
    AD7792_SPI_READ_DATA,   /* Reading data from register */
    AD7792_SPI_WRITE_DATA,  /* Writing data to register */
} AD7792SpiState;

#define TYPE_AD7792 "ad7792"
OBJECT_DECLARE_SIMPLE_TYPE(AD7792State, AD7792)

struct AD7792State {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    /* SPI signals */
    qemu_irq miso_irq;      /* MISO output (directly set GPIO input) */

    /* SPI bit-banging state */
    uint8_t cs;             /* Chip Select (active low) */
    uint8_t clk;            /* Clock state */
    uint8_t mosi;           /* MOSI input value */

    /* SPI shift register */
    uint8_t shift_in;       /* Incoming data (MOSI) */
    uint8_t shift_out;      /* Outgoing data (MISO) */
    uint8_t bit_count;      /* Bits shifted */

    /* SPI state machine */
    AD7792SpiState spi_state;
    uint8_t reg_addr;       /* Current register address */
    uint8_t is_read;        /* 1 = read operation */
    uint8_t byte_count;     /* Bytes transferred in current operation */
    uint8_t bytes_expected; /* Total bytes expected for current register */
    bool pending_read;      /* Read command issued, waiting for data phase */
    uint8_t pending_reg;    /* Register to read in pending read */
    uint8_t pending_byte_count; /* Bytes already read in multi-byte pending read */

    /* AD7792 Registers */
    uint8_t status;         /* Status Register */
    uint16_t mode;          /* Mode Register */
    uint16_t config;        /* Configuration Register */
    uint32_t data;          /* Data Register (24-bit) */
    uint8_t id;             /* ID Register */
    uint8_t io;             /* IO Register */
    uint16_t offset;        /* Offset Register */
    uint16_t fs;            /* Full-Scale Register */

    /* Data source */
    char *datafile;         /* Path to CSV data file */
    FILE *data_fp;          /* File pointer */
    uint32_t adc_values[AD7792_NUM_CHANNELS]; /* Current ADC values per channel */

    /* Debug */
    char *name;
};

#endif /* HW_SSI_AD7792_H */

