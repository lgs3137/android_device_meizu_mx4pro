#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "linux/i2c-dev.h"
#include "NXP_I2C.h"
int gI2cBufSz = NXP_I2C_MAX_SIZE;
/*
 * accounting globals
 */
int gNXP_i2c_writes = 0, gNXP_i2c_reads = 0;
static int client = -1;
enum NXP_I2C_Error NXP_I2C_Open(const char *dev, unsigned char slave_address)
{
    if(client >= 0) return NXP_I2C_Ok;
    if((client = open(dev, O_RDWR)) < 0)
    {
        return NXP_I2C_UnsupportedValue;
    }
    if(ioctl(client, I2C_SLAVE, slave_address >> 1) < 0)
    {
        return NXP_I2C_UnsupportedValue;
    }
    return NXP_I2C_Ok;
}
enum NXP_I2C_Error NXP_I2C_Close(void)
{
    if(client < 0)
    {
        return NXP_I2C_NoInit;
    }
    if(close(client) < 0)
    {
        return NXP_I2C_UnsupportedValue;
    }
    return NXP_I2C_Ok;
}
enum NXP_I2C_Error NXP_I2C_Write(unsigned char slave_address,
                                 int write_bytes, const unsigned char write_data[])
{
    int rc;
    unsigned char buffer[NXP_I2C_MAX_SIZE + 1];
    if(client < 0)
    {
        return NXP_I2C_NoInit;
    }
    if(write_bytes > gI2cBufSz)
    {
        return NXP_I2C_UnsupportedValue;
    }
    if(slave_address != 0x68)
    {
        return NXP_I2C_UnsupportedValue;
    }
    memcpy((void *)buffer, (void *)write_data, write_bytes);
    rc = write(client, buffer, write_bytes);
    if(rc < 0)
    {
        return NXP_I2C_UnsupportedValue;
    }
    return NXP_I2C_Ok;
}
enum NXP_I2C_Error NXP_I2C_WriteRead(unsigned char slave_address,
                                     int write_bytes, const unsigned char write_data[],
                                     int read_bytes, unsigned char read_data[])
{
    int rc;
    unsigned char wbuffer[NXP_I2C_MAX_SIZE + 1];
    unsigned char rbuffer[NXP_I2C_MAX_SIZE + 1];
    if(client < 0)
    {
        return NXP_I2C_NoInit;
    }
    if(write_bytes > gI2cBufSz)
    {
        return NXP_I2C_UnsupportedValue;
    }
    if(read_bytes > gI2cBufSz)
    {
        return NXP_I2C_UnsupportedValue;
    }
    if(slave_address != 0x68)
    {
        return NXP_I2C_UnsupportedValue;
    }
    memcpy((void *)wbuffer, (void *)write_data, write_bytes);
    rc = write(client, wbuffer, write_bytes);
    if(rc < 0)
    {
        return NXP_I2C_UnsupportedValue;
    }
    rc = read(client, rbuffer, read_bytes);
    if(rc < 0)
    {
        return NXP_I2C_UnsupportedValue;
    }
    memcpy((void *)read_data, (void *)rbuffer, read_bytes);
    return NXP_I2C_Ok;
}
int NXP_I2C_BufferSize(void)
{
    return gI2cBufSz > 0 ? gI2cBufSz : NXP_I2C_MAX_SIZE;
}
