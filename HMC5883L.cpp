#include "HMC5883L.h"
#include "I2Cdev.h"
#include "arm_math.h"
#include "string.h"
#include "rtthread.h"

#define M_57_3 57.29577f
int16_t xOffset = -390;
int16_t yOffset = 241;
int16_t zOffset = 0;

HMC5883L::HMC5883L(void)
{
	devAddr = HMC5883L_ADDRESS;
	buffer = (uint8_t*)rt_malloc(6);
	strcpy(name,HMC5883L_NAME);
}

HMC5883L::~HMC5883L(void)
{
	if(buffer != null)
	{
		rt_free(buffer);
		buffer = null;
	}
}

bool HMC5883L::initialize(void)
{
    // write CONFIG_A register
    if(!I2Cdev::writeByte(devAddr, HMC5883L_RA_CONFIG_A,
        (HMC5883L_AVERAGING_8 << (HMC5883L_CRA_AVERAGE_BIT - HMC5883L_CRA_AVERAGE_LENGTH + 1)) |
        (HMC5883L_RATE_75     << (HMC5883L_CRA_RATE_BIT - HMC5883L_CRA_RATE_LENGTH + 1)) |
        (HMC5883L_BIAS_NORMAL << (HMC5883L_CRA_BIAS_BIT - HMC5883L_CRA_BIAS_LENGTH + 1)))) return false;

    // write CONFIG_B register
	if(!I2Cdev::writeByte(devAddr, HMC5883L_RA_CONFIG_B, HMC5883L_GAIN_1090 << (HMC5883L_CRB_GAIN_BIT - HMC5883L_CRB_GAIN_LENGTH + 1))) return false;
    
    // write MODE register
    return I2Cdev::writeByte(devAddr, HMC5883L_RA_MODE, HMC5883L_MODE_SINGLE << (HMC5883L_MODEREG_BIT - HMC5883L_MODEREG_LENGTH + 1));
}

bool HMC5883L::testConnection(void) 
{
	uint8_t buffer[3];
	I2Cdev::readBytes(devAddr, HMC5883L_RA_ID_A, 3, buffer);
	return (buffer[0] == 'H' && buffer[1] == '4' && buffer[2] == '3');
}

uint8_t HMC5883L::getData(void* data1,void* data2,void* data3,void* data4,void* data5,void* data6)
{
	getHeadingCal((int16_t*)data1,(int16_t*)data2,(int16_t*)data3);
	if(data4 != null) getHeadingCal((float*)data4);
	return 4;
}

// DATA* registers

/** Get 3-axis heading measurements.
 * In the event the ADC reading overflows or underflows for the given channel,
 * or if there is a math overflow during the bias measurement, this data
 * register will contain the value -4096. This register value will clear when
 * after the next valid measurement is made. Note that this method automatically
 * clears the appropriate bit in the MODE register if Single mode is active.
 * @param x 16-bit signed integer container for X-axis heading
 * @param y 16-bit signed integer container for Y-axis heading
 * @param z 16-bit signed integer container for Z-axis heading
 * @see HMC5883L_RA_DATAX_H
 */
void HMC5883L::getHeadingRaw(int16_t *x, int16_t *y, int16_t *z) 
{
	I2Cdev::readBytes(devAddr, HMC5883L_RA_DATAX_H, 6, buffer);
	I2Cdev::writeByte(devAddr, HMC5883L_RA_MODE, HMC5883L_MODE_SINGLE << (HMC5883L_MODEREG_BIT - HMC5883L_MODEREG_LENGTH + 1));
	*x = (((int16_t)buffer[0]) << 8) | buffer[1];
	*y = (((int16_t)buffer[4]) << 8) | buffer[5];
	*z = (((int16_t)buffer[2]) << 8) | buffer[3];
}

void HMC5883L::getHeadingCal(int16_t *x, int16_t *y, int16_t *z)
{
	I2Cdev::readBytes(devAddr, HMC5883L_RA_DATAX_H, 6, buffer);
	I2Cdev::writeByte(devAddr, HMC5883L_RA_MODE, HMC5883L_MODE_SINGLE << (HMC5883L_MODEREG_BIT - HMC5883L_MODEREG_LENGTH + 1));
	*x = (((int16_t)buffer[0]) << 8) | buffer[1];
	*y = (((int16_t)buffer[4]) << 8) | buffer[5];
	*z = (((int16_t)buffer[2]) << 8) | buffer[3];
	
	*x -= xOffset;
	*y -= yOffset;
}

void HMC5883L::getHeadingCal(float *heading)
{
	int16_t x,y,z;
	getHeadingRaw(&x,&y,&z);
	*heading = atan2((float)y - yOffset, (float)x - xOffset);
    if(*heading < 0)
      *heading += 2 * PI;
	*heading = *heading * M_57_3;
}

void HMC5883L::setOffset(void)
{
	uint32_t tick = rt_tick_get() + 10000; //10s
	int16_t data[3],min[3],max[3];
	for(uint8_t i=0;i<3;i++)
	{
		min[i] = 10000;
		max[i] = -10000;
	}
	while(tick>rt_tick_get())
	{
		getHeadingRaw(&data[0],&data[1],&data[2]);
		for(uint8_t i=0;i<3;i++)
		{
			if(data[i]<min[i]) min[i] = data[i];
			if(data[i]>max[i]) max[i] = data[i];
		}
		rt_thread_delay(10);
	}
	xOffset = (min[0] + max[0]) / 2;
	yOffset = (min[1] + max[1]) / 2;
	zOffset = (min[2] + max[2]) / 2;
}
